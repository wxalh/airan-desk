#include "input_util.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#elif defined(Q_OS_LINUX)
#include <cerrno>
#include <cstring>
#include <sys/reboot.h>
#include <unistd.h>
#endif

bool InputUtil::execPowerOperation(bool restart, QString *errorMessage)
{
    if (errorMessage)
        errorMessage->clear();

#if defined(Q_OS_WIN)
    const auto fail = [errorMessage](DWORD code) {
        if (errorMessage)
            *errorMessage = tr("The system power operation failed (Windows error %1).").arg(code);
        return false;
    };
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return fail(GetLastError());

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    if (!LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &privileges.Privileges[0].Luid))
    {
        const DWORD error = GetLastError();
        CloseHandle(token);
        return fail(error);
    }
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    TOKEN_PRIVILEGES previous{};
    DWORD previousSize = sizeof(previous);
    SetLastError(ERROR_SUCCESS);
    const BOOL adjusted = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(previous), &previous, &previousSize);
    const DWORD privilegeError = GetLastError();
    if (!adjusted || privilegeError != ERROR_SUCCESS)
    {
        CloseHandle(token);
        return fail(privilegeError);
    }

    // Request an orderly shutdown and allow applications to save their work.
    const BOOL accepted = InitiateSystemShutdownExW(nullptr, nullptr, 0, FALSE, restart ? TRUE : FALSE,
                                                    SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_FLAG_PLANNED);
    const DWORD error = accepted ? ERROR_SUCCESS : GetLastError();
    AdjustTokenPrivileges(token, FALSE, &previous, 0, nullptr, nullptr);
    CloseHandle(token);
    return accepted ? true : fail(error);
#elif defined(Q_OS_MACOS)
    const ProcessSerialNumber systemProcess = {0, kSystemProcess};
    AEAddressDesc target = {typeNull, nullptr};
    AppleEvent event = {typeNull, nullptr};
    AppleEvent reply = {typeNull, nullptr};
    OSStatus status = AECreateDesc(typeProcessSerialNumber, &systemProcess, sizeof(systemProcess), &target);
    if (status == noErr)
        status = AECreateAppleEvent(kCoreEventClass, restart ? kAERestart : kAEShutDown, &target,
                                    kAutoGenerateReturnID, kAnyTransactionID, &event);
    if (status == noErr)
        status = AESendMessage(&event, &reply, kAEWaitReply | kAENeverInteract, 10 * 60);
    if (status == noErr)
    {
        SInt32 replyError = noErr;
        const OSStatus readStatus = AEGetParamPtr(&reply, keyErrorNumber, typeSInt32, nullptr,
                                                  &replyError, sizeof(replyError), nullptr);
        if (readStatus == noErr)
            status = replyError;
        else if (readStatus != errAEDescNotFound)
            status = readStatus;
    }
    AEDisposeDesc(&reply);
    AEDisposeDesc(&event);
    AEDisposeDesc(&target);
    if (status == noErr)
        return true;
    if (errorMessage)
        *errorMessage = tr("The system power operation failed (macOS error %1).").arg(status);
    return false;
#elif defined(Q_OS_LINUX)
    // The kernel API requires CAP_SYS_BOOT (normally a root service).
    // Flush dirty filesystem buffers before asking the kernel to stop.
    ::sync();
    if (::reboot(restart ? RB_AUTOBOOT : RB_POWER_OFF) == 0)
        return true;
    const int error = errno;
    if (errorMessage)
        *errorMessage = tr("The system power operation failed: %1 (errno %2). Root or CAP_SYS_BOOT is required.")
                            .arg(QString::fromLocal8Bit(std::strerror(error))).arg(error);
    return false;
#else
    Q_UNUSED(restart);
    if (errorMessage)
        *errorMessage = tr("System power operations are not supported on this platform.");
    return false;
#endif
}
