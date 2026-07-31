#include "util/input/input_util.h"

#include "app/app_runtime_internal.h"
#include "util/json/bounded_json_line_reader.h"
#include <QGuiApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaObject>
#include <QProcess>
#include <QRect>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QFileInfo>
#include <QtGlobal>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

#include "common/logger_manager.h"
#include "desktop_capture/win/screen_capture_utils.h"

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <tlhelp32.h>
#include <userenv.h>
#include <wtsapi32.h>

namespace
{
constexpr int kBrokerStartTimeoutMs = 15000;
constexpr int kBrokerConnectTimeoutMs = 200;
constexpr int kBrokerWriteTimeoutMs = 200;
constexpr int kBrokerHeartbeatIntervalMs = 2000;
constexpr int kBrokerHeartbeatTimeoutMs = 500;
constexpr int kBrokerRetryThrottleMs = 30000;
constexpr int kServiceStartRetryThrottleMs = 30000;
constexpr int kBrokerIdleQuitMs = 30 * 60 * 1000;
constexpr qint64 kMaxBrokerCommandBytes = 64 * 1024;
constexpr int kMinimumUsefulSecureDesktopScore = 250;
constexpr int kMinimumSecureDesktopCentralEdge = 8;
constexpr const char *kAiranServiceName = "airan-desk";
constexpr const wchar_t *kAiranServiceNameW = L"airan-desk";
constexpr const char *kAiranServiceDisplayName = "Airan Desk Service";
constexpr const char *kAiranServiceDescription = "Airan Desk LocalSystem service for unattended remote input.";

bool g_windowsInputBrokerMode = false;
std::atomic_bool g_windowsSessionLocked{false};
std::atomic_llong g_lastServiceStartAttemptMs{0};
std::atomic_bool g_inputDesktopSwitchFailureLogged{false};
std::atomic_bool g_cursorPositionFailureLogged{false};
std::atomic_bool g_brokerInputCommandLogged{false};
std::atomic_bool g_secureCaptureDesktopLogged{false};
std::atomic_bool g_secureCaptureInProgress{false};
std::atomic_bool g_windowsReadinessCancelled{false};
std::mutex g_brokerStateMutex;
std::recursive_mutex g_unattendedServiceOperationMutex;

bool windowsReadinessCancelled()
{
    return g_windowsReadinessCancelled.load(std::memory_order_acquire);
}

enum class BrokerKind
{
    None,
    Temporary,
    Unattended,
};

struct BrokerState
{
    QString serverName;
    QString token;
    qint64 lastStartAttemptMs = 0;
    bool startAttempted = false;
    BrokerKind kind = BrokerKind::None;
};

struct BrokerEndpoint
{
    QString serverName;
    QString token;
    BrokerKind kind = BrokerKind::None;
};

BrokerState &brokerState()
{
    static BrokerState state;
    return state;
}

BrokerEndpoint currentBrokerEndpoint()
{
    std::lock_guard<std::mutex> lock(g_brokerStateMutex);
    const BrokerState &state = brokerState();
    return {state.serverName, state.token, state.kind};
}

QString uuidWithoutBraces()
{
    QString value = QUuid::createUuid().toString();
    value.remove(QLatin1Char('{'));
    value.remove(QLatin1Char('}'));
    return value;
}

DWORD currentProcessSessionId()
{
    DWORD sessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId))
        return 0;
    return sessionId;
}

QString unattendedServerNameForSession(DWORD sessionId)
{
    if (sessionId == 0xFFFFFFFF)
        sessionId = 0;
    return QStringLiteral("airan-desk-input-service-%1").arg(sessionId);
}

QString unattendedServerName()
{
    return unattendedServerNameForSession(currentProcessSessionId());
}

QString serviceControlServerName()
{
    return QStringLiteral("airan-desk-input-service");
}

QString mutexSafeName(QString value)
{
    for (QChar &ch : value)
    {
        if (!ch.isLetterOrNumber())
            ch = QLatin1Char('_');
    }
    return value;
}

QString brokerSingletonMutexNameForServer(const QString &serverName)
{
    return QStringLiteral("Global\\airan-desk-input-broker-%1").arg(mutexSafeName(serverName));
}

QString brokerSingletonMutexNameForSession(DWORD sessionId)
{
    return brokerSingletonMutexNameForServer(unattendedServerNameForSession(sessionId));
}

HANDLE createBrokerSingletonMutex(const QString &serverName, bool *alreadyExists)
{
    if (alreadyExists)
        *alreadyExists = false;
    const std::wstring name = brokerSingletonMutexNameForServer(serverName).toStdWString();
    HANDLE mutex = CreateMutexW(nullptr, TRUE, name.c_str());
    if (!mutex)
        return nullptr;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (alreadyExists)
            *alreadyExists = true;
    }
    return mutex;
}

bool brokerSingletonMutexExistsForSession(DWORD sessionId)
{
    const std::wstring name = brokerSingletonMutexNameForSession(sessionId).toStdWString();
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
    if (!mutex)
        return false;
    CloseHandle(mutex);
    return true;
}

bool runProcess(const QString &program, const QStringList &arguments, int timeoutMs, QString *errorMessage)
{
    QProcess proc;
    proc.start(program, arguments);
    if (!proc.waitForStarted(3000))
    {
        if (errorMessage)
            *errorMessage = proc.errorString();
        return false;
    }
    if (!proc.waitForFinished(timeoutMs))
    {
        proc.kill();
        if (errorMessage)
            *errorMessage = InputUtil::tr("Process timed out.");
        return false;
    }
    if (proc.exitCode() != 0)
    {
        if (errorMessage)
        {
            const QByteArray stderrData = proc.readAllStandardError();
            const QByteArray stdoutData = proc.readAllStandardOutput();
            *errorMessage = QString::fromLocal8Bit(stderrData.isEmpty() ? stdoutData : stderrData).trimmed();
        }
        return false;
    }
    return true;
}

int runElevatedTargetAndWait(const QString &program,
                             const QString &parameters,
                             int timeoutMs,
                             QString *errorMessage)
{
    SHELLEXECUTEINFOW execInfo = {};
    execInfo.cbSize = sizeof(execInfo);
    execInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    execInfo.lpVerb = L"runas";
    execInfo.lpFile = reinterpret_cast<LPCWSTR>(program.utf16());
    execInfo.lpParameters = reinterpret_cast<LPCWSTR>(parameters.utf16());
    execInfo.nShow = SW_HIDE;
    if (!ShellExecuteExW(&execInfo))
    {
        const DWORD error = GetLastError();
        if (errorMessage)
        {
            *errorMessage = error == ERROR_CANCELLED
                                ? InputUtil::tr("Administrator approval was canceled.")
                                : InputUtil::tr("Administrator approval failed. Windows error: %1.").arg(error);
        }
        return static_cast<int>(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
    }

    const DWORD waitResult = WaitForSingleObject(execInfo.hProcess, static_cast<DWORD>(timeoutMs));
    if (waitResult == WAIT_TIMEOUT)
    {
        TerminateProcess(execInfo.hProcess, WAIT_TIMEOUT);
        WaitForSingleObject(execInfo.hProcess, 1000);
        CloseHandle(execInfo.hProcess);
        if (errorMessage)
            *errorMessage = InputUtil::tr("The elevated operation did not finish in time.");
        return WAIT_TIMEOUT;
    }
    if (waitResult == WAIT_FAILED)
    {
        const DWORD error = GetLastError();
        CloseHandle(execInfo.hProcess);
        if (errorMessage)
            *errorMessage = InputUtil::tr("Waiting for the elevated operation failed. Windows error: %1.").arg(error);
        return static_cast<int>(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
    }

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(execInfo.hProcess, &exitCode))
    {
        const DWORD error = GetLastError();
        CloseHandle(execInfo.hProcess);
        if (errorMessage)
            *errorMessage = InputUtil::tr("Reading the elevated operation result failed. Windows error: %1.").arg(error);
        return static_cast<int>(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
    }
    CloseHandle(execInfo.hProcess);
    if (exitCode != 0)
    {
        if (errorMessage)
            *errorMessage = InputUtil::tr("The elevated operation failed. Exit code: %1.").arg(exitCode);
        return static_cast<int>(exitCode);
    }
    return 0;
}

QString elevationCancelEventPrefix()
{
    return QStringLiteral("Local\\airan-desk-elevation-cancel-");
}

bool isValidElevationCancelEventName(const QString &eventName)
{
    const QString prefix = elevationCancelEventPrefix();
    if (!eventName.startsWith(prefix, Qt::CaseSensitive))
        return false;
    const QString suffix = eventName.mid(prefix.size());
    if (suffix.size() != 36)
        return false;
    const QUuid uuid(QStringLiteral("{%1}").arg(suffix));
    if (uuid.isNull())
        return false;
    QString canonical = uuid.toString();
    canonical.remove(QLatin1Char('{'));
    canonical.remove(QLatin1Char('}'));
    return canonical.compare(suffix, Qt::CaseInsensitive) == 0;
}

bool createElevationCancellationEvent(QString *eventName, HANDLE *eventHandle, QString *errorMessage)
{
    if (!eventName || !eventHandle)
        return false;
    *eventHandle = nullptr;
    *eventName = elevationCancelEventPrefix() + uuidWithoutBraces();

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const wchar_t *sddl = L"D:P(A;;GA;;;OW)(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00100000;;;AU)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl, SDDL_REVISION_1, &descriptor, nullptr))
    {
        if (errorMessage)
            *errorMessage = InputUtil::tr("Administrator approval failed. Windows error: %1.").arg(GetLastError());
        return false;
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    const std::wstring nativeName = eventName->toStdWString();
    *eventHandle = CreateEventW(&attributes, TRUE, FALSE, nativeName.c_str());
    const DWORD createError = GetLastError();
    LocalFree(descriptor);
    if (!*eventHandle)
    {
        if (errorMessage)
            *errorMessage = InputUtil::tr("Administrator approval failed. Windows error: %1.").arg(createError);
        return false;
    }
    return true;
}

bool runElevatedProcessAndWait(const QString &program,
                               const QString &operation,
                               int timeoutMs,
                               QString *errorMessage)
{
    QString cancelEventName;
    HANDLE cancelEvent = nullptr;
    if (!createElevationCancellationEvent(&cancelEventName, &cancelEvent, errorMessage))
        return false;

    QProcess launcher;
    launcher.setProgram(program);
    launcher.setArguments({QStringLiteral("--airan-elevation-launcher"),
                           QStringLiteral("--airan-elevation-operation"),
                           operation,
                           QStringLiteral("--airan-cancel-event"),
                           cancelEventName});
    launcher.start();
    auto cancelLauncher = [&launcher, cancelEvent]() {
        SetEvent(cancelEvent);
        if (launcher.state() != QProcess::NotRunning)
        {
            launcher.kill();
            launcher.waitForFinished(1000);
        }
    };

    bool launcherStarted = false;
    QElapsedTimer startTimer;
    startTimer.start();
    while (!launcherStarted && launcher.state() != QProcess::NotRunning && startTimer.elapsed() < 5000)
    {
        launcherStarted = launcher.waitForStarted(100);
        if (windowsReadinessCancelled())
        {
            cancelLauncher();
            CloseHandle(cancelEvent);
            if (errorMessage)
                *errorMessage = InputUtil::tr("The elevated operation did not finish in time.");
            return false;
        }
    }
    if (!launcherStarted)
    {
        cancelLauncher();
        CloseHandle(cancelEvent);
        if (errorMessage)
            *errorMessage = InputUtil::tr("Administrator approval failed. Windows error: %1.")
                                .arg(static_cast<int>(launcher.error()));
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (launcher.state() != QProcess::NotRunning && timer.elapsed() < timeoutMs)
    {
        launcher.waitForFinished(100);
        if (windowsReadinessCancelled())
        {
            cancelLauncher();
            CloseHandle(cancelEvent);
            if (errorMessage)
                *errorMessage = InputUtil::tr("The elevated operation did not finish in time.");
            return false;
        }
    }

    if (launcher.state() != QProcess::NotRunning)
    {
        cancelLauncher();
        CloseHandle(cancelEvent);
        if (errorMessage)
            *errorMessage = InputUtil::tr("The elevated operation did not finish in time.");
        return false;
    }

    CloseHandle(cancelEvent);
    const int exitCode = launcher.exitCode();
    if (launcher.exitStatus() == QProcess::NormalExit && exitCode == 0)
        return true;
    if (errorMessage)
    {
        if (exitCode == ERROR_CANCELLED)
            *errorMessage = InputUtil::tr("Administrator approval was canceled.");
        else if (exitCode == WAIT_TIMEOUT)
            *errorMessage = InputUtil::tr("The elevated operation did not finish in time.");
        else
            *errorMessage = InputUtil::tr("The elevated operation failed. Exit code: %1.").arg(exitCode);
    }
    return false;
}

QString elevatedSelfFailureImpact(const QString &argument)
{
    if (argument == QStringLiteral("--airan-install-service-elevated"))
    {
        return InputUtil::tr("The Airan Desk Windows service was not installed or updated. The remote side may be unable to capture or control the Windows lock screen, and Ctrl+Alt+Del will be unavailable.");
    }
    if (argument == QStringLiteral("--airan-start-service-elevated"))
    {
        return InputUtil::tr("The Airan Desk Windows service was not started. The remote side may be unable to capture or control the Windows lock screen, and Ctrl+Alt+Del will be unavailable until the service starts.");
    }
    if (argument == QStringLiteral("--airan-uninstall-service-elevated"))
    {
        return InputUtil::tr("The Airan Desk Windows service was not removed. Unattended remote input may remain enabled until the service is stopped or uninstalled.");
    }
    return QString();
}

QString withElevatedSelfFailureImpact(const QString &error, const QString &argument)
{
    const QString impact = elevatedSelfFailureImpact(argument);
    if (impact.isEmpty())
        return error;
    if (error.isEmpty())
        return impact;
    return QStringLiteral("%1\n\n%2").arg(error, impact);
}

bool isAiranServiceInstalled()
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return false;

    SC_HANDLE service = OpenServiceW(manager, kAiranServiceNameW, SERVICE_QUERY_STATUS);
    if (!service)
    {
        CloseServiceHandle(manager);
        return false;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return true;
}

bool startAiranService()
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
    {
        LOG_WARN("Failed to open service manager for Airan service start: {}", GetLastError());
        return false;
    }

    SC_HANDLE queryService = OpenServiceW(manager, kAiranServiceNameW, SERVICE_QUERY_STATUS);
    if (queryService)
    {
        SERVICE_STATUS_PROCESS status{};
        DWORD bytesNeeded = 0;
        if (QueryServiceStatusEx(queryService,
                                 SC_STATUS_PROCESS_INFO,
                                 reinterpret_cast<LPBYTE>(&status),
                                 sizeof(status),
                                 &bytesNeeded) &&
            status.dwCurrentState == SERVICE_RUNNING)
        {
            CloseServiceHandle(queryService);
            CloseServiceHandle(manager);
            return true;
        }
        CloseServiceHandle(queryService);
    }

    SC_HANDLE service = OpenServiceW(manager,
                                     kAiranServiceNameW,
                                     SERVICE_START | SERVICE_QUERY_STATUS);
    if (!service)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED)
            LOG_INFO("Airan service start requires elevation");
        else
            LOG_WARN("Failed to open Airan service for start: {}", error);
        CloseServiceHandle(manager);
        return false;
    }

    bool ok = true;
    if (!StartServiceW(service, 0, nullptr))
    {
        const DWORD error = GetLastError();
        ok = error == ERROR_SERVICE_ALREADY_RUNNING;
        if (!ok)
            LOG_WARN("Failed to start Airan service: {}", error);
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return ok;
}

DWORD queryAiranServiceState()
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return 0;

    SC_HANDLE service = OpenServiceW(manager, kAiranServiceNameW, SERVICE_QUERY_STATUS);
    if (!service)
    {
        CloseServiceHandle(manager);
        return 0;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    const bool ok = QueryServiceStatusEx(service,
                                         SC_STATUS_PROCESS_INFO,
                                         reinterpret_cast<LPBYTE>(&status),
                                         sizeof(status),
                                         &bytesNeeded);
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return ok ? status.dwCurrentState : 0;
}

QString sidToString(PSID sid)
{
    if (!sid)
        return QString();
    LPWSTR sidText = nullptr;
    if (!ConvertSidToStringSidW(sid, &sidText))
        return QString();
    const QString result = QString::fromWCharArray(sidText);
    LocalFree(sidText);
    return result;
}

QString accountNameForSid(PSID sid)
{
    if (!sid)
        return QString();
    DWORD nameChars = 0;
    DWORD domainChars = 0;
    SID_NAME_USE sidType{};
    LookupAccountSidW(nullptr, sid, nullptr, &nameChars, nullptr, &domainChars, &sidType);
    if (nameChars == 0)
        return sidToString(sid);

    std::wstring name(nameChars, L'\0');
    std::wstring domain(domainChars, L'\0');
    if (!LookupAccountSidW(nullptr,
                           sid,
                           name.data(),
                           &nameChars,
                           domain.data(),
                           &domainChars,
                           &sidType))
        return sidToString(sid);
    name.resize(nameChars);
    domain.resize(domainChars);
    const QString account = QString::fromStdWString(name);
    const QString domainName = QString::fromStdWString(domain);
    return domainName.isEmpty() ? account : QStringLiteral("%1\\%2").arg(domainName, account);
}

QString integrityName(DWORD rid)
{
    if (rid >= SECURITY_MANDATORY_SYSTEM_RID)
        return QStringLiteral("System");
    if (rid >= SECURITY_MANDATORY_HIGH_RID)
        return QStringLiteral("High");
    if (rid >= SECURITY_MANDATORY_MEDIUM_RID)
        return QStringLiteral("Medium");
    if (rid >= SECURITY_MANDATORY_LOW_RID)
        return QStringLiteral("Low");
    return QStringLiteral("Untrusted");
}

QString currentProcessTokenSummary()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return QStringLiteral("OpenProcessToken failed: %1").arg(GetLastError());

    QString userName;
    DWORD bytesNeeded = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytesNeeded);
    if (bytesNeeded > 0)
    {
        std::vector<BYTE> buffer(bytesNeeded);
        if (GetTokenInformation(token, TokenUser, buffer.data(), bytesNeeded, &bytesNeeded))
        {
            const auto *tokenUser = reinterpret_cast<const TOKEN_USER *>(buffer.data());
            userName = accountNameForSid(tokenUser->User.Sid);
        }
    }

    QString integrity;
    bytesNeeded = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &bytesNeeded);
    if (bytesNeeded > 0)
    {
        std::vector<BYTE> buffer(bytesNeeded);
        if (GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), bytesNeeded, &bytesNeeded))
        {
            const auto *mandatoryLabel = reinterpret_cast<const TOKEN_MANDATORY_LABEL *>(buffer.data());
            PSID sid = mandatoryLabel->Label.Sid;
            const DWORD subAuthorityCount = *GetSidSubAuthorityCount(sid);
            const DWORD rid = *GetSidSubAuthority(sid, subAuthorityCount - 1);
            integrity = QStringLiteral("%1(%2)").arg(integrityName(rid)).arg(rid);
        }
    }

    CloseHandle(token);
    return QStringLiteral("user=%1, integrity=%2, session=%3")
        .arg(userName.isEmpty() ? QStringLiteral("unknown") : userName,
             integrity.isEmpty() ? QStringLiteral("unknown") : integrity)
        .arg(ProcessIdToSessionId(GetCurrentProcessId(), &bytesNeeded) ? bytesNeeded : 0);
}

