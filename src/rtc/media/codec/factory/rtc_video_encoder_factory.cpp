#include "rtc/peer/factory/rtc_peer_connection_factories.h"

#include "media/codec/webrtc/airan_codec_adapter.h"
#include "common/logger_manager.h"

#if defined(WIN32)
#include <windows.h>
#endif

namespace rtc
{
namespace
{

#if defined(WIN32)

bool canLoadSystemLibrary(const wchar_t *name)
{
    HMODULE module = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module)
        module = LoadLibraryW(name);
    if (!module)
        return false;
    FreeLibrary(module);
    return true;
}
#endif


void probeHardwareCodecRuntime()
{
#if defined(WIN32)
    LOG_INFO("Windows hardware codec runtime probe: NVENC={}, AMF={}, IntelQSV/VPL={}/{}/{}",
             canLoadSystemLibrary(L"nvEncodeAPI64.dll"),
             canLoadSystemLibrary(L"amfrt64.dll"),
             canLoadSystemLibrary(L"libvpl.dll"),
             canLoadSystemLibrary(L"libmfx64.dll"),
             canLoadSystemLibrary(L"libmfxhw64.dll"));
#elif defined(__APPLE__)
    LOG_INFO("Apple hardware codec runtime is provided by Airan VideoToolbox backends when available");
#elif defined(__linux__)
    LOG_INFO("Linux hardware codec runtime is provided by Airan VAAPI/V4L2 backends when available");
#endif
}

} // namespace

std::unique_ptr<webrtc::VideoEncoderFactory> createAiranVideoEncoderFactory()
{
    probeHardwareCodecRuntime();
    LOG_INFO("Creating Airan video encoder factory; platform software fallback is enabled when configured by the media backend");
    return airan::media::createAiranVideoEncoderFactory();
}

} // namespace rtc
