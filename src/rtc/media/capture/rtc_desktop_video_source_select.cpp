#include "rtc/media/capture/rtc_desktop_video_source_select.h"

#include "common/logger_manager.h"
#include "media/capture/core/airan_capture_frame.h"
#include "rtc/media/capture/airan_desktop_capture_source.h"

#include <stdexcept>

namespace rtc
{
namespace
{

scoped_refptr<DesktopVideoSource> createNativeDesktopVideoSource(const Description::Video &desc)
{
    scoped_refptr<DesktopVideoSource> source;
    bool available = false;
    try
    {
        available = isAiranDesktopCaptureSourceAvailable();
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Desktop capture availability probe failed: {}", e.what());
        return nullptr;
    }
    catch (...)
    {
        LOG_ERROR("Desktop capture availability probe failed with unknown exception");
        return nullptr;
    }

    if (available)
    {
        try
        {
            source = createAiranDesktopCaptureSource(desc);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Airan desktop capture source creation failed: {}", e.what());
            return nullptr;
        }
        catch (...)
        {
            LOG_ERROR("Airan desktop capture source creation failed with unknown exception");
            return nullptr;
        }
        if (source)
        {
            setDesktopCaptureBackend("AiranWebRtcForkDesktopCapture");
            const auto capabilities = source->captureCapabilities();
            LOG_INFO("Selected Airan WebRTC-derived desktop capture source: requestedIndex={}, hasSourceId={}, sourceId={}, preferredPath={}, nativeGpu={}",
                     desc.desktopSourceIndex(),
                     desc.hasDesktopSourceId(),
                     desc.desktopSourceId(),
                     airan::media::toString(capabilities.preferred_capture_path),
                     capabilities.native_gpu);
        }
    }

    return source;
}

} // namespace

scoped_refptr<DesktopVideoSource> createDesktopVideoSourceForTrack(const Description::Video &desc)
{
    resetDesktopCaptureBackend();
    scoped_refptr<DesktopVideoSource> source = createNativeDesktopVideoSource(desc);
    if (!source)
    {
        setDesktopCaptureBackend("Unavailable");
        LOG_ERROR("No Airan desktop capture source is available: requestedIndex={}, hasSourceId={}, sourceId={}",
                  desc.desktopSourceIndex(),
                  desc.hasDesktopSourceId(),
                  desc.desktopSourceId());
    }
    return source;
}

} // namespace rtc