QString serviceBrokerTokenFromCommand(const QString &command)
{
    const QString marker = QStringLiteral(" --broker-token ");
    const int markerIndex = command.indexOf(marker, 0, Qt::CaseInsensitive);
    if (markerIndex < 0)
        return QString();
    QString token = command.mid(markerIndex + marker.size()).trimmed();
    if (token.startsWith(QLatin1Char('"')))
    {
        const int endQuote = token.indexOf(QLatin1Char('"'), 1);
        if (endQuote > 1)
            token = token.mid(1, endQuote - 1);
        else
            return QString();
    }
    else
    {
        const int space = token.indexOf(QLatin1Char(' '));
        if (space >= 0)
            token.truncate(space);
    }
    return token.trimmed();
}

QString expectedServiceBinaryPath(const QString &exe = QCoreApplication::applicationFilePath(),
                                  const QString &brokerToken = QString())
{
    const QString nativeExe = QDir::toNativeSeparators(QFileInfo(exe).absoluteFilePath());
    QString command = QStringLiteral("\"%1\" --service --windows-input-broker --unattended").arg(nativeExe);
    if (!brokerToken.isEmpty())
        command += QStringLiteral(" --broker-token \"") + brokerToken + QLatin1Char('"');
    return command;
}

QString expectedServiceCommandPrefix(const QString &exe = QCoreApplication::applicationFilePath())
{
    return expectedServiceBinaryPath(exe);
}

QString normalizedServiceBinaryPath(QString value)
{
    value = QDir::fromNativeSeparators(value.trimmed());
    while (value.contains(QStringLiteral("  ")))
        value.replace(QStringLiteral("  "), QStringLiteral(" "));
    return value.toLower();
}

bool queryAiranServiceBinaryPath(QString *binaryPath, QString *errorMessage = nullptr)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
    {
        if (errorMessage)
            *errorMessage = InputUtil::tr("OpenSCManager failed: %1").arg(GetLastError());
        return false;
    }

    SC_HANDLE service = OpenServiceW(manager, kAiranServiceNameW, SERVICE_QUERY_CONFIG);
    if (!service)
    {
        const DWORD error = GetLastError();
        CloseServiceHandle(manager);
        if (errorMessage)
            *errorMessage = InputUtil::tr("OpenService failed: %1").arg(error);
        return false;
    }

    DWORD bytesNeeded = 0;
    QueryServiceConfigW(service, nullptr, 0, &bytesNeeded);
    if (bytesNeeded == 0)
    {
        const DWORD error = GetLastError();
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        if (errorMessage)
            *errorMessage = InputUtil::tr("QueryServiceConfig failed: %1").arg(error);
        return false;
    }

    std::vector<BYTE> buffer(bytesNeeded);
    auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
    if (!QueryServiceConfigW(service, config, bytesNeeded, &bytesNeeded))
    {
        const DWORD error = GetLastError();
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        if (errorMessage)
            *errorMessage = InputUtil::tr("QueryServiceConfig failed: %1").arg(error);
        return false;
    }

    if (binaryPath)
        *binaryPath = QString::fromWCharArray(config->lpBinaryPathName ? config->lpBinaryPathName : L"");
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return true;
}

QString queryAiranServiceBrokerToken(QString *errorMessage = nullptr)
{
    QString command;
    if (!queryAiranServiceBinaryPath(&command, errorMessage))
        return QString();
    return serviceBrokerTokenFromCommand(command);
}

bool duplicateLocalSystemTokenForSession(DWORD sessionId, HANDLE *sessionToken)
{
    if (!sessionToken)
        return false;
    *sessionToken = nullptr;

    HANDLE processToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY |
                              TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                          &processToken))
    {
        LOG_WARN("Failed to open service process token: {}", GetLastError());
        return false;
    }

    HANDLE duplicatedToken = nullptr;
    const BOOL duplicated = DuplicateTokenEx(processToken,
                                             TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY |
                                                 TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                                             nullptr,
                                             SecurityIdentification,
                                             TokenPrimary,
                                             &duplicatedToken);
    CloseHandle(processToken);
    if (!duplicated)
    {
        LOG_WARN("Failed to duplicate service process token: {}", GetLastError());
        return false;
    }

    if (!SetTokenInformation(duplicatedToken, TokenSessionId, &sessionId, sizeof(sessionId)))
    {
        LOG_WARN("Failed to move service token to session {}: {}", sessionId, GetLastError());
        CloseHandle(duplicatedToken);
        return false;
    }

    *sessionToken = duplicatedToken;
    return true;
}

bool processRunsAsLocalSystem(HANDLE process)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token))
        return false;

    DWORD bytesNeeded = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytesNeeded);
    bool result = false;
    if (bytesNeeded > 0)
    {
        std::vector<BYTE> buffer(bytesNeeded);
        if (GetTokenInformation(token, TokenUser, buffer.data(), bytesNeeded, &bytesNeeded))
        {
            const auto *tokenUser = reinterpret_cast<const TOKEN_USER *>(buffer.data());
            result = IsWellKnownSid(tokenUser->User.Sid, WinLocalSystemSid);
        }
    }
    CloseHandle(token);
    return result;
}

int terminateSystemBrokerProcessesInSession(DWORD sessionId)
{
    int terminated = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        LOG_WARN("Failed to create process snapshot for broker cleanup: {}", GetLastError());
        return 0;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry))
    {
        CloseHandle(snapshot);
        return 0;
    }

    do
    {
        if (_wcsicmp(entry.szExeFile, L"airan-desk.exe") != 0)
            continue;
        DWORD processSessionId = 0;
        if (!ProcessIdToSessionId(entry.th32ProcessID, &processSessionId) || processSessionId != sessionId)
            continue;
        if (entry.th32ProcessID == GetCurrentProcessId())
            continue;

        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                                     FALSE,
                                     entry.th32ProcessID);
        if (!process)
            continue;
        const bool isSystem = processRunsAsLocalSystem(process);
        if (isSystem)
        {
            if (TerminateProcess(process, 0))
            {
                ++terminated;
                LOG_WARN("Terminated stale Windows input broker process: pid={}, session={}",
                         entry.th32ProcessID,
                         sessionId);
            }
            else
            {
                LOG_WARN("Failed to terminate stale Windows input broker process pid={} session={}: {}",
                         entry.th32ProcessID,
                         sessionId,
                         GetLastError());
            }
        }
        CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));

    CloseHandle(snapshot);
    return terminated;
}

bool startBrokerInSession(DWORD sessionId, const QString &brokerToken)
{
    if (sessionId == 0 || sessionId == 0xFFFFFFFF || brokerToken.isEmpty())
        return false;

    HANDLE sessionToken = nullptr;
    if (!duplicateLocalSystemTokenForSession(sessionId, &sessionToken))
        return false;

    void *environment = nullptr;
    const BOOL hasEnvironment = CreateEnvironmentBlock(&environment, sessionToken, FALSE);
    if (!hasEnvironment)
        LOG_WARN("Failed to create service broker environment block: {}", GetLastError());

    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString serverName = unattendedServerNameForSession(sessionId);
    const QString command = QStringLiteral("\"%1\" --windows-input-broker --unattended --server-name \"%2\" --token \"%3\"")
                                .arg(exe, serverName, brokerToken);
    std::wstring commandLine = command.toStdWString();
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    const std::wstring applicationName = exe.toStdWString();
    const std::wstring workingDirectory = QDir::toNativeSeparators(QCoreApplication::applicationDirPath()).toStdWString();

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
    PROCESS_INFORMATION processInfo{};
    const DWORD creationFlags = CREATE_NO_WINDOW | (hasEnvironment ? CREATE_UNICODE_ENVIRONMENT : 0);
    const BOOL created = CreateProcessAsUserW(sessionToken,
                                              applicationName.c_str(),
                                              mutableCommand.data(),
                                              nullptr,
                                              nullptr,
                                              FALSE,
                                              creationFlags,
                                              environment,
                                              workingDirectory.c_str(),
                                              &startupInfo,
                                              &processInfo);
    if (environment)
        DestroyEnvironmentBlock(environment);
    CloseHandle(sessionToken);

    if (!created)
    {
        LOG_WARN("Failed to start Windows input broker in session {}: {}", sessionId, GetLastError());
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    LOG_INFO("Started Windows input broker in session {} on {}", sessionId, serverName);
    return true;
}

std::vector<DWORD> brokerTargetSessionIds()
{
    std::vector<DWORD> result;
    auto addSession = [&result](DWORD sessionId) {
        if (sessionId == 0 || sessionId == 0xFFFFFFFF)
            return;
        if (std::find(result.begin(), result.end(), sessionId) == result.end())
            result.push_back(sessionId);
    };

    addSession(WTSGetActiveConsoleSessionId());

    WTS_SESSION_INFOW *sessions = nullptr;
    DWORD count = 0;
    if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count))
    {
        for (DWORD i = 0; i < count; ++i)
        {
            const WTS_CONNECTSTATE_CLASS state = sessions[i].State;
            if (state == WTSActive || state == WTSConnected || state == WTSDisconnected)
                addSession(sessions[i].SessionId);
        }
        WTSFreeMemory(sessions);
    }
    return result;
}

std::wstring toWide(const QString &value)
{
    return value.toStdWString();
}

bool waitForServiceStopped(SC_HANDLE service, DWORD timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    while (timer.elapsed() < static_cast<qint64>(timeoutMs))
    {
        if (!QueryServiceStatusEx(service,
                                  SC_STATUS_PROCESS_INFO,
                                  reinterpret_cast<LPBYTE>(&status),
                                  sizeof(status),
                                  &bytesNeeded))
            return false;
        if (status.dwCurrentState == SERVICE_STOPPED)
            return true;
        QThread::msleep(100);
    }
    return false;
}

bool stopAiranServiceIfRunning(SC_HANDLE service)
{
    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    if (!QueryServiceStatusEx(service,
                              SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<LPBYTE>(&status),
                              sizeof(status),
                              &bytesNeeded))
        return false;
    if (status.dwCurrentState == SERVICE_STOPPED)
        return true;

    SERVICE_STATUS stopStatus{};
    if (!ControlService(service, SERVICE_CONTROL_STOP, &stopStatus))
    {
        const DWORD error = GetLastError();
        if (error != ERROR_SERVICE_NOT_ACTIVE)
        {
            LOG_WARN("Failed to stop Airan service: {}", error);
            return false;
        }
    }
    return waitForServiceStopped(service, 10000);
}

bool stopAiranServiceDirect(QString *errorMessage = nullptr)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
    {
        if (errorMessage)
            *errorMessage = InputUtil::tr("OpenSCManager failed: %1").arg(GetLastError());
        return false;
    }

    SC_HANDLE service = OpenServiceW(manager,
                                     kAiranServiceNameW,
                                     SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!service)
    {
        const DWORD error = GetLastError();
        CloseServiceHandle(manager);
        if (error == ERROR_SERVICE_DOES_NOT_EXIST)
            return true;
        if (errorMessage)
            *errorMessage = InputUtil::tr("OpenService failed: %1").arg(error);
        return false;
    }

    const bool ok = stopAiranServiceIfRunning(service);
    if (!ok && errorMessage)
        *errorMessage = InputUtil::tr("ControlService failed: %1").arg(GetLastError());
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return ok;
}

qint64 fileTimeToUnixMs(const FILETIME &fileTime)
{
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    constexpr unsigned long long kWindowsToUnixEpoch100Ns = 116444736000000000ULL;
    if (value.QuadPart <= kWindowsToUnixEpoch100Ns)
        return 0;
    return static_cast<qint64>((value.QuadPart - kWindowsToUnixEpoch100Ns) / 10000ULL);
}

qint64 runningAiranServiceProcessCreationMs()
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        return 0;

    SC_HANDLE service = OpenServiceW(manager, kAiranServiceNameW, SERVICE_QUERY_STATUS);
    if (!service)
    {
        CloseServiceHandle(manager);
        return 0;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    if (!QueryServiceStatusEx(service,
                              SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<LPBYTE>(&status),
                              sizeof(status),
                              &bytesNeeded) ||
        status.dwCurrentState != SERVICE_RUNNING ||
        status.dwProcessId == 0)
    {
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return 0;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, status.dwProcessId);
    if (!process)
    {
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return 0;
    }

    FILETIME creationTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    const bool ok = GetProcessTimes(process, &creationTime, &exitTime, &kernelTime, &userTime);
    CloseHandle(process);
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return ok ? fileTimeToUnixMs(creationTime) : 0;
}

bool setAiranServiceDescription(SC_HANDLE service)
{
    std::wstring description = toWide(QString::fromUtf8(kAiranServiceDescription));
    SERVICE_DESCRIPTIONW serviceDescription{};
    serviceDescription.lpDescription = description.data();
    if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &serviceDescription))
    {
        LOG_WARN("Failed to set Airan service description: {}", GetLastError());
        return false;
    }
    return true;
}

bool setAiranServiceFailureRecovery(SC_HANDLE service, QString *errorMessage)
{
    SC_ACTION actions[] = {
        {SC_ACTION_RESTART, 5 * 1000},
        {SC_ACTION_RESTART, 15 * 1000},
        {SC_ACTION_RESTART, 60 * 1000},
    };
    SERVICE_FAILURE_ACTIONSW failureActions{};
    failureActions.dwResetPeriod = 24 * 60 * 60;
    failureActions.cActions = static_cast<DWORD>(sizeof(actions) / sizeof(actions[0]));
    failureActions.lpsaActions = actions;
    if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failureActions))
    {
        const DWORD error = GetLastError();
        if (errorMessage)
            *errorMessage = InputUtil::tr("ChangeServiceConfig failed: %1").arg(error);
        return false;
    }

    SERVICE_FAILURE_ACTIONS_FLAG failureActionsFlag{};
    failureActionsFlag.fFailureActionsOnNonCrashFailures = TRUE;
    if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &failureActionsFlag))
    {
        const DWORD error = GetLastError();
        if (errorMessage)
            *errorMessage = InputUtil::tr("ChangeServiceConfig failed: %1").arg(error);
        return false;
    }
    return true;
}

