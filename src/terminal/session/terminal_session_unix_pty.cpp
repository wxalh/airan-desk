#include "terminal_session.h"
#include "common/logger_manager.h"

#if !defined(Q_OS_WIN)

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#if defined(Q_OS_MACOS)
#include <util.h>
#else
#include <pty.h>
#endif

extern char **environ;

namespace
{
struct UnixAccountInfo
{
    QByteArray userName;
    QByteArray homePath;
    QByteArray shellPath;
};

UnixAccountInfo currentUnixAccountInfo()
{
    UnixAccountInfo account;
    long bufferSize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufferSize < 1024)
        bufferSize = 16 * 1024;

    for (int attempt = 0; attempt < 4 && bufferSize <= 1024 * 1024; ++attempt)
    {
        std::vector<char> buffer(static_cast<size_t>(bufferSize));
        passwd entry{};
        passwd *result = nullptr;
        const int status = getpwuid_r(geteuid(), &entry, buffer.data(), buffer.size(), &result);
        if (status == 0 && result)
        {
            if (entry.pw_name)
                account.userName = QByteArray(entry.pw_name);
            if (entry.pw_dir)
                account.homePath = QByteArray(entry.pw_dir);
            if (entry.pw_shell)
                account.shellPath = QByteArray(entry.pw_shell);
            break;
        }
        if (status != ERANGE)
            break;
        bufferSize *= 2;
    }
    return account;
}
} // namespace

