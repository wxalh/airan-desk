#include "runtime_environment.h"

#include <QCoreApplication>

#include <atomic>

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace
{
std::atomic_bool g_uiAvailable{false};
}

bool RuntimeEnvironment::detectInteractiveUi()
{
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    DWORD sessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId) || sessionId == 0)
        return false;

    HWINSTA windowStation = GetProcessWindowStation();
    USEROBJECTFLAGS flags{};
    DWORD needed = 0;
    if (!windowStation ||
        !GetUserObjectInformationW(windowStation, UOI_FLAGS, &flags, sizeof(flags), &needed))
    {
        return false;
    }
    return (flags.dwFlags & WSF_VISIBLE) != 0;
#elif defined(Q_OS_MACOS)
    CFDictionaryRef session = CGSessionCopyCurrentDictionary();
    const bool available = session != nullptr;
    if (session)
        CFRelease(session);
    return available;
#else
    return !qEnvironmentVariableIsEmpty("DISPLAY") ||
           !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
#endif
}

void RuntimeEnvironment::setDetectedUiAvailability(bool available)
{
    g_uiAvailable.store(available);
}

bool RuntimeEnvironment::uiAvailable()
{
    return g_uiAvailable.load();
}
