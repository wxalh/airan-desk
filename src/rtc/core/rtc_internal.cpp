#include "rtc/core/rtc_internal.h"

#include <mutex>
#include <utility>

/*
 * Aggregates declarations for RTC internals. Implementations live in focused
 * translation units such as rtc_runtime.cpp, rtc_frame_conversion.cpp, codec
 * preferences, SDP conversion, and Airan desktop capture sources.
 */

namespace rtc
{
namespace
{

std::mutex g_desktopCaptureBackendMutex;
std::string g_desktopCaptureBackend;

} // namespace

std::string currentDesktopCaptureBackend()
{
    std::lock_guard<std::mutex> lock(g_desktopCaptureBackendMutex);
    return g_desktopCaptureBackend;
}

void setDesktopCaptureBackend(std::string backend)
{
    std::lock_guard<std::mutex> lock(g_desktopCaptureBackendMutex);
    g_desktopCaptureBackend = std::move(backend);
}

void resetDesktopCaptureBackend()
{
    setDesktopCaptureBackend(std::string());
}

} // namespace rtc