bool installAiranServiceDirect(const QString &exe, QString *errorMessage)
{
    const QString brokerToken = uuidWithoutBraces();
    const QString binPath = expectedServiceBinaryPath(exe, brokerToken);
    const std::wstring binPathW = toWide(binPath);
    const std::wstring displayNameW = toWide(QString::fromUtf8(kAiranServiceDisplayName));

    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE | SC_MANAGER_CONNECT);
    if (!manager)
    {
        if (errorMessage)
            *errorMessage = InputUtil::tr("OpenSCManager failed: %1").arg(GetLastError());
        return false;
    }

    SC_HANDLE service = OpenServiceW(manager,
                                     kAiranServiceNameW,
                                     SERVICE_CHANGE_CONFIG | SERVICE_START | SERVICE_STOP |
                                         SERVICE_QUERY_STATUS | DELETE);
    if (service)
    {
        stopAiranServiceIfRunning(service);
        if (!ChangeServiceConfigW(service,
                                  SERVICE_WIN32_OWN_PROCESS,
                                  SERVICE_AUTO_START,
                                  SERVICE_ERROR_NORMAL,
                                  binPathW.c_str(),
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  displayNameW.c_str()))
        {
            if (errorMessage)
                *errorMessage = InputUtil::tr("ChangeServiceConfig failed: %1").arg(GetLastError());
            CloseServiceHandle(service);
            CloseServiceHandle(manager);
            return false;
        }
    }
    else
    {
        service = CreateServiceW(manager,
                                 kAiranServiceNameW,
                                 displayNameW.c_str(),
                                 SERVICE_CHANGE_CONFIG | SERVICE_START | SERVICE_STOP |
                                     SERVICE_QUERY_STATUS | DELETE,
                                 SERVICE_WIN32_OWN_PROCESS,
                                 SERVICE_AUTO_START,
                                 SERVICE_ERROR_NORMAL,
                                 binPathW.c_str(),
                                 nullptr,
                                 nullptr,
                                 nullptr,
                                 nullptr,
                                 nullptr);
        if (!service)
        {
            if (errorMessage)
                *errorMessage = InputUtil::tr("CreateService failed: %1").arg(GetLastError());
            CloseServiceHandle(manager);
            return false;
        }
    }

    setAiranServiceDescription(service);
    if (!setAiranServiceFailureRecovery(service, errorMessage))
    {
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return false;
    }
    if (!StartServiceW(service, 0, nullptr))
    {
        const DWORD error = GetLastError();
        if (error != ERROR_SERVICE_ALREADY_RUNNING)
        {
            if (errorMessage)
                *errorMessage = InputUtil::tr("StartService failed: %1").arg(error);
            CloseServiceHandle(service);
            CloseServiceHandle(manager);
            return false;
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return true;
}

bool uninstallAiranServiceDirect(QString *errorMessage)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
    {
        if (errorMessage)
            *errorMessage = InputUtil::tr("OpenSCManager failed: %1").arg(GetLastError());
        return false;
    }

    SC_HANDLE service = OpenServiceW(manager,
                                     kAiranServiceNameW,
                                     SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (!service)
    {
        const DWORD error = GetLastError();
        CloseServiceHandle(manager);
        if (error == ERROR_SERVICE_DOES_NOT_EXIST)
            return true;
        if (errorMessage)
            *errorMessage = InputUtil::tr("OpenService failed: %1").arg(error);
        return false;
    }

    stopAiranServiceIfRunning(service);
    const BOOL deleted = DeleteService(service);
    const DWORD deleteError = GetLastError();
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    if (!deleted && deleteError != ERROR_SERVICE_MARKED_FOR_DELETE)
    {
        if (errorMessage)
            *errorMessage = InputUtil::tr("DeleteService failed: %1").arg(deleteError);
        return false;
    }
    return true;
}

bool runElevatedSelfAndWait(const QString &argument, QString *errorMessage)
{
    const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QString error;
    if (runElevatedProcessAndWait(exePath, argument, 30000, &error))
        return true;
    if (errorMessage)
        *errorMessage = withElevatedSelfFailureImpact(error, argument);
    return false;
}

struct MonitorSelection
{
    int targetIndex = 0;
    int currentIndex = 0;
    RECT rect{};
    bool found = false;
};

QString normalizedDisplayName(const QString &name)
{
    QString normalized = name.trimmed().toUpper();
    if (normalized.startsWith(QStringLiteral("\\\\.\\")))
        normalized.remove(0, 4);
    return normalized;
}

QString normalizedDisplayName(const std::string &name)
{
    return normalizedDisplayName(QString::fromStdString(name));
}

std::optional<QRect> desktopRectToQRect(const airan::desktop_capture::DesktopRect &rect)
{
    if (rect.is_empty())
        return std::nullopt;
    return QRect(rect.left(),
                 rect.top(),
                 qMax(1, rect.width()),
                 qMax(1, rect.height()));
}

bool hasInvalidWindowsSourceRect(const airan::desktop_capture::DesktopCapturer::SourceList &sources)
{
    if (sources.empty())
        return true;
    for (const auto &source : sources)
    {
        if (airan::desktop_capture::GetScreenRect(source.id, std::nullopt).is_empty())
            return true;
    }
    return false;
}

bool buildExpectedWindowsSourceList(airan::desktop_capture::DesktopCapturer::SourceList *sources,
                                    std::vector<std::string> *deviceNames)
{
    if (!sources || !deviceNames)
        return false;
    sources->clear();
    deviceNames->clear();

    airan::desktop_capture::DesktopCapturer::SourceList deviceSources;
    std::vector<std::string> deviceSourceNames;
    const bool deviceResult = airan::desktop_capture::GetScreenList(&deviceSources, &deviceSourceNames);
    airan::desktop_capture::DesktopCapturer::SourceList monitorSources;
    const bool monitorResult = airan::desktop_capture::GetAiranMonitorList(&monitorSources);
    const bool deviceRectInvalid = hasInvalidWindowsSourceRect(deviceSources);
    if (monitorResult &&
        (monitorSources.size() > deviceSources.size() ||
         !deviceResult || deviceSources.empty() || deviceRectInvalid))
    {
        *sources = monitorSources;
        return true;
    }

    *sources = deviceSources;
    *deviceNames = deviceSourceNames;
    return deviceResult;
}

BOOL CALLBACK enumerateMonitorRectByIndex(HMONITOR, HDC, LPRECT rect, LPARAM param)
{
    auto *selection = reinterpret_cast<MonitorSelection *>(param);
    if (!selection || !rect)
        return FALSE;
    if (selection->currentIndex == selection->targetIndex)
    {
        selection->rect = *rect;
        selection->found = true;
        return FALSE;
    }
    ++selection->currentIndex;
    return TRUE;
}

std::optional<QRect> physicalMonitorRectForScreen(QScreen *screen, int screenIndex)
{
    airan::desktop_capture::DesktopCapturer::SourceList sources;
    std::vector<std::string> deviceNames;
    if (screen && buildExpectedWindowsSourceList(&sources, &deviceNames))
    {
        const QString qtDisplayName = normalizedDisplayName(screen->name());
        for (int i = 0; i < static_cast<int>(deviceNames.size()) && i < static_cast<int>(sources.size()); ++i)
        {
            if (!qtDisplayName.isEmpty() && qtDisplayName == normalizedDisplayName(deviceNames[static_cast<size_t>(i)]))
            {
                const auto rect = desktopRectToQRect(
                    airan::desktop_capture::GetScreenRect(sources[static_cast<size_t>(i)].id, std::nullopt));
                if (rect.has_value())
                {
                    LOG_TRACE("Resolved mouse target screen by Windows display name: index={}, name={}, rect={}x{}+{}+{}",
                              screenIndex,
                              screen->name(),
                              rect->width(),
                              rect->height(),
                              rect->x(),
                              rect->y());
                    return rect;
                }
            }
        }

        if (screenIndex >= 0 && screenIndex < static_cast<int>(sources.size()))
        {
            const auto rect = desktopRectToQRect(
                airan::desktop_capture::GetScreenRect(sources[static_cast<size_t>(screenIndex)].id, std::nullopt));
            if (rect.has_value())
            {
                LOG_TRACE("Resolved mouse target screen by Windows display index: index={}, rect={}x{}+{}+{}",
                          screenIndex,
                          rect->width(),
                          rect->height(),
                          rect->x(),
                          rect->y());
                return rect;
            }
        }
    }

    airan::desktop_capture::DesktopCapturer::SourceList monitorSources;
    if (screenIndex >= 0 &&
        airan::desktop_capture::GetAiranMonitorList(&monitorSources) &&
        screenIndex < static_cast<int>(monitorSources.size()))
    {
        const auto rect = desktopRectToQRect(
            airan::desktop_capture::GetScreenRect(monitorSources[static_cast<size_t>(screenIndex)].id, std::nullopt));
        if (rect.has_value())
        {
            LOG_TRACE("Resolved mouse target screen by Windows monitor index: index={}, rect={}x{}+{}+{}",
                      screenIndex,
                      rect->width(),
                      rect->height(),
                      rect->x(),
                      rect->y());
            return rect;
        }
    }

    return std::nullopt;
}

std::optional<QRect> physicalMonitorRectForDesktopSource(int desktopSourceIndex)
{
    if (desktopSourceIndex < 0)
        return std::nullopt;

    airan::desktop_capture::DesktopCapturer::SourceList sources;
    std::vector<std::string> deviceNames;
    if (buildExpectedWindowsSourceList(&sources, &deviceNames) &&
        desktopSourceIndex < static_cast<int>(sources.size()))
    {
        const auto rect = desktopRectToQRect(
            airan::desktop_capture::GetScreenRect(sources[static_cast<size_t>(desktopSourceIndex)].id, std::nullopt));
        if (rect.has_value())
        {
            LOG_TRACE("Resolved mouse target screen by Windows desktop source: sourceIndex={}, sourceId={}, rect={}x{}+{}+{}",
                      desktopSourceIndex,
                      static_cast<intptr_t>(sources[static_cast<size_t>(desktopSourceIndex)].id),
                      rect->width(),
                      rect->height(),
                      rect->x(),
                      rect->y());
            return rect;
        }
    }

    return std::nullopt;
}

LONG normalizeAbsoluteCoordinate(int coord, int max)
{
    if (max <= 1)
        return 0;
    double value = (coord * 65535.0) / (static_cast<double>(max) - 1.0);
    value = qBound(0.0, value, 65535.0);
    return static_cast<LONG>(std::round(value));
}

DWORD desktopAccessMask()
{
    return DESKTOP_CREATEMENU |
           DESKTOP_CREATEWINDOW |
           DESKTOP_ENUMERATE |
           DESKTOP_HOOKCONTROL |
           DESKTOP_JOURNALPLAYBACK |
           DESKTOP_JOURNALRECORD |
           DESKTOP_READOBJECTS |
           DESKTOP_SWITCHDESKTOP |
           DESKTOP_WRITEOBJECTS;
}

QString desktopName(HDESK desktop)
{
    if (!desktop)
        return QString();

    DWORD bytesNeeded = 0;
    GetUserObjectInformationW(desktop, UOI_NAME, nullptr, 0, &bytesNeeded);
    if (bytesNeeded == 0)
        return QString();

    std::vector<wchar_t> buffer((bytesNeeded / sizeof(wchar_t)) + 1, L'\0');
    if (!GetUserObjectInformationW(desktop, UOI_NAME, buffer.data(), bytesNeeded, &bytesNeeded))
        return QString();
    return QString::fromWCharArray(buffer.data());
}

QString currentInputDesktopName()
{
    HDESK inputDesktop = OpenInputDesktop(0, FALSE, desktopAccessMask());
    if (!inputDesktop)
        return QStringLiteral("OpenInputDesktop failed:%1").arg(GetLastError());
    const QString name = desktopName(inputDesktop);
    CloseDesktop(inputDesktop);
    return name;
}

bool isSecureInputDesktopName(const QString &name)
{
    return name.compare(QStringLiteral("Winlogon"), Qt::CaseInsensitive) == 0;
}

bool isLetterVirtualKey(int keyCode)
{
    return keyCode >= 'A' && keyCode <= 'Z';
}

QString keyboardModifierStateSummary()
{
    auto stateFor = [](int vk) {
        const SHORT asyncState = GetAsyncKeyState(vk);
        const SHORT keyState = GetKeyState(vk);
        return QStringLiteral("async=0x%1,state=0x%2")
            .arg(static_cast<quint16>(asyncState), 4, 16, QLatin1Char('0'))
            .arg(static_cast<quint16>(keyState), 4, 16, QLatin1Char('0'));
    };

    return QStringLiteral("shift{%1},lshift{%2},rshift{%3},ctrl{%4},alt{%5},caps{%6}")
        .arg(stateFor(VK_SHIFT),
             stateFor(VK_LSHIFT),
             stateFor(VK_RSHIFT),
             stateFor(VK_CONTROL),
             stateFor(VK_MENU),
             stateFor(VK_CAPITAL));
}

template <typename Operation>
bool runOnFreshInputDesktopThread(const char *label, Operation operation, QString *errorMessage = nullptr)
{
    bool ok = false;
    QString error;
    std::thread worker([&]() {
        HDESK originalDesktop = GetThreadDesktop(GetCurrentThreadId());
        HDESK inputDesktop = OpenInputDesktop(0, FALSE, desktopAccessMask());
        bool desktopChanged = false;
        const QString originalName = desktopName(originalDesktop);
        QString inputName;
        if (inputDesktop)
        {
            inputName = desktopName(inputDesktop);
            if (SetThreadDesktop(inputDesktop))
            {
                desktopChanged = true;
            }
            else
            {
                const DWORD switchError = GetLastError();
                error = QCoreApplication::translate("InputUtil", "SetThreadDesktop failed: %1, original=%2, input=%3")
                            .arg(switchError)
                            .arg(originalName, inputName);
                if (!g_inputDesktopSwitchFailureLogged.exchange(true))
                    LOG_WARN("{} could not switch to input desktop: {}", label ? label : "Input", error);
            }
        }
        else
        {
            error = QCoreApplication::translate("InputUtil", "OpenInputDesktop failed: %1, original=%2")
                        .arg(GetLastError())
                        .arg(originalName);
        }

        QString operationError;
        ok = operation(&operationError);
        if (!ok)
        {
            if (!operationError.isEmpty())
                error = operationError;
            if (!inputName.isEmpty())
                error += QStringLiteral(", inputDesktop=%1").arg(inputName);
        }

        if (desktopChanged && originalDesktop)
            SetThreadDesktop(originalDesktop);
        if (inputDesktop)
            CloseDesktop(inputDesktop);
    });
    worker.join();

    if (!ok && errorMessage)
        *errorMessage = error;
    return ok;
}

INPUT makeMouseMoveInput(LONG absX, LONG absY)
{
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    input.mi.dx = absX;
    input.mi.dy = absY;
    return input;
}

INPUT makeMouseButtonInput(DWORD flag)
{
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;
    return input;
}

INPUT makeMouseWheelInput(int mouseData)
{
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = mouseData;
    return input;
}

bool sendInputs(INPUT *inputs, UINT count, const char *label, QString *errorMessage = nullptr)
{
    if (!inputs || count == 0)
        return true;

    QString error;
    const bool ok = runOnFreshInputDesktopThread(label ? label : "input", [&](QString *operationError) {
        const UINT sent = SendInput(count, inputs, sizeof(INPUT));
        if (sent != count)
        {
            if (operationError)
            {
                *operationError = QStringLiteral("SendInput failed: sent=%1, expected=%2, lastError=%3")
                                      .arg(sent)
                                      .arg(count)
                                      .arg(GetLastError());
            }
            return false;
        }
        return true;
    },
                                                 &error);
    if (!ok)
    {
        if (errorMessage)
            *errorMessage = error;
        LOG_WARN("SendInput failed for {}: {}", label ? label : "input", error);
        return false;
    }
    return true;
}

bool sendSecureLetterKeybdEvent(int keyCode, const QString &dwFlags, QString *errorMessage)
{
    const DWORD flags = dwFlags == QStringLiteral("down") ? 0 : KEYEVENTF_KEYUP;
    return runOnFreshInputDesktopThread("secure keyboard letter", [=](QString *) {
        const BYTE scanCode = static_cast<BYTE>(MapVirtualKeyW(static_cast<UINT>(keyCode), MAPVK_VK_TO_VSC));
        keybd_event(static_cast<BYTE>(keyCode), scanCode, flags, 0);
        return true;
    },
                                        errorMessage);
}

void moveCursorToPixel(int x, int y)
{
    QString error;
    const bool ok = runOnFreshInputDesktopThread("cursor positioning", [&](QString *operationError) {
        if (SetCursorPos(x, y))
            return true;
        if (operationError)
        {
            *operationError = QStringLiteral("SetCursorPos failed: x=%1, y=%2, lastError=%3")
                                  .arg(x)
                                  .arg(y)
                                  .arg(GetLastError());
        }
        return false;
    },
                                                 &error);
    if (!ok && !g_cursorPositionFailureLogged.exchange(true))
        LOG_WARN("SetCursorPos failed for input injection: {}", error);
}

bool connectToServer(QLocalSocket *socket,
                     const QString &serverName,
                     int timeoutMs,
                     QIODevice::OpenMode openMode = QIODevice::WriteOnly)
{
    if (!socket)
        return false;
    socket->abort();
    socket->connectToServer(serverName, openMode);
    return socket->waitForConnected(timeoutMs);
}

bool isAuthorizedBrokerClient(QLocalSocket *socket)
{
    if (!socket)
        return false;
    ULONG clientProcessId = 0;
    const HANDLE pipe = reinterpret_cast<HANDLE>(socket->socketDescriptor());
    if (pipe == INVALID_HANDLE_VALUE || !GetNamedPipeClientProcessId(pipe, &clientProcessId) || clientProcessId == 0)
    {
        LOG_WARN("Windows input broker could not identify the pipe client: {}", GetLastError());
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, clientProcessId);
    if (!process)
    {
        LOG_WARN("Windows input broker could not inspect client pid {}: {}", clientProcessId, GetLastError());
        return false;
    }

    std::vector<wchar_t> path(32768, L'\0');
    DWORD pathChars = static_cast<DWORD>(path.size());
    const bool queried = QueryFullProcessImageNameW(process, 0, path.data(), &pathChars);
    CloseHandle(process);
    if (!queried || pathChars == 0)
    {
        LOG_WARN("Windows input broker could not read client pid {} image path: {}", clientProcessId, GetLastError());
        return false;
    }

    const QString clientPath = QDir::cleanPath(QString::fromWCharArray(path.data(), static_cast<int>(pathChars)));
    const QString expectedPath = QDir::cleanPath(QCoreApplication::applicationFilePath());
    if (clientPath.compare(expectedPath, Qt::CaseInsensitive) != 0)
    {
        LOG_WARN("Windows input broker rejected client pid {} from unexpected image path", clientProcessId);
        return false;
    }
    return true;
}

bool connectToBroker(QLocalSocket *socket, int timeoutMs)
{
    const BrokerEndpoint endpoint = currentBrokerEndpoint();
    if (endpoint.serverName.isEmpty())
        return false;
    return connectToServer(socket, endpoint.serverName, timeoutMs);
}

bool sendJsonCommandAndReadAck(const QString &serverName,
                               const QJsonObject &message,
                               int responseTimeoutMs,
                               QJsonObject *ackObject = nullptr,
                               QString *errorMessage = nullptr)
{
    QLocalSocket socket;
    if (!connectToServer(&socket, serverName, kBrokerConnectTimeoutMs, QIODevice::ReadWrite))
    {
        if (errorMessage)
            *errorMessage = socket.errorString();
        return false;
    }

    const QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (socket.write(data) != data.size() || !socket.waitForBytesWritten(kBrokerWriteTimeoutMs))
    {
        if (errorMessage)
            *errorMessage = socket.errorString();
        socket.disconnectFromServer();
        return false;
    }

    QByteArray line;
    if (!readBoundedJsonLine(&socket, &line, 4096, responseTimeoutMs))
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("InputUtil", "Response is missing, timed out, or too large");
        socket.disconnectFromServer();
        return false;
    }
    socket.disconnectFromServer();

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        if (errorMessage)
            *errorMessage = error.errorString();
        return false;
    }

    const QJsonObject ack = document.object();
    if (ackObject)
        *ackObject = ack;
    if (!ack.value(QStringLiteral("ok")).toBool(false))
    {
        if (errorMessage)
            *errorMessage = ack.value(QStringLiteral("error")).toString();
        return false;
    }
    return true;
}