bool TerminalSession::startPty(int cols, int rows)
{
    const UnixAccountInfo account = currentUnixAccountInfo();
    QString shellPath;
#if defined(Q_OS_LINUX)
    const QString preferredBashPath = QStringLiteral("/bin/bash");
    if (QFileInfo(preferredBashPath).isExecutable())
        shellPath = preferredBashPath;
#endif
    if (shellPath.isEmpty())
        shellPath = QFile::decodeName(account.shellPath).trimmed();
    if (shellPath.isEmpty() || !QFileInfo(shellPath).isAbsolute() || !QFileInfo(shellPath).isExecutable())
        shellPath = defaultShell().trimmed();
    if (!QFileInfo(shellPath).isAbsolute())
        shellPath = QStandardPaths::findExecutable(shellPath);
    const QString canonicalShellPath = QFileInfo(shellPath).canonicalFilePath();
    if (!canonicalShellPath.isEmpty() &&
        QFileInfo(canonicalShellPath).fileName().compare(QStringLiteral("bash"), Qt::CaseInsensitive) == 0)
    {
        shellPath = canonicalShellPath;
    }
    const QByteArray shell = QFile::encodeName(shellPath);
    if (shell.isEmpty() || !QFileInfo(shellPath).isExecutable())
    {
        emit errorOccurred(QStringLiteral("The configured shell is not executable: %1").arg(defaultShell()));
        return false;
    }
    m_activeShellPath = shellPath;

    std::vector<QByteArray> environmentStorage;
    const QByteArray inheritedPromptCommand = qgetenv("PROMPT_COMMAND");
    const QByteArray libraryPathMarker = qgetenv("AIRAN_DESK_LD_LIBRARY_PATH_WAS_SET");
    const bool hasLibraryPathMarker = libraryPathMarker == "0" || libraryPathMarker == "1";
    const QByteArray originalLibraryPath = qgetenv("AIRAN_DESK_ORIGINAL_LD_LIBRARY_PATH");
    const bool hasAccountHome = !account.homePath.isEmpty();
    const bool hasAccountUser = !account.userName.isEmpty();
    for (char **entry = environ; entry && *entry; ++entry)
    {
        const QByteArray value(*entry);
        if (!value.startsWith("TERM=") &&
            !value.startsWith("SHELL=") &&
            !(hasAccountHome && value.startsWith("HOME=")) &&
            !(hasAccountUser && value.startsWith("USER=")) &&
            !(hasAccountUser && value.startsWith("LOGNAME=")) &&
            !value.startsWith("PROMPT_COMMAND=") &&
            !value.startsWith("AIRAN_DESK_LD_LIBRARY_PATH_WAS_SET=") &&
            !value.startsWith("AIRAN_DESK_ORIGINAL_LD_LIBRARY_PATH=") &&
            !(hasLibraryPathMarker && value.startsWith("LD_LIBRARY_PATH=")))
        {
            environmentStorage.push_back(value);
        }
    }
    if (libraryPathMarker == "1")
        environmentStorage.push_back(QByteArray("LD_LIBRARY_PATH=") + originalLibraryPath);
    if (hasAccountHome)
        environmentStorage.push_back(QByteArray("HOME=") + account.homePath);
    if (hasAccountUser)
    {
        environmentStorage.push_back(QByteArray("USER=") + account.userName);
        environmentStorage.push_back(QByteArray("LOGNAME=") + account.userName);
    }
    environmentStorage.emplace_back("TERM=xterm-256color");
    environmentStorage.push_back(QByteArray("SHELL=") + shell);
    const bool isBash = QFileInfo(shellPath).fileName().compare(QStringLiteral("bash"), Qt::CaseInsensitive) == 0;
    int bashRcPipe[2] = {-1, -1};
    QByteArray bashRcFilePath;
    if (isBash)
    {
        if (::pipe(bashRcPipe) != 0)
        {
            emit errorOccurred(QStringLiteral("Failed to create pseudo terminal: %1").arg(QString::fromLocal8Bit(strerror(errno))));
            return false;
        }

        QByteArray promptCommand = "printf '\\033]7;file://localhost%s\\007' \"$PWD\"";
        if (!inheritedPromptCommand.trimmed().isEmpty())
            promptCommand += QByteArray("; ") + inheritedPromptCommand;
        environmentStorage.push_back(QByteArray("AIRAN_DESK_PATH_PROMPT_COMMAND=") + promptCommand);
        const QByteArray bashRcFileContent =
            QByteArray("exec ") + QByteArray::number(bashRcPipe[0]) + "<&-; " +
            "if [ -f /etc/profile ]; then source /etc/profile; fi; "
            "if [ -f \"$HOME/.bashrc\" ]; then source \"$HOME/.bashrc\"; fi; "
            "if [ -n \"$PROMPT_COMMAND\" ]; then "
                "PROMPT_COMMAND=\"$PROMPT_COMMAND; $AIRAN_DESK_PATH_PROMPT_COMMAND\"; "
            "else "
                "PROMPT_COMMAND=\"$AIRAN_DESK_PATH_PROMPT_COMMAND\"; "
            "fi; "
            "unset AIRAN_DESK_PATH_PROMPT_COMMAND";

        int offset = 0;
        while (offset < bashRcFileContent.size())
        {
            const ssize_t written = ::write(bashRcPipe[1],
                                            bashRcFileContent.constData() + offset,
                                            static_cast<size_t>(bashRcFileContent.size() - offset));
            if (written > 0)
            {
                offset += static_cast<int>(written);
                continue;
            }
            if (written < 0 && errno == EINTR)
                continue;

            const int writeError = errno;
            ::close(bashRcPipe[0]);
            ::close(bashRcPipe[1]);
            emit errorOccurred(QStringLiteral("Failed to create pseudo terminal: %1")
                                   .arg(QString::fromLocal8Bit(strerror(writeError))));
            return false;
        }
        ::close(bashRcPipe[1]);
        bashRcPipe[1] = -1;
#if defined(Q_OS_MACOS)
        bashRcFilePath = QByteArray("/dev/fd/") + QByteArray::number(bashRcPipe[0]);
#else
        bashRcFilePath = QByteArray("/proc/self/fd/") + QByteArray::number(bashRcPipe[0]);
#endif
    }

    std::vector<char *> environment;
    environment.reserve(environmentStorage.size() + 1);
    for (QByteArray &value : environmentStorage)
        environment.push_back(value.data());
    environment.push_back(nullptr);
    std::vector<QByteArray> argumentStorage;
    argumentStorage.push_back(shell);
    if (isBash)
    {
        argumentStorage.emplace_back("--noprofile");
        argumentStorage.emplace_back("--rcfile");
        argumentStorage.push_back(bashRcFilePath);
        argumentStorage.emplace_back("-i");
    }
    std::vector<char *> arguments;
    arguments.reserve(argumentStorage.size() + 1);
    for (QByteArray &value : argumentStorage)
        arguments.push_back(value.data());
    arguments.push_back(nullptr);

    struct winsize ws{};
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);

    pid_t pid = forkpty(&m_masterFd, nullptr, nullptr, &ws);
    if (pid < 0)
    {
        const int forkError = errno;
        if (bashRcPipe[0] >= 0)
            ::close(bashRcPipe[0]);
        emit errorOccurred(QStringLiteral("Failed to create pseudo terminal: %1")
                               .arg(QString::fromLocal8Bit(strerror(forkError))));
        return false;
    }

    if (pid == 0)
    {
        execve(shell.constData(), arguments.data(), environment.data());
        _exit(127);
    }

    if (bashRcPipe[0] >= 0)
        ::close(bashRcPipe[0]);

    m_childPid = pid;
    int flags = fcntl(m_masterFd, F_GETFL, 0);
    fcntl(m_masterFd, F_SETFL, flags | O_NONBLOCK);

    m_notifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &TerminalSession::onPtyReadyRead);
    m_writeNotifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &TerminalSession::flushPtyInput);

    return true;
}

