#include "desktop_grab.h"
#include "desktop_grab_qt.h"
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#if !defined(AIRAN_COMPATIBLE_WIN7)
#include "desktop_grab_dxgi.h"
#include "desktop_grab_wgc.h"

namespace {

struct WinVersion
{
    DWORD major{0};
    DWORD minor{0};
};

WinVersion queryWindowsVersion()
{
    WinVersion v{};
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
    {
        return v;
    }

    using RtlGetVersionFn = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);
    auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (!rtlGetVersion)
    {
        return v;
    }

    RTL_OSVERSIONINFOW osvi{};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    if (rtlGetVersion(&osvi) == 0)
    {
        v.major = osvi.dwMajorVersion;
        v.minor = osvi.dwMinorVersion;
    }
    return v;
}

bool isWin10OrGreater(const WinVersion &v)
{
    return (v.major > 10) || (v.major == 10);
}

bool isWin8OrGreater(const WinVersion &v)
{
    return (v.major > 6) || (v.major == 6 && v.minor >= 2);
}

} // namespace
#endif
#endif

std::shared_ptr<DesktopGrab> DesktopGrab::createBestDesktopGrabber(int screenIndex, QObject *parent)
{
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#if !defined(AIRAN_COMPATIBLE_WIN7)
    const WinVersion winVer = queryWindowsVersion();
    LOG_INFO("DesktopGrab backend select by runtime OS version: {}.{}", winVer.major, winVer.minor);

    if (isWin10OrGreater(winVer))
    {
        auto wgcGrabber = std::make_shared<DesktopGrabWGC>(parent);
        if (wgcGrabber && wgcGrabber->init(screenIndex))
        {
            return wgcGrabber;
        }
        auto dxgiGrabber = std::make_shared<DesktopGrabDXGI>();
        if (dxgiGrabber && dxgiGrabber->init(screenIndex))
        {
            return dxgiGrabber;
        }
    }
    else if (isWin8OrGreater(winVer))
    {
        auto dxgiGrabber = std::make_shared<DesktopGrabDXGI>();
        if (dxgiGrabber && dxgiGrabber->init(screenIndex))
        {
            return dxgiGrabber;
        }
    }
    else
    {
        LOG_INFO("Windows version below 8 detected, skip WGC/DXGI and use Qt grabber");
    }
#endif
#endif

    // 如果 DXGI 不可用或非 Windows，使用 Qt 实现
    auto qtGrabber = std::make_shared<DesktopGrabQt>(parent);
    if (qtGrabber && qtGrabber->init(screenIndex))
    {
        return qtGrabber;
    }

    // 两者都不可用，返回 nullptr
    return nullptr;
}