bool probeBrokerProtocol(const QString &serverName,
                         bool logFailure = true,
                         int responseTimeoutMs = 2000,
                         const QString &token = QString())
{
    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("ping"));
    message.insert(QStringLiteral("ack"), true);
    if (!token.isEmpty())
        message.insert(QStringLiteral("token"), token);

    QJsonObject ack;
    QString error;
    if (!sendJsonCommandAndReadAck(serverName, message, responseTimeoutMs, &ack, &error))
    {
        if (logFailure)
            LOG_WARN("Windows input broker protocol probe failed on {}: {}", serverName, error);
        return false;
    }
    if (ack.value(QStringLiteral("type")).toString() != QStringLiteral("pong"))
    {
        LOG_WARN("Windows input broker protocol probe returned unexpected response on {}", serverName);
        return false;
    }
    return true;
}

class InputBrokerClient
{
    struct PendingAck;

public:
    ~InputBrokerClient()
    {
        stop();
    }

    bool isActiveFor(const BrokerEndpoint &endpoint)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_running &&
               !endpoint.serverName.isEmpty() &&
               endpoint.serverName == m_serverName &&
               endpoint.token == m_token &&
               endpoint.kind == m_kind;
    }

    bool start(const BrokerEndpoint &endpoint)
    {
        if (endpoint.serverName.isEmpty())
            return false;

        stopIfEndpointChanged(endpoint);

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_running)
            return true;

        m_serverName = endpoint.serverName;
        m_token = endpoint.token;
        m_kind = endpoint.kind;
        m_stop = false;
        m_running = true;
        m_reconnectRequested.store(false);
        m_activityGeneration = 0;
        m_worker = std::thread(&InputBrokerClient::run, this, m_serverName);
        m_heartbeatWorker = std::thread(&InputBrokerClient::runHeartbeat, this, endpoint);
        return true;
    }

    bool enqueue(const QByteArray &data, bool isMove, bool isBoundary)
    {
        return enqueueInternal(data, isMove, isBoundary, isMove && isBoundary, nullptr);
    }

    bool enqueueReliable(const QByteArray &data)
    {
        return enqueueInternal(data, false, true, false, nullptr);
    }

    bool enqueueAndWait(const QByteArray &data, int timeoutMs)
    {
        auto ack = std::make_shared<PendingAck>();
        if (!enqueueInternal(data, false, true, false, ack))
            return false;
        std::unique_lock<std::mutex> lock(ack->mutex);
        const bool completed = ack->cv.wait_for(lock,
                                                std::chrono::milliseconds(timeoutMs),
                                                [&ack]() { return ack->done; });
        if (!completed)
            ack->cancelled = true;
        return completed && ack->ok;
    }

    void stop()
    {
        std::thread worker;
        std::thread heartbeatWorker;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running && !m_worker.joinable() && !m_heartbeatWorker.joinable())
                return;
            m_stop = true;
            m_cv.notify_all();
            worker = std::move(m_worker);
            heartbeatWorker = std::move(m_heartbeatWorker);
            m_running = false;
            for (const PendingMessage &message : m_queue)
                completeAck(message.ack, false);
            m_queue.clear();
        }
        if (worker.joinable())
            worker.join();
        if (heartbeatWorker.joinable())
            heartbeatWorker.join();
    }

private:
    struct PendingAck
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        bool ok = false;
        bool cancelled = false;
    };

    struct PendingMessage
    {
        QByteArray data;
        bool isMove = false;
        bool isBoundary = false;
        bool retryUntilSent = false;
        std::shared_ptr<PendingAck> ack;
    };

    static void completeAck(const std::shared_ptr<PendingAck> &ack, bool ok)
    {
        if (!ack)
            return;
        {
            std::lock_guard<std::mutex> lock(ack->mutex);
            ack->ok = ok;
            ack->done = true;
        }
        ack->cv.notify_all();
    }

    bool enqueueInternal(const QByteArray &data,
                         bool isMove,
                         bool isBoundary,
                         bool retryUntilSent,
                         const std::shared_ptr<PendingAck> &ack)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running || m_stop)
            return false;

        ++m_activityGeneration;
        if (isMove && !isBoundary && !m_queue.empty() &&
            m_queue.back().isMove && !m_queue.back().isBoundary)
        {
            m_queue.back().data = data;
            m_cv.notify_all();
            return true;
        }

        constexpr size_t kMaxQueueSize = 512;
        constexpr size_t kBoundaryReserve = 4;
        if (!isBoundary && m_queue.size() >= kMaxQueueSize - kBoundaryReserve)
            return true;
        if (m_queue.size() >= kMaxQueueSize)
        {
            auto it = std::find_if(m_queue.begin(), m_queue.end(), [](const PendingMessage &message) {
                return message.isMove && !message.isBoundary;
            });
            if (it != m_queue.end())
                m_queue.erase(it);
            else if (isMove && !isBoundary)
                return true;
            else
                return false;
        }

        m_queue.push_back({data, isMove, isBoundary, retryUntilSent, ack});
        m_cv.notify_all();
        return true;
    }

    void stopIfEndpointChanged(const BrokerEndpoint &endpoint)
    {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            changed = m_running &&
                      (endpoint.serverName != m_serverName ||
                       endpoint.token != m_token ||
                       endpoint.kind != m_kind);
        }
        if (changed)
            stop();
    }

    bool takeNext(PendingMessage *message)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return m_stop || !m_queue.empty(); });
        if (m_stop)
            return false;

        *message = m_queue.front();
        m_queue.pop_front();
        while (message->isMove && !message->isBoundary && !m_queue.empty() &&
               m_queue.front().isMove && !m_queue.front().isBoundary)
        {
            *message = m_queue.front();
            m_queue.pop_front();
        }
        return true;
    }

    void run(QString serverName)
    {
        QLocalSocket socket;
        while (true)
        {
            PendingMessage message;
            if (!takeNext(&message))
                break;

            if (message.ack)
            {
                std::lock_guard<std::mutex> ackLock(message.ack->mutex);
                if (message.ack->cancelled)
                    continue;
            }

            if (m_reconnectRequested.exchange(false))
                socket.abort();

            bool sent = false;
            int boundaryRetryDelayMs = 50;
            while (!sent)
            {
                for (int attempt = 0; attempt < 2 && !sent; ++attempt)
                {
                    if (socket.state() != QLocalSocket::ConnectedState)
                    {
                        socket.abort();
                        socket.connectToServer(serverName, QIODevice::ReadWrite);
                        if (!socket.waitForConnected(kBrokerConnectTimeoutMs))
                        {
                            if (attempt == 1)
                                LOG_WARN("Failed to connect to Windows input broker worker socket: {}", socket.errorString());
                            continue;
                        }
                    }

                    const qint64 queuedBytes = socket.write(message.data);
                    const bool fullyQueued = queuedBytes == message.data.size();
                    sent = fullyQueued && socket.waitForBytesWritten(kBrokerWriteTimeoutMs);
                    if (!sent)
                    {
                        if (attempt == 1)
                            LOG_WARN("Failed to write to Windows input broker worker socket: {}", socket.errorString());
                        socket.abort();
                        if (message.ack && fullyQueued)
                            break;
                    }
                }

                if (sent || !message.retryUntilSent || message.ack)
                    break;

                std::unique_lock<std::mutex> retryLock(m_mutex);
                if (m_stop)
                    break;
                m_cv.wait_for(retryLock,
                              std::chrono::milliseconds(boundaryRetryDelayMs),
                              [this]() { return m_stop; });
                boundaryRetryDelayMs = std::min(boundaryRetryDelayMs * 2, 2000);
            }

            if (message.ack)
            {
                bool acknowledged = false;
                if (sent)
                {
                    QByteArray line;
                    if (readBoundedJsonLine(&socket, &line, 4096, 500))
                    {
                        QJsonParseError error{};
                        const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &error);
                        acknowledged = error.error == QJsonParseError::NoError &&
                                       document.isObject() &&
                                       document.object().value(QStringLiteral("ok")).toBool(false);
                    }
                    if (!acknowledged)
                        socket.abort();
                }
                completeAck(message.ack, acknowledged);
            }
        }
        socket.disconnectFromServer();
        if (socket.state() != QLocalSocket::UnconnectedState)
            socket.waitForDisconnected(50);
    }

    void runHeartbeat(BrokerEndpoint endpoint)
    {
        int consecutiveHeartbeatFailures = 0;
        while (true)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            const std::uint64_t activityGeneration = m_activityGeneration;
            const bool interrupted = m_cv.wait_for(
                lock,
                std::chrono::milliseconds(kBrokerHeartbeatIntervalMs),
                [this, activityGeneration]() {
                    return m_stop || m_activityGeneration != activityGeneration;
                });
            if (m_stop)
                return;
            if (interrupted)
                continue;
            lock.unlock();

            const bool healthy = probeBrokerProtocol(endpoint.serverName,
                                                     false,
                                                     kBrokerHeartbeatTimeoutMs,
                                                     endpoint.token);
            if (healthy)
            {
                consecutiveHeartbeatFailures = 0;
                continue;
            }

            ++consecutiveHeartbeatFailures;
            LOG_WARN("Windows input broker idle heartbeat failed ({}/3)", consecutiveHeartbeatFailures);
            if (consecutiveHeartbeatFailures >= 3)
            {
                m_reconnectRequested.store(true);
                consecutiveHeartbeatFailures = 0;
            }
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<PendingMessage> m_queue;
    std::thread m_worker;
    std::thread m_heartbeatWorker;
    QString m_serverName;
    QString m_token;
    BrokerKind m_kind = BrokerKind::None;
    std::atomic_bool m_reconnectRequested{false};
    std::uint64_t m_activityGeneration = 0;
    bool m_running = false;
    bool m_stop = false;
};

InputBrokerClient &inputBrokerClient()
{
    static InputBrokerClient client;
    return client;
}

void useUnattendedBroker(const QString &serverName, const QString &token)
{
    std::lock_guard<std::mutex> lock(g_brokerStateMutex);
    BrokerState &state = brokerState();
    state.serverName = serverName;
    state.token = token;
    state.lastStartAttemptMs = 0;
    state.startAttempted = false;
    state.kind = BrokerKind::Unattended;
}

void clearUnattendedBrokerState()
{
    std::lock_guard<std::mutex> lock(g_brokerStateMutex);
    BrokerState &state = brokerState();
    if (state.kind != BrokerKind::Unattended)
        return;
    state.serverName.clear();
    state.token.clear();
    state.lastStartAttemptMs = 0;
    state.startAttempted = false;
    state.kind = BrokerKind::None;
}

bool tryStartUnattendedInputService()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = g_lastServiceStartAttemptMs.load();
    if (last > 0 && now - last < kServiceStartRetryThrottleMs)
        return false;
    g_lastServiceStartAttemptMs.store(now);
    return startAiranService();
}

bool ensureUnattendedBrokerReady()
{
    const QString serverName = unattendedServerName();
    const QString brokerToken = queryAiranServiceBrokerToken();
    if (brokerToken.isEmpty())
        return false;
    if (probeBrokerProtocol(serverName, false, 2000, brokerToken))
    {
        useUnattendedBroker(serverName, brokerToken);
        return true;
    }

    if (!InputUtil::isWindowsUnattendedInputInstalled())
        return false;

    tryStartUnattendedInputService();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000)
    {
        if (windowsReadinessCancelled())
            return false;
        if (probeBrokerProtocol(serverName, false, 2000, brokerToken))
        {
            useUnattendedBroker(serverName, brokerToken);
            return true;
        }
        QThread::msleep(100);
    }
    return false;
}

bool waitForUnattendedBrokerProtocol(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        if (windowsReadinessCancelled())
            return false;
        const QString serverName = unattendedServerName();
        const QString brokerToken = queryAiranServiceBrokerToken();
        if (!brokerToken.isEmpty() && probeBrokerProtocol(serverName, false, 2000, brokerToken))
        {
            useUnattendedBroker(serverName, brokerToken);
            return true;
        }
        QThread::msleep(100);
    }
    return false;
}

bool ensureBrokerReady()
{
    if (ensureUnattendedBrokerReady())
        return true;

    clearUnattendedBrokerState();
    LOG_WARN("Windows unattended input broker is not available; using direct input fallback when permitted");
    return false;
}

bool directInputFallbackAllowed()
{
    if (g_windowsInputBrokerMode)
        return true;
    if (g_windowsSessionLocked.load())
        return false;
    return !isAiranServiceInstalled();
}

bool sendToBrokerAck(const QJsonObject &payload)
{
    if (g_windowsInputBrokerMode)
        return false;

    if (!ensureBrokerReady())
        return false;

    BrokerEndpoint endpoint = currentBrokerEndpoint();
    if (endpoint.serverName.isEmpty())
        return false;

    if (!inputBrokerClient().isActiveFor(endpoint) && !inputBrokerClient().start(endpoint))
        return false;

    QJsonObject message = payload;
    if (!endpoint.token.isEmpty())
        message.insert(QStringLiteral("token"), endpoint.token);
    message.insert(QStringLiteral("ack"), true);

    const QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    const bool ok = inputBrokerClient().enqueueAndWait(data, 1000);
    if (!ok)
        LOG_WARN("Windows input broker command failed: type={}",
                 message.value(QStringLiteral("type")).toString());
    return ok;
}

bool sendToBroker(const QJsonObject &payload)
{
    if (g_windowsInputBrokerMode)
        return false;

    BrokerEndpoint endpoint = currentBrokerEndpoint();
    if (!inputBrokerClient().isActiveFor(endpoint))
    {
        if (endpoint.serverName.isEmpty() || endpoint.token.isEmpty())
            return false;
        if (!inputBrokerClient().start(endpoint))
            return false;
    }

    QJsonObject message = payload;
    if (!endpoint.token.isEmpty())
        message.insert(QStringLiteral("token"), endpoint.token);
    const QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    const bool isMove = message.value(QStringLiteral("type")).toString() == QStringLiteral("mousePixel") &&
                        message.value(QStringLiteral("dwFlags")).toString() == QStringLiteral("move");
    const bool isBoundary = isMove && message.value(QStringLiteral("moveBoundary")).toBool(false);
    return inputBrokerClient().enqueue(data, isMove, isBoundary);
}

bool sendToBrokerReliable(const QJsonObject &payload)
{
    if (g_windowsInputBrokerMode)
        return false;

    BrokerEndpoint endpoint = currentBrokerEndpoint();
    if (!inputBrokerClient().isActiveFor(endpoint))
    {
        if (endpoint.serverName.isEmpty() || endpoint.token.isEmpty())
            return false;
        if (!inputBrokerClient().start(endpoint))
            return false;
    }

    QJsonObject message = payload;
    if (!endpoint.token.isEmpty())
        message.insert(QStringLiteral("token"), endpoint.token);
    const QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    return inputBrokerClient().enqueueReliable(data);
}

QJsonObject mousePixelMessage(int button,
                              int x,
                              int y,
                              int mouseData,
                              const QString &dwFlags,
                              bool reliableMoveBoundary = false)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("mousePixel"));
    object.insert(QStringLiteral("button"), button);
    object.insert(QStringLiteral("x"), x);
    object.insert(QStringLiteral("y"), y);
    object.insert(QStringLiteral("mouseData"), mouseData);
    object.insert(QStringLiteral("dwFlags"), dwFlags);
    if (reliableMoveBoundary)
        object.insert(QStringLiteral("moveBoundary"), true);
    return object;
}