void TerminalSession::flushPtyInput()
{
    if (m_masterFd < 0 || m_pendingInput.isEmpty())
    {
        if (m_writeNotifier)
            m_writeNotifier->setEnabled(false);
        return;
    }

    while (!m_pendingInput.isEmpty())
    {
        const ssize_t written = ::write(m_masterFd,
                                        m_pendingInput.constData(),
                                        static_cast<size_t>(m_pendingInput.size()));
        if (written > 0)
        {
            m_pendingInput.remove(0, static_cast<int>(written));
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            if (m_writeNotifier)
                m_writeNotifier->setEnabled(true);
            return;
        }

        LOG_WARN("Failed to write terminal input to pty: {}", QString::fromLocal8Bit(strerror(errno)));
        m_pendingInput.clear();
        break;
    }

    if (m_writeNotifier)
        m_writeNotifier->setEnabled(false);
}

void TerminalSession::onPtyReadyRead()
{
    if (m_stopping.load() || m_outputPaused.load())
        return;
    QByteArray buffer(8192, Qt::Uninitialized);
    for (;;)
    {
        ssize_t n = ::read(m_masterFd, buffer.data(), static_cast<size_t>(buffer.size()));
        if (n > 0)
        {
            emit outputReady(buffer.left(static_cast<int>(n)));
            if (m_outputPaused.load())
                break;
            continue;
        }
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
        {
            if (m_notifier)
            {
                m_notifier->setEnabled(false);
                m_notifier->deleteLater();
                m_notifier = nullptr;
            }
            if (m_writeNotifier)
            {
                m_writeNotifier->setEnabled(false);
                m_writeNotifier->deleteLater();
                m_writeNotifier = nullptr;
            }
            m_pendingInput.clear();
            if (m_masterFd >= 0)
            {
                ::close(m_masterFd);
                m_masterFd = -1;
            }

            int status = 0;
            int exitCode = 0;
            if (m_childPid > 0 && waitpid(static_cast<pid_t>(m_childPid), &status, WNOHANG) > 0)
            {
                if (WIFEXITED(status))
                    exitCode = WEXITSTATUS(status);
                else if (WIFSIGNALED(status))
                    exitCode = 128 + WTERMSIG(status);
                m_childPid = -1;
            }
            else if (m_childPid > 0)
            {
                reapChildProcessNonBlocking();
            }
            emitClosedOnce(exitCode);
        }
        break;
    }
}

void TerminalSession::reapChildProcessNonBlocking()
{
    if (m_childPid <= 0)
        return;

    for (int i = 0; i < 10; ++i)
    {
        int status = 0;
        const pid_t result = ::waitpid(static_cast<pid_t>(m_childPid), &status, WNOHANG);
        if (result == static_cast<pid_t>(m_childPid) || result < 0)
        {
            m_childPid = -1;
            return;
        }
        QThread::msleep(20);
    }

    ::kill(static_cast<pid_t>(m_childPid), SIGKILL);
    int status = 0;
    while (::waitpid(static_cast<pid_t>(m_childPid), &status, 0) < 0 && errno == EINTR)
    {
    }
    m_childPid = -1;
}

#endif
