#include "media/codec/backends/ffmpeg/encoder/core/ffmpeg_video_encoder.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include "media/codec/backends/ffmpeg/encoder/util/ffmpeg_encoder_util.h"
#include "common/logger_manager.h"

#include <rtc_base/logging.h>

namespace airan::media::ffmpeg
{


bool FfmpegVideoEncoder::setupHardwareFrames()
{
    if (!m_probe || m_probe->deviceType == AV_HWDEVICE_TYPE_NONE || m_probe->hardwarePixelFormat == AV_PIX_FMT_NONE)
        return true;
    if (m_ctx->pix_fmt != m_probe->hardwarePixelFormat)
        return true;
    if (m_ctx->width <= 0 || m_ctx->height <= 0)
    {
        LOG_WARN("Airan FFmpeg hardware frame setup refused invalid size; backend={}, size={}x{}",
                 m_probe->backend ? m_probe->backend : "",
                 m_ctx->width, m_ctx->height);
        return false;
    }
    const int deviceResult = av_hwdevice_ctx_create(&m_hwDevice, m_probe->deviceType, nullptr, nullptr, 0);
    if (deviceResult < 0)
    {
        LOG_WARN("Airan FFmpeg hardware device setup failed; backend={}, device={}, error={}",
                 m_probe->backend ? m_probe->backend : "",
                 static_cast<int>(m_probe->deviceType),
                 ffmpegErrorText(deviceResult));
        return false;
    }
    m_hwFrames = av_hwframe_ctx_alloc(m_hwDevice);
    if (!m_hwFrames)
    {
        LOG_WARN("Airan FFmpeg hardware frame context alloc failed; backend={}",
                 m_probe->backend ? m_probe->backend : "");
        return false;
    }
    auto *frames = reinterpret_cast<AVHWFramesContext *>(m_hwFrames->data);
    frames->format = m_probe->hardwarePixelFormat;
    frames->sw_format = m_probe->softwarePixelFormat;
    frames->width = m_ctx->width;
    frames->height = m_ctx->height;
    frames->initial_pool_size = m_probe->deviceType == AV_HWDEVICE_TYPE_QSV ? 16 : 8;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    if (m_probe->hardwarePixelFormat == AV_PIX_FMT_D3D11)
    {
        auto *d3d11Frames = reinterpret_cast<AVD3D11VAFramesContext *>(frames->hwctx);
        if (d3d11Frames)
        {
            d3d11Frames->BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_VIDEO_ENCODER;
            d3d11Frames->MiscFlags = 0;
        }
    }
#endif
    m_ctx->sw_pix_fmt = frames->sw_format;
    const int framesResult = av_hwframe_ctx_init(m_hwFrames);
    if (framesResult < 0)
    {
        LOG_WARN("Airan FFmpeg hardware frame context init failed; backend={}, format={}, sw_format={}, size={}x{}, error={}",
                 m_probe->backend ? m_probe->backend : "",
                 static_cast<int>(frames->format),
                 static_cast<int>(frames->sw_format),
                 frames->width, frames->height,
                 ffmpegErrorText(framesResult));
        return false;
    }
    m_ctx->hw_device_ctx = av_buffer_ref(m_hwDevice);
    m_ctx->hw_frames_ctx = av_buffer_ref(m_hwFrames);
    return m_ctx->hw_device_ctx && m_ctx->hw_frames_ctx;
}

} // namespace airan::media::ffmpeg

#endif