QJsonObject keyboardMessage(int keyCode, const QString &dwFlags)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("keyboard"));
    object.insert(QStringLiteral("key"), keyCode);
    object.insert(QStringLiteral("dwFlags"), dwFlags);
    return object;
}

QJsonObject keyboardTextMessage(const QString &text)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("keyboardText"));
    object.insert(QStringLiteral("text"), text);
    return object;
}

QJsonObject sasMessage()
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("sas"));
    return object;
}

bool triggerSecureAttentionSequenceDirect(QString *errorMessage)
{
    HMODULE sasModule = LoadLibraryW(L"sas.dll");
    if (!sasModule)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("InputUtil", "sas.dll is not available: %1").arg(GetLastError());
        return false;
    }

    using SendSasFunc = void(WINAPI *)(BOOL);
    auto sendSas = reinterpret_cast<SendSasFunc>(GetProcAddress(sasModule, "SendSAS"));
    if (!sendSas)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("InputUtil", "sas.dll does not export SendSAS: %1").arg(GetLastError());
        FreeLibrary(sasModule);
        return false;
    }

    sendSas(FALSE);
    FreeLibrary(sasModule);
    return true;
}

BOOL CALLBACK countVisibleDesktopWindows(HWND hwnd, LPARAM param)
{
    auto *count = reinterpret_cast<int *>(param);
    if (!count || !IsWindowVisible(hwnd))
        return TRUE;
    RECT rect{};
    if (GetWindowRect(hwnd, &rect) &&
        rect.right > rect.left &&
        rect.bottom > rect.top)
    {
        ++(*count);
    }
    return TRUE;
}

struct WindowOverlayState
{
    HDC targetDc = nullptr;
    HDC referenceDc = nullptr;
    int virtualX = 0;
    int virtualY = 0;
    int virtualWidth = 0;
    int virtualHeight = 0;
    int paintedWindows = 0;
};

struct SecureFrameQuality
{
    int score = -1;
    int colorCount = 0;
    int lumaStdDev = 0;
    int averageEdge = 0;
    int centralEdge = 0;
    int blueRatioPercent = 0;
    bool bluePlaceholder = false;
};

BOOL CALLBACK overlayVisibleDesktopWindow(HWND hwnd, LPARAM param)
{
    auto *state = reinterpret_cast<WindowOverlayState *>(param);
    if (!state || !state->targetDc || !state->referenceDc || !IsWindowVisible(hwnd))
        return TRUE;

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect) ||
        rect.right <= rect.left ||
        rect.bottom <= rect.top ||
        rect.right <= state->virtualX ||
        rect.bottom <= state->virtualY ||
        rect.left >= state->virtualX + state->virtualWidth ||
        rect.top >= state->virtualY + state->virtualHeight)
    {
        return TRUE;
    }

    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    HDC windowDc = CreateCompatibleDC(state->referenceDc);
    HBITMAP windowBitmap = CreateCompatibleBitmap(state->referenceDc, width, height);
    if (!windowDc || !windowBitmap)
    {
        if (windowBitmap)
            DeleteObject(windowBitmap);
        if (windowDc)
            DeleteDC(windowDc);
        return TRUE;
    }

    HGDIOBJ oldObject = SelectObject(windowDc, windowBitmap);
    constexpr UINT kPrintWindowRenderFullContent = 0x00000002;
    if (PrintWindow(hwnd, windowDc, kPrintWindowRenderFullContent))
    {
        BitBlt(state->targetDc,
               rect.left - state->virtualX,
               rect.top - state->virtualY,
               width,
               height,
               windowDc,
               0,
               0,
               SRCCOPY);
        ++state->paintedWindows;
    }

    if (oldObject)
        SelectObject(windowDc, oldObject);
    DeleteObject(windowBitmap);
    DeleteDC(windowDc);
    return TRUE;
}

QRect requestedSecureCaptureRect(const QJsonObject &request)
{
    const int width = request.value(QStringLiteral("width")).toInt();
    const int height = request.value(QStringLiteral("height")).toInt();
    if (width <= 0 || height <= 0)
        return QRect();
    return QRect(request.value(QStringLiteral("x")).toInt(),
                 request.value(QStringLiteral("y")).toInt(),
                 width,
                 height);
}

QRect boundedSecureCaptureRect(const QRect &requested)
{
    const QRect virtualRect(GetSystemMetrics(SM_XVIRTUALSCREEN),
                            GetSystemMetrics(SM_YVIRTUALSCREEN),
                            qMax(1, GetSystemMetrics(SM_CXVIRTUALSCREEN)),
                            qMax(1, GetSystemMetrics(SM_CYVIRTUALSCREEN)));
    if (requested.isValid())
    {
        const QRect bounded = requested.intersected(virtualRect);
        if (bounded.isValid() && !bounded.isEmpty())
            return bounded;
    }
    return virtualRect;
}

bool captureDesktopFrameFromCandidate(const QString &candidate,
                                      const QRect &requestedRect,
                                      QJsonObject *header,
                                      QByteArray *pixels)
{
    if (!header || !pixels)
        return false;

    std::atomic_bool ok{false};
    std::thread worker([&]() {
        HDESK originalDesktop = GetThreadDesktop(GetCurrentThreadId());
        HDESK targetDesktop = nullptr;
        if (candidate == QStringLiteral("Winlogon"))
            targetDesktop = OpenDesktopW(L"Winlogon", 0, FALSE, desktopAccessMask());
        else if (candidate == QStringLiteral("Default"))
            targetDesktop = OpenDesktopW(L"Default", 0, FALSE, desktopAccessMask());
        else
            targetDesktop = OpenInputDesktop(0, FALSE, desktopAccessMask());

        if (!targetDesktop)
        {
            LOG_WARN("Secure capture failed to open {} desktop: {}", candidate, GetLastError());
            return;
        }

        const QString actualDesktopName = desktopName(targetDesktop);
        bool desktopChanged = false;
        if (SetThreadDesktop(targetDesktop))
            desktopChanged = true;
        else
            LOG_WARN("Secure capture failed to switch {} desktop (actual={}): {}",
                     candidate,
                     actualDesktopName,
                     GetLastError());

        int visibleWindowCount = 0;
        if (desktopChanged)
            EnumWindows(countVisibleDesktopWindows, reinterpret_cast<LPARAM>(&visibleWindowCount));

        const QRect captureRect = boundedSecureCaptureRect(requestedRect);
        const int x = captureRect.x();
        const int y = captureRect.y();
        const int width = qMax(1, captureRect.width());
        const int height = qMax(1, captureRect.height());
        HDC screenDc = GetDC(nullptr);
        HDC memoryDc = screenDc ? CreateCompatibleDC(screenDc) : nullptr;
        HBITMAP bitmap = screenDc ? CreateCompatibleBitmap(screenDc, width, height) : nullptr;
        if (!screenDc || !memoryDc || !bitmap)
        {
            LOG_WARN("Secure capture failed to create GDI resources: {}", GetLastError());
        }
        else
        {
            HGDIOBJ oldObject = SelectObject(memoryDc, bitmap);
            const BOOL bltOk = BitBlt(memoryDc, 0, 0, width, height, screenDc, x, y, SRCCOPY | CAPTUREBLT);
            if (!bltOk)
            {
                LOG_WARN("Secure capture BitBlt failed: {}", GetLastError());
            }
            else
            {
                WindowOverlayState overlayState{};
                overlayState.targetDc = memoryDc;
                overlayState.referenceDc = screenDc;
                overlayState.virtualX = x;
                overlayState.virtualY = y;
                overlayState.virtualWidth = width;
                overlayState.virtualHeight = height;
                if (desktopChanged)
                    EnumWindows(overlayVisibleDesktopWindow, reinterpret_cast<LPARAM>(&overlayState));

                BITMAPINFO info{};
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = width;
                info.bmiHeader.biHeight = -height;
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                info.bmiHeader.biCompression = BI_RGB;
                const int stride = width * 4;
                QByteArray data(stride * height, Qt::Uninitialized);
                const int rows = GetDIBits(memoryDc,
                                           bitmap,
                                           0,
                                           static_cast<UINT>(height),
                                           data.data(),
                                           &info,
                                           DIB_RGB_COLORS);
                if (rows == height)
                {
                    header->insert(QStringLiteral("ok"), true);
                    header->insert(QStringLiteral("width"), width);
                    header->insert(QStringLiteral("height"), height);
                    header->insert(QStringLiteral("stride"), stride);
                    header->insert(QStringLiteral("bytes"), data.size());
                    header->insert(QStringLiteral("x"), x);
                    header->insert(QStringLiteral("y"), y);
                    header->insert(QStringLiteral("desktop"), actualDesktopName.isEmpty() ? candidate : actualDesktopName);
                    header->insert(QStringLiteral("windows"), visibleWindowCount);
                    header->insert(QStringLiteral("paintedWindows"), overlayState.paintedWindows);
                    header->insert(QStringLiteral("cropped"), requestedRect.isValid());
                    *pixels = std::move(data);
                    ok.store(true);
                }
                else
                {
                    LOG_WARN("Secure capture GetDIBits failed: rows={}, error={}", rows, GetLastError());
                }
            }
            if (oldObject)
                SelectObject(memoryDc, oldObject);
        }

        if (bitmap)
            DeleteObject(bitmap);
        if (memoryDc)
            DeleteDC(memoryDc);
        if (screenDc)
            ReleaseDC(nullptr, screenDc);
        if (desktopChanged && originalDesktop)
            SetThreadDesktop(originalDesktop);
        CloseDesktop(targetDesktop);
    });
    worker.join();
    return ok.load();
}

QRect primarySecureCaptureRect()
{
    const POINT origin{0, 0};
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor && GetMonitorInfo(monitor, &info))
    {
        return QRect(info.rcMonitor.left,
                     info.rcMonitor.top,
                     qMax(1, static_cast<int>(info.rcMonitor.right - info.rcMonitor.left)),
                     qMax(1, static_cast<int>(info.rcMonitor.bottom - info.rcMonitor.top)));
    }
    return QRect();
}

SecureFrameQuality analyzeSecureFrameContent(const QJsonObject &header, const QByteArray &pixels)
{
    SecureFrameQuality quality;
    const int width = header.value(QStringLiteral("width")).toInt();
    const int height = header.value(QStringLiteral("height")).toInt();
    const int stride = header.value(QStringLiteral("stride")).toInt();
    if (width <= 0 || height <= 0 || stride < width * 4 || pixels.size() < stride * height)
        return quality;

    constexpr int kGridX = 64;
    constexpr int kGridY = 36;
    std::set<uint32_t> colors;
    std::vector<int> luminance;
    std::vector<bool> bluePixels;
    luminance.reserve(kGridX * kGridY);
    bluePixels.reserve(kGridX * kGridY);
    int blueCount = 0;

    const uchar *data = reinterpret_cast<const uchar *>(pixels.constData());
    for (int gy = 0; gy < kGridY; ++gy)
    {
        const int y = qBound(0, (gy * height) / kGridY, height - 1);
        for (int gx = 0; gx < kGridX; ++gx)
        {
            const int x = qBound(0, (gx * width) / kGridX, width - 1);
            const uchar *pixel = data + y * stride + x * 4;
            const int b = pixel[0];
            const int g = pixel[1];
            const int r = pixel[2];
            const uint32_t quantized = static_cast<uint32_t>(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
            const bool blueLike = b > 120 && b > r + 45 && b > g + 15 && r < 110;
            colors.insert(quantized);
            luminance.push_back((r * 30 + g * 59 + b * 11) / 100);
            bluePixels.push_back(blueLike);
            if (blueLike)
                ++blueCount;
        }
    }

    double mean = 0.0;
    for (int value : luminance)
        mean += value;
    mean /= qMax(1, static_cast<int>(luminance.size()));

    double variance = 0.0;
    for (int value : luminance)
    {
        const double delta = value - mean;
        variance += delta * delta;
    }
    variance /= qMax(1, static_cast<int>(luminance.size()));

    int edgeSum = 0;
    int edgeCount = 0;
    for (int i = 1; i < static_cast<int>(luminance.size()); ++i)
    {
        if (i % kGridX == 0)
            continue;
        edgeSum += std::abs(luminance[i] - luminance[i - 1]);
        ++edgeCount;
    }
    const int averageEdge = edgeCount > 0 ? edgeSum / edgeCount : 0;

    int centralEdgeSum = 0;
    int centralEdgeCount = 0;
    for (int gy = kGridY / 4; gy < (kGridY * 3) / 4; ++gy)
    {
        for (int gx = kGridX / 4 + 1; gx < (kGridX * 3) / 4; ++gx)
        {
            const int index = gy * kGridX + gx;
            centralEdgeSum += std::abs(luminance[index] - luminance[index - 1]);
            ++centralEdgeCount;
        }
    }
    const int centralEdge = centralEdgeCount > 0 ? centralEdgeSum / centralEdgeCount : 0;
    const int lumaStdDev = static_cast<int>(std::sqrt(variance));
    const int blueRatioPercent = static_cast<int>((blueCount * 100) / qMax(1, static_cast<int>(bluePixels.size())));

    int score = static_cast<int>(colors.size()) * 80 +
                lumaStdDev * 25 +
                averageEdge * 20 +
                centralEdge * 35 +
                header.value(QStringLiteral("windows")).toInt() * 120 +
                header.value(QStringLiteral("paintedWindows")).toInt() * 300;
    const bool lowInformationFrame = colors.size() <= 3 && variance < 10.0;
    if (lowInformationFrame)
        score -= 3000;
    const bool sparseBlueFrame = blueRatioPercent >= 70 &&
                                 colors.size() <= 16 &&
                                 lumaStdDev <= 28 &&
                                 averageEdge <= 8 &&
                                 centralEdge <= 8 &&
                                 header.value(QStringLiteral("paintedWindows")).toInt() <= 0;
    const bool bluePlaceholder = lowInformationFrame || sparseBlueFrame;
    if (bluePlaceholder)
        score -= 5000;

    quality.score = score;
    quality.colorCount = static_cast<int>(colors.size());
    quality.lumaStdDev = lumaStdDev;
    quality.averageEdge = averageEdge;
    quality.centralEdge = centralEdge;
    quality.blueRatioPercent = blueRatioPercent;
    quality.bluePlaceholder = bluePlaceholder;
    return quality;
}

bool isSecureFrameAccepted(const QJsonObject &header)
{
    if (header.value(QStringLiteral("score")).toInt(-1) < kMinimumUsefulSecureDesktopScore)
        return false;
    if (header.value(QStringLiteral("bluePlaceholder")).toBool(false))
        return false;
    if (header.value(QStringLiteral("colors")).toInt() <= 3 &&
        header.value(QStringLiteral("lumaStdDev")).toInt() <= 3)
    {
        return false;
    }
    return true;
}

void scaleSecureFrameIfRequested(const QJsonObject &request, QJsonObject *header, QByteArray *pixels)
{
    if (!header || !pixels || pixels->isEmpty())
        return;

    const int maxWidth = request.value(QStringLiteral("maxWidth")).toInt();
    const int maxHeight = request.value(QStringLiteral("maxHeight")).toInt();
    const int width = header->value(QStringLiteral("width")).toInt();
    const int height = header->value(QStringLiteral("height")).toInt();
    const int stride = header->value(QStringLiteral("stride")).toInt();
    if (maxWidth <= 0 || maxHeight <= 0 ||
        width <= 0 || height <= 0 ||
        width <= maxWidth && height <= maxHeight ||
        stride < width * 4 ||
        pixels->size() < stride * height)
    {
        return;
    }

    const QSize targetSize = QSize(width, height).scaled(maxWidth, maxHeight, Qt::KeepAspectRatio);
    if (targetSize.isEmpty() || targetSize.width() <= 0 || targetSize.height() <= 0 ||
        targetSize == QSize(width, height))
    {
        return;
    }

    const QImage source(reinterpret_cast<const uchar *>(pixels->constData()),
                        width,
                        height,
                        stride,
                        QImage::Format_ARGB32);
    QImage scaled = source.scaled(targetSize, Qt::KeepAspectRatio, Qt::FastTransformation)
                        .convertToFormat(QImage::Format_ARGB32);
    if (scaled.isNull())
        return;

    const int scaledStride = scaled.bytesPerLine();
    QByteArray scaledPixels(scaledStride * scaled.height(), Qt::Uninitialized);
    for (int y = 0; y < scaled.height(); ++y)
    {
        memcpy(scaledPixels.data() + y * scaledStride,
               scaled.constScanLine(y),
               static_cast<size_t>(scaledStride));
    }

    header->insert(QStringLiteral("originalWidth"), width);
    header->insert(QStringLiteral("originalHeight"), height);
    header->insert(QStringLiteral("scaled"), true);
    header->insert(QStringLiteral("width"), scaled.width());
    header->insert(QStringLiteral("height"), scaled.height());
    header->insert(QStringLiteral("stride"), scaledStride);
    header->insert(QStringLiteral("bytes"), scaledPixels.size());
    *pixels = std::move(scaledPixels);
}

struct SecureFrameSharedMemory
{
    HANDLE mapping = nullptr;
    QString name;

    ~SecureFrameSharedMemory()
    {
        if (mapping)
            CloseHandle(mapping);
    }

    SecureFrameSharedMemory() = default;
    SecureFrameSharedMemory(const SecureFrameSharedMemory &) = delete;
    SecureFrameSharedMemory &operator=(const SecureFrameSharedMemory &) = delete;
};

bool createWorldReadableSecurityAttributes(SECURITY_ATTRIBUTES *attributes, PSECURITY_DESCRIPTOR *descriptor)
{
    if (!attributes || !descriptor)
        return false;
    *descriptor = nullptr;
    static const wchar_t kSecurityDescriptor[] =
        L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)(A;;GRGW;;;WD)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kSecurityDescriptor,
                                                              SDDL_REVISION_1,
                                                              descriptor,
                                                              nullptr))
    {
        return false;
    }
    attributes->nLength = sizeof(SECURITY_ATTRIBUTES);
    attributes->lpSecurityDescriptor = *descriptor;
    attributes->bInheritHandle = FALSE;
    return true;
}

std::unique_ptr<SecureFrameSharedMemory> createSecureFrameSharedMemory(const QByteArray &pixels, QString *errorMessage)
{
    if (pixels.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("InputUtil", "empty frame");
        return nullptr;
    }

    SECURITY_ATTRIBUTES securityAttributes{};
    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
    SECURITY_ATTRIBUTES *securityAttributesPtr = nullptr;
    if (createWorldReadableSecurityAttributes(&securityAttributes, &securityDescriptor))
        securityAttributesPtr = &securityAttributes;

    auto shared = std::make_unique<SecureFrameSharedMemory>();
    shared->name = QStringLiteral("Global\\airan-desk-secure-frame-%1-%2")
                       .arg(GetCurrentProcessId())
                       .arg(uuidWithoutBraces());
    const std::wstring mappingName = shared->name.toStdWString();
    const quint64 size = static_cast<quint64>(pixels.size());
    shared->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                         securityAttributesPtr,
                                         PAGE_READWRITE,
                                         static_cast<DWORD>(size >> 32),
                                         static_cast<DWORD>(size & 0xFFFFFFFF),
                                         mappingName.c_str());
    if (securityDescriptor)
        LocalFree(securityDescriptor);
    if (!shared->mapping)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("InputUtil", "CreateFileMapping failed: %1").arg(GetLastError());
        return nullptr;
    }

    void *view = MapViewOfFile(shared->mapping, FILE_MAP_WRITE, 0, 0, static_cast<SIZE_T>(pixels.size()));
    if (!view)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("InputUtil", "MapViewOfFile failed: %1").arg(GetLastError());
        return nullptr;
    }
    memcpy(view, pixels.constData(), static_cast<size_t>(pixels.size()));
    UnmapViewOfFile(view);
    return shared;
}

void waitForSecureFrameSharedMemoryAck(QLocalSocket *socket, const QString &name)
{
    if (!socket || name.isEmpty())
        return;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000)
    {
        QByteArray line;
        const int remaining = 3000 - static_cast<int>(timer.elapsed());
        if (!readBoundedJsonLine(socket, &line, 4096, remaining))
            break;
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject())
        {
            const QJsonObject object = document.object();
            if (object.value(QStringLiteral("type")).toString() == QStringLiteral("secureCaptureFrameRead") &&
                object.value(QStringLiteral("sharedMemoryName")).toString() == name)
                return;
        }
    }
    LOG_WARN("Timed out waiting for secure frame shared memory read ack: {}", name);
}

bool captureInputDesktopFrame(const QJsonObject &request, QJsonObject *header, QByteArray *pixels)
{
    if (!header || !pixels)
        return false;

    struct CandidateFrame
    {
        QString name;
        QJsonObject header;
        QByteArray pixels;
        int score = -1;
        int securePriority = 0;
    };

    struct CaptureRectCandidate
    {
        QString kind;
        QRect rect;
    };

    std::optional<CandidateFrame> best;
    static std::atomic_llong lastCandidateLogMs{0};
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    qint64 previousLogMs = lastCandidateLogMs.load();
    const bool logCandidates = !g_secureCaptureDesktopLogged.load() ||
                               nowMs - previousLogMs > 5000;
    if (logCandidates)
        lastCandidateLogMs.store(nowMs);
    const QRect requestRect = requestedSecureCaptureRect(request);
    std::vector<CaptureRectCandidate> rectCandidates;
    if (requestRect.isValid() && !requestRect.isEmpty())
        rectCandidates.push_back({QStringLiteral("selected"), requestRect});
    const QRect primaryRect = primarySecureCaptureRect();
    if (primaryRect.isValid() && !primaryRect.isEmpty() && primaryRect != requestRect)
        rectCandidates.push_back({QStringLiteral("primary"), primaryRect});
    rectCandidates.push_back({QStringLiteral("virtual"), QRect()});

    auto secureDesktopPriority = [](const QString &name) {
        if (name.compare(QStringLiteral("Input"), Qt::CaseInsensitive) == 0)
            return 3;
        if (name.compare(QStringLiteral("Winlogon"), Qt::CaseInsensitive) == 0)
            return 2;
        return 1;
    };

    const QStringList candidates{QStringLiteral("Input"), QStringLiteral("Winlogon"), QStringLiteral("Default")};
    for (const QString &candidate : candidates)
    {
        for (const CaptureRectCandidate &rectCandidate : rectCandidates)
        {
            QJsonObject candidateHeader;
            QByteArray candidatePixels;
            if (!captureDesktopFrameFromCandidate(candidate, rectCandidate.rect, &candidateHeader, &candidatePixels))
                continue;

            const SecureFrameQuality quality = analyzeSecureFrameContent(candidateHeader, candidatePixels);
            candidateHeader.insert(QStringLiteral("score"), quality.score);
            candidateHeader.insert(QStringLiteral("colors"), quality.colorCount);
            candidateHeader.insert(QStringLiteral("lumaStdDev"), quality.lumaStdDev);
            candidateHeader.insert(QStringLiteral("edgeAvg"), quality.averageEdge);
            candidateHeader.insert(QStringLiteral("centralEdge"), quality.centralEdge);
            candidateHeader.insert(QStringLiteral("blueRatio"), quality.blueRatioPercent);
            candidateHeader.insert(QStringLiteral("bluePlaceholder"), quality.bluePlaceholder);
            candidateHeader.insert(QStringLiteral("rectKind"), rectCandidate.kind);
            const int priority = secureDesktopPriority(candidateHeader.value(QStringLiteral("desktop")).toString(candidate));
            candidateHeader.insert(QStringLiteral("securePriority"), priority);
            if (logCandidates)
            {
                LOG_DEBUG("Secure desktop capture candidate={}, rect={}, actual={}, priority={}, windows={}, painted={}, score={}, colors={}, lumaStdDev={}, edgeAvg={}, centralEdge={}, blueRatio={}%, bluePlaceholder={}, size={}x{}+{}+{}, cropped={}",
                          candidate,
                          rectCandidate.kind,
                          candidateHeader.value(QStringLiteral("desktop")).toString(),
                          priority,
                          candidateHeader.value(QStringLiteral("windows")).toInt(),
                          candidateHeader.value(QStringLiteral("paintedWindows")).toInt(),
                          quality.score,
                          quality.colorCount,
                          quality.lumaStdDev,
                          quality.averageEdge,
                          quality.centralEdge,
                          quality.blueRatioPercent,
                          quality.bluePlaceholder,
                          candidateHeader.value(QStringLiteral("width")).toInt(),
                          candidateHeader.value(QStringLiteral("height")).toInt(),
                          candidateHeader.value(QStringLiteral("x")).toInt(),
                          candidateHeader.value(QStringLiteral("y")).toInt(),
                          candidateHeader.value(QStringLiteral("cropped")).toBool(false));
            }

            const bool candidateAccepted = isSecureFrameAccepted(candidateHeader);
            const bool bestAccepted = best.has_value() && isSecureFrameAccepted(best->header);
            if (!best.has_value() ||
                (candidateAccepted && !bestAccepted) ||
                (candidateAccepted == bestAccepted && priority > best->securePriority) ||
                (candidateAccepted == bestAccepted && priority == best->securePriority &&
                 quality.score > best->score))
            {
                best = CandidateFrame{candidate, std::move(candidateHeader), std::move(candidatePixels), quality.score, priority};
            }
        }
    }

    if (best.has_value())
    {
        if (logCandidates || !g_secureCaptureDesktopLogged.load())
        {
            LOG_DEBUG("Secure desktop capture selected candidate={}, rect={}, actual={}, priority={}, windows={}, painted={}, score={}, colors={}, lumaStdDev={}, edgeAvg={}, centralEdge={}, blueRatio={}%, bluePlaceholder={}, size={}x{}+{}+{}, cropped={}",
                      best->name,
                      best->header.value(QStringLiteral("rectKind")).toString(),
                      best->header.value(QStringLiteral("desktop")).toString(),
                      best->securePriority,
                      best->header.value(QStringLiteral("windows")).toInt(),
                      best->header.value(QStringLiteral("paintedWindows")).toInt(),
                      best->score,
                      best->header.value(QStringLiteral("colors")).toInt(),
                      best->header.value(QStringLiteral("lumaStdDev")).toInt(),
                      best->header.value(QStringLiteral("edgeAvg")).toInt(),
                      best->header.value(QStringLiteral("centralEdge")).toInt(),
                      best->header.value(QStringLiteral("blueRatio")).toInt(),
                      best->header.value(QStringLiteral("bluePlaceholder")).toBool(false),
                      best->header.value(QStringLiteral("width")).toInt(),
                      best->header.value(QStringLiteral("height")).toInt(),
                      best->header.value(QStringLiteral("x")).toInt(),
                      best->header.value(QStringLiteral("y")).toInt(),
                      best->header.value(QStringLiteral("cropped")).toBool(false));
        }
        g_secureCaptureDesktopLogged.store(true);
        scaleSecureFrameIfRequested(request, &best->header, &best->pixels);
        const QString frameHash = QString::fromLatin1(
            QCryptographicHash::hash(best->pixels, QCryptographicHash::Md5).toHex());
        best->header.insert(QStringLiteral("hash"), frameHash);
        if (!frameHash.isEmpty() && frameHash == request.value(QStringLiteral("lastHash")).toString())
        {
            best->header.insert(QStringLiteral("ok"), false);
            best->header.insert(QStringLiteral("bytes"), 0);
            best->header.insert(QStringLiteral("error"), QStringLiteral("duplicate-frame"));
            best->pixels.clear();
        }
        *header = std::move(best->header);
        *pixels = std::move(best->pixels);
        return true;
    }
    return false;
}

bool sendBrokerControlMessage(const BrokerEndpoint &endpoint, const QString &type, bool waitForAck = false)
{
    if (endpoint.serverName.isEmpty())
        return false;

    QJsonObject message;
    message.insert(QStringLiteral("type"), type);
    if (waitForAck)
        message.insert(QStringLiteral("ack"), true);
    if (!endpoint.token.isEmpty())
        message.insert(QStringLiteral("token"), endpoint.token);
    const QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';

    QLocalSocket socket;
    if (!connectToServer(&socket,
                         endpoint.serverName,
                         kBrokerConnectTimeoutMs,
                         waitForAck ? QIODevice::ReadWrite : QIODevice::WriteOnly))
        return false;
    const bool ok = socket.write(data) == data.size() &&
                    socket.waitForBytesWritten(kBrokerWriteTimeoutMs);
    if (ok && waitForAck)
    {
        QByteArray line;
        if (!readBoundedJsonLine(&socket, &line, 4096, 500))
        {
            socket.disconnectFromServer();
            return false;
        }
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
        {
            socket.disconnectFromServer();
            return false;
        }
        const bool ackOk = document.object().value(QStringLiteral("ok")).toBool(false);
        socket.disconnectFromServer();
        return ackOk;
    }
    socket.disconnectFromServer();
    return ok;
}

bool sendKeyboardEventDirect(int keyCode, const QString &dwFlags, QString *errorMessage = nullptr)
{
    const QString inputDesktop = currentInputDesktopName();
    const bool secureDesktop = isSecureInputDesktopName(inputDesktop);
    const bool letterKey = isLetterVirtualKey(keyCode);

    if (secureDesktop && letterKey)
    {
        QString error;
        const QString modifiers = keyboardModifierStateSummary();
        const bool ok = sendSecureLetterKeybdEvent(keyCode, dwFlags, &error);
        if (!ok)
        {
            if (errorMessage)
                *errorMessage = error;
        }
        LOG_DEBUG("Windows input broker secure letter injection: key={}, flags={}, desktop={}, mode=keybd_event, modifiers={}, ok={}, error={}",
                  keyCode,
                  dwFlags,
                  inputDesktop,
                  modifiers,
                  ok,
                  error);
        return ok;
    }

    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    DWORD flags = dwFlags == "down" ? 0 : KEYEVENTF_KEYUP;
    input.ki.wVk = static_cast<WORD>(keyCode);
    input.ki.dwFlags = flags;
    QString error;
    const bool ok = sendInputs(&input, 1, dwFlags == "down" ? "keyboard down" : "keyboard up", &error);
    if (!ok && errorMessage)
        *errorMessage = error;
    if (secureDesktop || letterKey)
    {
        LOG_DEBUG("Windows input broker keyboard injection: key={}, flags={}, desktop={}, mode=sendinput-vk, ok={}, error={}",
                  keyCode,
                  dwFlags,
                  inputDesktop,
                  ok,
                  error);
    }
    return ok;
}

bool sendKeyboardTextDirect(const QString &text)
{
    if (text.isEmpty())
        return true;

    bool ok = true;
    const std::wstring value = text.toStdWString();
    for (wchar_t ch : value)
    {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = static_cast<WORD>(ch);
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
        inputs[1] = inputs[0];
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        ok = sendInputs(inputs, 2, "keyboard text") && ok;
    }
    return ok;
}

bool sendMouseEventAtPixel(int button, int x, int y, int mouseData, const QString &dwFlags, QString *errorMessage = nullptr)
{
    const int virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int virtualW = qMax(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    const int virtualH = qMax(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    const int boundedX = qBound(virtualX, x, virtualX + virtualW - 1);
    const int boundedY = qBound(virtualY, y, virtualY + virtualH - 1);
    const LONG absX = normalizeAbsoluteCoordinate(boundedX - virtualX, virtualW);
    const LONG absY = normalizeAbsoluteCoordinate(boundedY - virtualY, virtualH);

    if (dwFlags == "move")
    {
        moveCursorToPixel(boundedX, boundedY);
        INPUT input = makeMouseMoveInput(absX, absY);
        return sendInputs(&input, 1, "mouse move", errorMessage);
    }

    moveCursorToPixel(boundedX, boundedY);
    if (dwFlags == "wheel")
    {
        INPUT inputs[2] = {
            makeMouseMoveInput(absX, absY),
            makeMouseWheelInput(mouseData),
        };
        return sendInputs(inputs, 2, "mouse wheel", errorMessage);
    }

    if (dwFlags == "doubleClick")
    {
        const DWORD downFlag = (button == Qt::LeftButton) ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_RIGHTDOWN;
        const DWORD upFlag = (button == Qt::LeftButton) ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_RIGHTUP;
        INPUT inputs[5] = {
            makeMouseMoveInput(absX, absY),
            makeMouseButtonInput(downFlag),
            makeMouseButtonInput(upFlag),
            makeMouseButtonInput(downFlag),
            makeMouseButtonInput(upFlag),
        };
        return sendInputs(inputs, 5, "mouse double click", errorMessage);
    }

    DWORD flag = 0;
    switch (button)
    {
    case Qt::LeftButton:
        flag = (dwFlags == "down") ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        break;
    case Qt::RightButton:
        flag = (dwFlags == "down") ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        break;
    case Qt::MiddleButton:
        flag = (dwFlags == "down") ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        break;
    default:
        return false;
    }

    INPUT inputs[2] = {
        makeMouseMoveInput(absX, absY),
        makeMouseButtonInput(flag),
    };
    return sendInputs(inputs, 2, dwFlags == "down" ? "mouse button down" : "mouse button up", errorMessage);
}

void sendBrokerAck(QLocalSocket *socket, bool ok, const QString &error = QString())
{
    if (!socket)
        return;
    QJsonObject ack;
    ack.insert(QStringLiteral("ok"), ok);
    if (!error.isEmpty())
        ack.insert(QStringLiteral("error"), error);
    socket->write(QJsonDocument(ack).toJson(QJsonDocument::Compact) + '\n');
    socket->waitForBytesWritten(200);
}

QRect monitorRectForIndex(int screenIndex)
{
    if (screenIndex >= 0)
    {
        const QList<QScreen *> screens = QGuiApplication::screens();
        if (QScreen *screen = screens.value(screenIndex, nullptr))
        {
            const auto physicalRect = physicalMonitorRectForScreen(screen, screenIndex);
            if (physicalRect.has_value())
                return *physicalRect;
        }

        MonitorSelection selection{};
        selection.targetIndex = screenIndex;
        EnumDisplayMonitors(nullptr, nullptr, enumerateMonitorRectByIndex, reinterpret_cast<LPARAM>(&selection));
        if (selection.found)
            return QRect(selection.rect.left,
                         selection.rect.top,
                         qMax(1, static_cast<int>(selection.rect.right - selection.rect.left)),
                         qMax(1, static_cast<int>(selection.rect.bottom - selection.rect.top)));
    }

    const POINT origin{0, 0};
    HMONITOR primary = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (primary && GetMonitorInfo(primary, &info))
    {
        return QRect(info.rcMonitor.left,
                     info.rcMonitor.top,
                     qMax(1, static_cast<int>(info.rcMonitor.right - info.rcMonitor.left)),
                     qMax(1, static_cast<int>(info.rcMonitor.bottom - info.rcMonitor.top)));
    }

    return QRect(GetSystemMetrics(SM_XVIRTUALSCREEN),
                 GetSystemMetrics(SM_YVIRTUALSCREEN),
                 qMax(1, GetSystemMetrics(SM_CXVIRTUALSCREEN)),
                 qMax(1, GetSystemMetrics(SM_CYVIRTUALSCREEN)));
}

void writeSecureCaptureFrameResponse(QLocalSocket *socket, const QJsonObject &request)
{
    struct CaptureFlagGuard
    {
        ~CaptureFlagGuard()
        {
            g_secureCaptureInProgress.store(false);
        }
    } guard;

    QJsonObject header;
    header.insert(QStringLiteral("type"), QStringLiteral("secureCaptureFrame"));
    QByteArray pixels;
    if (!captureInputDesktopFrame(request, &header, &pixels))
    {
        header.insert(QStringLiteral("ok"), false);
        header.insert(QStringLiteral("bytes"), 0);
    }
    else if (header.value(QStringLiteral("ok")).toBool(false) &&
             !isSecureFrameAccepted(header))
    {
        const QString error = header.value(QStringLiteral("bluePlaceholder")).toBool(false)
                                  ? QStringLiteral("blue-placeholder-frame")
                                  : QStringLiteral("low-content-frame");
        LOG_WARN("Secure desktop broker suppressed frame: reason={}, desktop={}, score={}, colors={}, lumaStdDev={}, edgeAvg={}, centralEdge={}, blueRatio={}%, size={}x{}+{}+{}",
                 error,
                 header.value(QStringLiteral("desktop")).toString(),
                 header.value(QStringLiteral("score")).toInt(),
                 header.value(QStringLiteral("colors")).toInt(),
                 header.value(QStringLiteral("lumaStdDev")).toInt(),
                 header.value(QStringLiteral("edgeAvg")).toInt(),
                 header.value(QStringLiteral("centralEdge")).toInt(),
                 header.value(QStringLiteral("blueRatio")).toInt(),
                 header.value(QStringLiteral("width")).toInt(),
                 header.value(QStringLiteral("height")).toInt(),
                 header.value(QStringLiteral("x")).toInt(),
                 header.value(QStringLiteral("y")).toInt());
        pixels.clear();
        header.insert(QStringLiteral("ok"), false);
        header.insert(QStringLiteral("bytes"), 0);
        header.insert(QStringLiteral("error"), error);
    }

    if (socket)
    {
        std::unique_ptr<SecureFrameSharedMemory> sharedFrame;
        if (header.value(QStringLiteral("ok")).toBool(false) && !pixels.isEmpty() &&
            request.value(QStringLiteral("sharedMemory")).toBool(false))
        {
            QString sharedMemoryError;
            sharedFrame = createSecureFrameSharedMemory(pixels, &sharedMemoryError);
            if (sharedFrame)
            {
                header.insert(QStringLiteral("transport"), QStringLiteral("sharedMemory"));
                header.insert(QStringLiteral("sharedMemoryName"), sharedFrame->name);
                header.insert(QStringLiteral("sharedMemoryBytes"), pixels.size());
                LOG_DEBUG("Secure desktop frame using shared memory transport: name={}, bytes={}",
                          sharedFrame->name,
                          pixels.size());
                pixels.clear();
            }
            else
            {
                LOG_WARN("Secure desktop shared memory transport unavailable: {}", sharedMemoryError);
                header.insert(QStringLiteral("transport"), QStringLiteral("socket"));
            }
        }
        else
        {
            header.insert(QStringLiteral("transport"), QStringLiteral("socket"));
        }

        const QByteArray response = QJsonDocument(header).toJson(QJsonDocument::Compact) + '\n';
        socket->write(response);
        if (!pixels.isEmpty())
            socket->write(pixels);
        socket->waitForBytesWritten(3000);
        if (sharedFrame)
            waitForSecureFrameSharedMemoryAck(socket, sharedFrame->name);
        socket->disconnectFromServer();
        socket->waitForDisconnected(200);
        delete socket;
    }
}

class SecureCaptureFrameThread : public QThread
{
public:
    SecureCaptureFrameThread(QLocalSocket *socket, QJsonObject request)
        : m_socket(socket),
          m_request(std::move(request))
    {
    }

protected:
    void run() override
    {
        writeSecureCaptureFrameResponse(m_socket, m_request);
    }

private:
    QLocalSocket *m_socket = nullptr;
    QJsonObject m_request;
};

bool handleBrokerMessage(const QJsonObject &object, const QString &expectedToken, QLocalSocket *socket)
{
    const QString type = object.value(QStringLiteral("type")).toString();
    if (expectedToken.isEmpty() || object.value(QStringLiteral("token")).toString() != expectedToken)
    {
        LOG_WARN("Rejected Windows input broker message with invalid token");
        if (socket && type == QStringLiteral("secureCaptureFrame"))
        {
            QJsonObject response;
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("type"), QStringLiteral("secureCaptureFrame"));
            response.insert(QStringLiteral("error"), QStringLiteral("broker-authentication-failed"));
            response.insert(QStringLiteral("bytes"), 0);
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
            socket->waitForBytesWritten(200);
        }
        else if (object.value(QStringLiteral("ack")).toBool(false))
        {
            sendBrokerAck(socket, false, QStringLiteral("broker-authentication-failed"));
        }
        return false;
    }

    const bool wantsAck = object.value(QStringLiteral("ack")).toBool(false);
    if (type == QStringLiteral("ping"))
    {
        if (socket)
        {
            QJsonObject ack;
            ack.insert(QStringLiteral("ok"), true);
            ack.insert(QStringLiteral("type"), QStringLiteral("pong"));
            ack.insert(QStringLiteral("version"), 1);
            socket->write(QJsonDocument(ack).toJson(QJsonDocument::Compact) + '\n');
            socket->waitForBytesWritten(200);
        }
    }
    else if (type == QStringLiteral("mousePixel"))
    {
        if (!g_brokerInputCommandLogged.exchange(true))
            LOG_DEBUG("Windows input broker received first input command: type=mousePixel, token={}",
                      currentProcessTokenSummary());
        QString operationError;
        const bool ok = sendMouseEventAtPixel(object.value(QStringLiteral("button")).toInt(-1),
                                              object.value(QStringLiteral("x")).toInt(),
                                              object.value(QStringLiteral("y")).toInt(),
                                              object.value(QStringLiteral("mouseData")).toInt(),
                                              object.value(QStringLiteral("dwFlags")).toString(),
                                              &operationError);
        if (wantsAck)
            sendBrokerAck(socket, ok, ok ? QString() : (operationError.isEmpty() ? QStringLiteral("SendInput failed") : operationError));
    }
    else if (type == QStringLiteral("keyboard"))
    {
        if (!g_brokerInputCommandLogged.exchange(true))
            LOG_DEBUG("Windows input broker received first input command: type=keyboard, token={}",
                      currentProcessTokenSummary());
        const int key = object.value(QStringLiteral("key")).toInt(-1);
        const QString flags = object.value(QStringLiteral("dwFlags")).toString();
        const QString inputDesktop = currentInputDesktopName();
        if (isSecureInputDesktopName(inputDesktop))
        {
            LOG_DEBUG("Windows input broker received secure keyboard command: key={}, flags={}, desktop={}",
                      key,
                      flags,
                      inputDesktop);
        }
        QString operationError;
        const bool ok = sendKeyboardEventDirect(key, flags, &operationError);
        if (wantsAck)
            sendBrokerAck(socket, ok, ok ? QString() : (operationError.isEmpty() ? QStringLiteral("SendInput failed") : operationError));
    }
    else if (type == QStringLiteral("keyboardText"))
    {
        if (!g_brokerInputCommandLogged.exchange(true))
            LOG_DEBUG("Windows input broker received first input command: type=keyboardText, token={}",
                      currentProcessTokenSummary());
        const bool ok = sendKeyboardTextDirect(object.value(QStringLiteral("text")).toString());
        if (wantsAck)
            sendBrokerAck(socket, ok, ok ? QString() : QStringLiteral("SendInput failed"));
    }
    else if (type == QStringLiteral("secureCaptureFrame"))
    {
        if (!socket)
            return false;
        bool expected = false;
        if (!g_secureCaptureInProgress.compare_exchange_strong(expected, true))
        {
            QJsonObject busy;
            busy.insert(QStringLiteral("ok"), false);
            busy.insert(QStringLiteral("type"), QStringLiteral("secureCaptureFrame"));
            busy.insert(QStringLiteral("bytes"), 0);
            busy.insert(QStringLiteral("error"), QStringLiteral("secure-capture-busy"));
            socket->write(QJsonDocument(busy).toJson(QJsonDocument::Compact) + '\n');
            socket->waitForBytesWritten(200);
            return false;
        }
        socket->disconnect();
        socket->setParent(nullptr);
        auto *worker = new SecureCaptureFrameThread(socket, object);
        socket->moveToThread(worker);
        QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        worker->start();
        return true;
    }
    else if (type == QStringLiteral("sas"))
    {
        QString error;
        if (triggerSecureAttentionSequenceDirect(&error))
        {
            LOG_INFO("Secure attention sequence requested");
            if (wantsAck)
                sendBrokerAck(socket, true);
        }
        else
        {
            LOG_WARN("Secure attention sequence failed: {}", error);
            if (wantsAck)
                sendBrokerAck(socket, false, error);
        }
    }
    else if (type == QStringLiteral("stopService"))
    {
        if (wantsAck)
            sendBrokerAck(socket, true);
        QTimer::singleShot(0, []() {
            QString error;
            if (!stopAiranServiceDirect(&error))
                LOG_WARN("Failed to stop Airan service from broker: {}", error);
        });
    }
    else if (type == QStringLiteral("quit"))
    {
        LOG_INFO("Windows input broker quit requested");
        if (QCoreApplication *app = QCoreApplication::instance())
            QMetaObject::invokeMethod(app, "quit", Qt::QueuedConnection);
    }
    else
    {
        LOG_WARN("Unknown Windows input broker message type: {}", type);
    }
    return false;
}
} // namespace

void InputUtil::execKeyboardEvent(int keyCode, const QString &dwFlags)
{
    if (!g_windowsInputBrokerMode && sendToBrokerReliable(keyboardMessage(keyCode, dwFlags)))
        return;
    if (!directInputFallbackAllowed())
        return;

    sendKeyboardEventDirect(keyCode, dwFlags);
}

void InputUtil::execKeyboardText(const QString &text)
{
    if (text.isEmpty())
        return;
    if (!g_windowsInputBrokerMode && sendToBrokerReliable(keyboardTextMessage(text)))
        return;
    if (!directInputFallbackAllowed())
        return;

    sendKeyboardTextDirect(text);
}

bool InputUtil::sendSecureAttentionSequence(QString *errorMessage)
{
    if (!g_windowsInputBrokerMode && sendToBrokerAck(sasMessage()))
        return true;

    if (triggerSecureAttentionSequenceDirect(errorMessage))
        return true;

    if (errorMessage && errorMessage->isEmpty())
        *errorMessage = InputUtil::tr("Secure attention sequence is not available. Ctrl+Alt+Del cannot be sent to the remote Windows lock screen.");
    return false;
}

void InputUtil::execMouseEvent(int button, qreal x_n, qreal y_n, int mouseData, const QString &dwFlags,
                               bool reliableMoveBoundary)
{
    const int virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int virtualW = qMax(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    const int virtualH = qMax(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    const int x = virtualX + static_cast<int>(std::round(qBound(0.0, x_n, 1.0) * (virtualW - 1)));
    const int y = virtualY + static_cast<int>(std::round(qBound(0.0, y_n, 1.0) * (virtualH - 1)));
    const bool reliable = dwFlags != QStringLiteral("move");
    if (!g_windowsInputBrokerMode &&
        (reliable ? sendToBrokerReliable(mousePixelMessage(button, x, y, mouseData, dwFlags, reliableMoveBoundary))
                  : sendToBroker(mousePixelMessage(button, x, y, mouseData, dwFlags, reliableMoveBoundary))))
        return;
    if (!directInputFallbackAllowed())
        return;
    sendMouseEventAtPixel(button, x, y, mouseData, dwFlags);
}

void InputUtil::execMouseEventOnScreen(int button, qreal x_n, qreal y_n, int mouseData, const QString &dwFlags,
                                       int screenIndex, bool reliableMoveBoundary)
{
    const QRect rect = monitorRectForIndex(screenIndex);
    execMouseEventInRect(button, x_n, y_n, mouseData, dwFlags, rect, reliableMoveBoundary);
}

void InputUtil::execMouseEventInRect(int button, qreal x_n, qreal y_n, int mouseData, const QString &dwFlags,
                                     const QRect &rect, bool reliableMoveBoundary)
{
    if (!rect.isValid())
    {
        execMouseEvent(button, x_n, y_n, mouseData, dwFlags, reliableMoveBoundary);
        return;
    }
    const int x = rect.left() + static_cast<int>(std::round(qBound(0.0, x_n, 1.0) * (rect.width() - 1)));
    const int y = rect.top() + static_cast<int>(std::round(qBound(0.0, y_n, 1.0) * (rect.height() - 1)));
    const bool reliable = dwFlags != QStringLiteral("move");
    if (!g_windowsInputBrokerMode &&
        (reliable ? sendToBrokerReliable(mousePixelMessage(button, x, y, mouseData, dwFlags, reliableMoveBoundary))
                  : sendToBroker(mousePixelMessage(button, x, y, mouseData, dwFlags, reliableMoveBoundary))))
        return;
    if (!directInputFallbackAllowed())
        return;
    sendMouseEventAtPixel(button, x, y, mouseData, dwFlags);
}

void InputUtil::execMouseEventOnDesktopSource(int button, qreal x_n, qreal y_n, int mouseData, const QString &dwFlags,
                                              int desktopSourceIndex, bool reliableMoveBoundary)
{
    const QRect rect = physicalMonitorRectForDesktopSource(desktopSourceIndex).value_or(monitorRectForIndex(desktopSourceIndex));
    execMouseEventInRect(button, x_n, y_n, mouseData, dwFlags, rect, reliableMoveBoundary);
}

bool InputUtil::prepareWindowsInputBroker(QString *errorMessage)
{
    std::lock_guard<std::recursive_mutex> operationLock(g_unattendedServiceOperationMutex);
    if (g_windowsInputBrokerMode)
        return true;
    const bool ok = ensureBrokerReady();
    if (ok)
        inputBrokerClient().start(currentBrokerEndpoint());
    if (!ok && errorMessage)
        *errorMessage = InputUtil::tr("Windows input broker is not available. Remote keyboard and mouse control may not work on the current desktop.");
    return ok;
}

QString InputUtil::windowsInputBrokerServerName()
{
    return unattendedServerName();
}

bool InputUtil::authenticateWindowsInputBrokerRequest(QJsonObject *request)
{
    if (!request)
        return false;
    const BrokerEndpoint endpoint = currentBrokerEndpoint();
    if (endpoint.serverName.isEmpty() || endpoint.token.isEmpty())
        return false;
    request->insert(QStringLiteral("token"), endpoint.token);
    return true;
}

bool InputUtil::isWindowsUnattendedInputInstalled()
{
    return isAiranServiceInstalled();
}

bool InputUtil::isWindowsUnattendedInputUpdateRequired(QString *reason)
{
    if (reason)
        reason->clear();
    if (!isAiranServiceInstalled())
        return false;

    QString actualPath;
    QString error;
    if (!queryAiranServiceBinaryPath(&actualPath, &error))
    {
        if (reason)
            *reason = error;
        return true;
    }

    const QString expectedPrefix = expectedServiceCommandPrefix();
    const QString normalizedActual = normalizedServiceBinaryPath(actualPath);
    const QString normalizedPrefix = normalizedServiceBinaryPath(expectedPrefix);
    const QString brokerToken = serviceBrokerTokenFromCommand(actualPath);
    const QString expected = expectedServiceBinaryPath(QCoreApplication::applicationFilePath(), brokerToken);
    if (!normalizedActual.startsWith(normalizedPrefix) || brokerToken.isEmpty() ||
        normalizedActual != normalizedServiceBinaryPath(expected))
    {
        if (reason)
        {
            *reason = QCoreApplication::translate("InputUtil", "Installed service command is out of date.\nCurrent: %1\nExpected: %2")
                          .arg(actualPath, expected);
        }
        return true;
    }

    const qint64 serviceStartedMs = runningAiranServiceProcessCreationMs();
    const qint64 exeModifiedMs = QFileInfo(QCoreApplication::applicationFilePath()).lastModified().toMSecsSinceEpoch();
    if (serviceStartedMs > 0 && exeModifiedMs > serviceStartedMs + 2000)
    {
        if (reason)
        {
            *reason = QCoreApplication::translate("InputUtil", "Installed service process is older than the current executable.");
        }
        return true;
    }
    return false;
}

bool InputUtil::ensureWindowsUnattendedInputServiceReady(QString *errorMessage)
{
    std::lock_guard<std::recursive_mutex> operationLock(g_unattendedServiceOperationMutex);
    if (windowsReadinessCancelled())
        return false;
    if (errorMessage)
        errorMessage->clear();
    if (!isAiranServiceInstalled())
    {
        if (errorMessage)
            *errorMessage = InputUtil::tr("Airan Desk Windows service is not installed. The remote side cannot capture or control the Windows lock screen, and Ctrl+Alt+Del is unavailable.");
        return false;
    }

    QString updateReason;
    if (isWindowsUnattendedInputUpdateRequired(&updateReason))
    {
        LOG_INFO("Airan service update required: {}", updateReason);
        return installWindowsUnattendedInput(errorMessage);
    }

    g_lastServiceStartAttemptMs.store(0);
    if (waitForUnattendedBrokerProtocol(300))
        return true;

    const DWORD initialServiceState = queryAiranServiceState();
    if (initialServiceState == SERVICE_START_PENDING || initialServiceState == SERVICE_RUNNING)
    {
        LOG_INFO("Airan service is starting; waiting for the interactive input broker");
        if (waitForUnattendedBrokerProtocol(10000))
            return true;
    }

    const bool started = tryStartUnattendedInputService();
    if (!started && queryAiranServiceState() == SERVICE_STOPPED)
    {
        LOG_INFO("Airan service is stopped; requesting elevation to start it");
        if (!runElevatedSelfAndWait(QStringLiteral("--airan-start-service-elevated"), errorMessage))
            return false;
        g_lastServiceStartAttemptMs.store(0);
    }

    if (waitForUnattendedBrokerProtocol(5000))
        return true;

    LOG_WARN("Installed Airan service did not provide a compatible input broker, repairing service");
    return installWindowsUnattendedInput(errorMessage);
}

bool InputUtil::installWindowsUnattendedInput(QString *errorMessage)
{
    std::lock_guard<std::recursive_mutex> operationLock(g_unattendedServiceOperationMutex);
    if (windowsReadinessCancelled())
        return false;
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QString directError;
    if (installAiranServiceDirect(exe, &directError))
        return waitForUnattendedBrokerProtocol(5000);

    LOG_WARN("Direct Airan service install failed, requesting elevation: {}", directError);
    if (!runElevatedSelfAndWait(QStringLiteral("--airan-install-service-elevated"), errorMessage))
        return false;
    g_lastServiceStartAttemptMs.store(0);
    tryStartUnattendedInputService();
    if (waitForUnattendedBrokerProtocol(10000))
        return true;
    if (errorMessage)
        *errorMessage = InputUtil::tr("Airan Desk Windows service was installed, but the input broker did not become ready. Lock-screen capture/control and Ctrl+Alt+Del may be unavailable until the service is restarted or repaired.");
    return false;
}

bool InputUtil::uninstallWindowsUnattendedInput(QString *errorMessage)
{
    std::lock_guard<std::recursive_mutex> operationLock(g_unattendedServiceOperationMutex);
    shutdownWindowsInputBroker();
    QString directError;
    if (uninstallAiranServiceDirect(&directError))
        return true;

    LOG_WARN("Direct Airan service uninstall failed, requesting elevation: {}", directError);
    return runElevatedSelfAndWait(QStringLiteral("--airan-uninstall-service-elevated"), errorMessage);
}

bool InputUtil::stopWindowsUnattendedInputService(QString *errorMessage)
{
    std::lock_guard<std::recursive_mutex> operationLock(g_unattendedServiceOperationMutex);
    if (g_windowsInputBrokerMode)
        return true;

    const QString brokerToken = queryAiranServiceBrokerToken();
    const BrokerEndpoint serviceControl{serviceControlServerName(), brokerToken, BrokerKind::Unattended};
    if (sendBrokerControlMessage(serviceControl, QStringLiteral("stopService"), true))
        return true;

    const BrokerEndpoint unattended{unattendedServerName(), brokerToken, BrokerKind::Unattended};
    if (sendBrokerControlMessage(unattended, QStringLiteral("stopService"), true))
        return true;

    QString directError;
    if (stopAiranServiceDirect(&directError))
        return true;

    if (errorMessage)
        *errorMessage = directError;
    LOG_WARN("Failed to stop Windows unattended input service: {}", directError);
    return false;
}

void InputUtil::setWindowsSessionLocked(bool locked)
{
    g_windowsSessionLocked.store(locked);
}

bool InputUtil::isWindowsSessionLocked()
{
    return g_windowsSessionLocked.load();
}

void InputUtil::shutdownWindowsInputBroker()
{
    if (g_windowsInputBrokerMode)
        return;

    const BrokerEndpoint endpoint = currentBrokerEndpoint();
    inputBrokerClient().stop();
    if (endpoint.kind == BrokerKind::Temporary && !endpoint.serverName.isEmpty())
        sendBrokerControlMessage(endpoint, QStringLiteral("quit"));
}

void InputUtil::cancelWindowsUnattendedInputReadiness()
{
    g_windowsReadinessCancelled.store(true, std::memory_order_release);
}

void InputUtil::resetWindowsUnattendedInputReadinessCancellation()
{
    g_windowsReadinessCancelled.store(false, std::memory_order_release);
}

int InputUtil::runWindowsInputBroker(int argc, char *argv[])
{
    g_windowsInputBrokerMode = true;
    QCoreApplication app(argc, argv);

    const QStringList args = app.arguments();
    QString serverName;
    QString token;
    bool unattended = false;
    bool serviceMode = false;
    for (int i = 0; i < args.size(); ++i)
    {
        if (args[i] == QStringLiteral("--server-name") && i + 1 < args.size())
            serverName = args[++i];
        else if (args[i] == QStringLiteral("--token") && i + 1 < args.size())
            token = args[++i];
        else if (args[i] == QStringLiteral("--unattended"))
            unattended = true;
        else if (args[i] == QStringLiteral("--service"))
            serviceMode = true;
    }

    if (unattended && serverName.isEmpty())
        serverName = unattendedServerName();

    if (serverName.isEmpty() || token.isEmpty())
        return 2;

    AppRuntime::initLog();
    LOG_DEBUG("Windows input broker token: {}", currentProcessTokenSummary());

    HANDLE singletonMutex = nullptr;
    if (unattended)
    {
        bool alreadyExists = false;
        singletonMutex = createBrokerSingletonMutex(serverName, &alreadyExists);
        if (!singletonMutex)
        {
            LOG_WARN("Windows input broker failed to create singleton mutex for {}: {}", serverName, GetLastError());
        }
        else if (alreadyExists)
        {
            LOG_WARN("Windows input broker already running for {}, exiting duplicate broker", serverName);
            CloseHandle(singletonMutex);
            return 0;
        }
    }

    QLocalServer::removeServer(serverName);
    QLocalServer server;
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    server.setSocketOptions(unattended ? QLocalServer::WorldAccessOption
                                       : QLocalServer::UserAccessOption);
#endif
    if (!server.listen(serverName))
    {
        LOG_ERROR("Windows input broker failed to listen on {}: {}", serverName, server.errorString());
        if (singletonMutex)
        {
            ReleaseMutex(singletonMutex);
            CloseHandle(singletonMutex);
        }
        return 3;
    }

    QTimer idleTimer;
    idleTimer.setSingleShot(true);
    QObject::connect(&idleTimer, &QTimer::timeout, &app, &QCoreApplication::quit);
    if (!serviceMode)
        idleTimer.start(kBrokerIdleQuitMs);

    QObject::connect(&server, &QLocalServer::newConnection, [&server, &token, &idleTimer, serviceMode]() {
        while (QLocalSocket *socket = server.nextPendingConnection())
        {
            if (!isAuthorizedBrokerClient(socket))
            {
                socket->disconnectFromServer();
                socket->deleteLater();
                continue;
            }
            socket->setReadBufferSize(kMaxBrokerCommandBytes + 1);
            QObject::connect(socket, &QLocalSocket::readyRead, [socket, token, &idleTimer, serviceMode]() {
                while (true)
                {
                    const QByteArray prefix = socket->peek(kMaxBrokerCommandBytes + 1);
                    const int newline = prefix.indexOf('\n');
                    if (newline < 0)
                    {
                        if (prefix.size() > kMaxBrokerCommandBytes)
                        {
                            LOG_WARN("Windows input broker rejected oversized command");
                            socket->disconnectFromServer();
                        }
                        return;
                    }
                    // `peek()` already confirmed a complete line. Let Qt
                    // consume the full line; passing `newline + 1` as a
                    // max size can omit the newline/terminator and leave the
                    // same byte queued for an endless invalid-JSON loop.
                    const QByteArray line = socket->readLine().trimmed();
                    QJsonParseError error{};
                    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
                    if (error.error != QJsonParseError::NoError || !document.isObject())
                    {
                        LOG_WARN("Windows input broker ignored invalid JSON: {}", error.errorString());
                        // A malformed client stream must not monopolize the
                        // socket callback or flood the log. Drop the client
                        // and let it reconnect with a fresh protocol frame.
                        socket->disconnectFromServer();
                        return;
                    }
                    if (!serviceMode)
                        idleTimer.start(kBrokerIdleQuitMs);
                    if (handleBrokerMessage(document.object(), token, socket))
                        return;
                }
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
        }
    });

    LOG_INFO("Windows input broker is running");
    const int result = app.exec();
    server.close();
    QLocalServer::removeServer(serverName);
    if (singletonMutex)
    {
        ReleaseMutex(singletonMutex);
        CloseHandle(singletonMutex);
    }
    return result;
}

int InputUtil::runWindowsInputBrokerService(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    AppRuntime::initLog();

    QString brokerToken;
    const QStringList args = app.arguments();
    for (int i = 0; i + 1 < args.size(); ++i)
    {
        if (args[i] == QStringLiteral("--broker-token"))
        {
            brokerToken = args[i + 1];
            break;
        }
    }
    if (brokerToken.isEmpty())
    {
        LOG_ERROR("Windows input broker service started without an authentication token");
        return 2;
    }

    const QString controlName = serviceControlServerName();
    QLocalServer::removeServer(controlName);
    QLocalServer controlServer;
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    controlServer.setSocketOptions(QLocalServer::WorldAccessOption);
#endif
    if (!controlServer.listen(controlName))
        LOG_WARN("Windows input broker service control pipe failed to listen on {}: {}", controlName, controlServer.errorString());
    else
        LOG_INFO("Windows input broker service control pipe is running on {}", controlName);

    QObject::connect(&controlServer, &QLocalServer::newConnection, [&controlServer, &app, brokerToken]() {
        while (QLocalSocket *socket = controlServer.nextPendingConnection())
        {
            if (!isAuthorizedBrokerClient(socket))
            {
                socket->disconnectFromServer();
                socket->deleteLater();
                continue;
            }
            socket->setReadBufferSize(kMaxBrokerCommandBytes + 1);
            QObject::connect(socket, &QLocalSocket::readyRead, [socket, &app, brokerToken]() {
                while (true)
                {
                    const QByteArray prefix = socket->peek(kMaxBrokerCommandBytes + 1);
                    const int newline = prefix.indexOf('\n');
                    if (newline < 0)
                    {
                        if (prefix.size() > kMaxBrokerCommandBytes)
                        {
                            LOG_WARN("Windows input broker service rejected oversized command");
                            socket->disconnectFromServer();
                        }
                        return;
                    }
                    // `peek()` already confirmed a complete line. Let Qt
                    // consume the full line; passing `newline + 1` as a
                    // max size can omit the newline/terminator and leave the
                    // same byte queued for an endless invalid-JSON loop.
                    const QByteArray line = socket->readLine().trimmed();
                    QJsonParseError error{};
                    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
                    if (error.error != QJsonParseError::NoError || !document.isObject())
                    {
                        LOG_WARN("Windows input broker service ignored invalid control JSON: {}", error.errorString());
                        // Do not keep draining a malformed control stream:
                        // it can starve the event loop and hide stopService.
                        socket->disconnectFromServer();
                        return;
                    }

                    const QJsonObject object = document.object();
                    if (object.value(QStringLiteral("token")).toString() != brokerToken)
                    {
                        LOG_WARN("Windows input broker service rejected control command with invalid token");
                        socket->disconnectFromServer();
                        return;
                    }
                    const QString type = object.value(QStringLiteral("type")).toString();
                    if (type == QStringLiteral("ping"))
                    {
                        QJsonObject ack;
                        ack.insert(QStringLiteral("ok"), true);
                        ack.insert(QStringLiteral("type"), QStringLiteral("pong"));
                        ack.insert(QStringLiteral("version"), 1);
                        socket->write(QJsonDocument(ack).toJson(QJsonDocument::Compact) + '\n');
                        socket->waitForBytesWritten(200);
                    }
                    else if (type == QStringLiteral("stopService") || type == QStringLiteral("quit"))
                    {
                        sendBrokerAck(socket, true);
                        LOG_INFO("Windows input broker service stop requested through control pipe");
                        QTimer::singleShot(0, &app, &QCoreApplication::quit);
                        return;
                    }
                    else
                    {
                        sendBrokerAck(socket, false, QStringLiteral("Unknown service control command"));
                    }
                }
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
        }
    });

    const auto ensureBroker = [brokerToken]() {
        static std::map<DWORD, int> failureCounts;
        const std::vector<DWORD> sessions = brokerTargetSessionIds();
        if (sessions.empty())
        {
            LOG_WARN("Windows input broker service found no interactive target sessions");
            return;
        }
        for (DWORD sessionId : sessions)
        {
            const QString serverName = unattendedServerNameForSession(sessionId);
            if (probeBrokerProtocol(serverName, false, 2000, brokerToken))
            {
                failureCounts[sessionId] = 0;
                continue;
            }

            if (!brokerSingletonMutexExistsForSession(sessionId))
            {
                failureCounts[sessionId] = 0;
                startBrokerInSession(sessionId, brokerToken);
                continue;
            }

            const int failures = ++failureCounts[sessionId];
            LOG_WARN("Windows input broker in session {} did not answer watchdog probe ({}/3)", sessionId, failures);
            if (failures >= 3)
            {
                sendBrokerControlMessage({serverName, brokerToken, BrokerKind::Unattended}, QStringLiteral("quit"));
                QThread::msleep(300);
                terminateSystemBrokerProcessesInSession(sessionId);
                startBrokerInSession(sessionId, brokerToken);
                failureCounts[sessionId] = 0;
            }
        }
    };

    QTimer brokerWatchdog;
    QObject::connect(&brokerWatchdog, &QTimer::timeout, &app, ensureBroker);
    brokerWatchdog.start(10000);
    QTimer::singleShot(0, &app, ensureBroker);

    LOG_INFO("Windows input broker service manager is running");
    const int result = app.exec();
    controlServer.close();
    QLocalServer::removeServer(controlName);
    for (DWORD sessionId : brokerTargetSessionIds())
        sendBrokerControlMessage({unattendedServerNameForSession(sessionId), brokerToken, BrokerKind::Unattended},
                                 QStringLiteral("quit"));
    sendBrokerControlMessage({serviceControlServerName(), brokerToken, BrokerKind::Unattended},
                             QStringLiteral("quit"));
    return result;
}

int InputUtil::runWindowsServiceElevatedCommand(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    QString error;
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());

    auto argumentValue = [&args](const QString &name) {
        const int index = args.indexOf(name);
        return index >= 0 && index + 1 < args.size() ? args[index + 1] : QString();
    };

    if (args.contains(QStringLiteral("--airan-elevation-launcher")))
    {
        const QString operation = argumentValue(QStringLiteral("--airan-elevation-operation"));
        const QString cancelEventName = argumentValue(QStringLiteral("--airan-cancel-event"));
        const bool validOperation = operation == QStringLiteral("--airan-install-service-elevated") ||
                                    operation == QStringLiteral("--airan-start-service-elevated") ||
                                    operation == QStringLiteral("--airan-uninstall-service-elevated");
        if (!validOperation || !isValidElevationCancelEventName(cancelEventName))
            return ERROR_INVALID_PARAMETER;

        const std::wstring nativeCancelEventName = cancelEventName.toStdWString();
        HANDLE launcherCancelEvent = OpenEventW(SYNCHRONIZE, FALSE, nativeCancelEventName.c_str());
        if (!launcherCancelEvent)
            return ERROR_CANCELLED;
        const bool cancelled = WaitForSingleObject(launcherCancelEvent, 0) == WAIT_OBJECT_0;
        CloseHandle(launcherCancelEvent);
        if (cancelled)
            return ERROR_CANCELLED;

        const QString parameters = QStringLiteral("%1 --airan-cancel-event \"%2\"")
                                       .arg(operation, cancelEventName);
        return runElevatedTargetAndWait(exe, parameters, 30000, &error);
    }

    HANDLE cancelEvent = nullptr;
    const QString cancelEventName = argumentValue(QStringLiteral("--airan-cancel-event"));
    if (!cancelEventName.isEmpty())
    {
        if (!isValidElevationCancelEventName(cancelEventName))
            return ERROR_INVALID_PARAMETER;
        const std::wstring nativeName = cancelEventName.toStdWString();
        cancelEvent = OpenEventW(SYNCHRONIZE, FALSE, nativeName.c_str());
        if (!cancelEvent)
            return ERROR_CANCELLED;
        if (WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0)
        {
            CloseHandle(cancelEvent);
            return ERROR_CANCELLED;
        }
    }

    auto finish = [cancelEvent](int result) {
        if (cancelEvent)
            CloseHandle(cancelEvent);
        return result;
    };
    if (args.contains(QStringLiteral("--airan-install-service-elevated")))
    {
        return finish(installAiranServiceDirect(exe, &error) ? 0 : 1);
    }
    if (args.contains(QStringLiteral("--airan-start-service-elevated")))
    {
        return finish(startAiranService() ? 0 : 1);
    }
    if (args.contains(QStringLiteral("--airan-uninstall-service-elevated")))
    {
        return finish(uninstallAiranServiceDirect(&error) ? 0 : 1);
    }
    return finish(2);
}

#endif
