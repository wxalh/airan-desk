#include "media/codec/backends/ffmpeg/decoder/ffmpeg_video_decoder.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_probe.h"
#include "media/codec/backends/ffmpeg/codec/ffmpeg_openh264_option.h"
#include "common/logger_manager.h"
#include "util/config/config_util.h"

namespace airan::media::ffmpeg
{


bool FfmpegVideoDecoder::configureProbe(size_t probeIndex)
{
    if (probeIndex >= m_probes.size())
        return false;

    sws_freeContext(m_sws);
    m_sws = nullptr;
    av_packet_free(&m_packet);
    av_buffer_unref(&m_hwDevice);
    avcodec_free_context(&m_ctx);

    m_probeIndex = probeIndex;
    m_probe = m_probes[probeIndex];
    m_fatalDecoderError = false;
    if (!probeRuntimeDependenciesAvailable(*m_probe))
    {
        LOG_WARN("FFmpeg decoder probe {} runtime dependencies unavailable; trying next backend", m_probe->backend);
        m_probe = nullptr;
        return false;
    }

    const AVCodec *codec = avcodec_find_decoder_by_name(m_probe->ffmpegCodec);
    if (!codec)
        return false;
    m_ctx = avcodec_alloc_context3(codec);
    if (!m_ctx)
        return false;
    m_ctx->opaque = this;
    m_ctx->get_format = &FfmpegVideoDecoder::getFormat;
    if (m_probe->deviceType != AV_HWDEVICE_TYPE_NONE &&
        av_hwdevice_ctx_create(&m_hwDevice, m_probe->deviceType, nullptr, nullptr, 0) >= 0)
    {
        m_ctx->hw_device_ctx = av_buffer_ref(m_hwDevice);
    }
    if (m_probe->deviceType != AV_HWDEVICE_TYPE_NONE && !m_ctx->hw_device_ctx)
    {
        LOG_WARN("FFmpeg decoder probe {} has no hardware device; trying next backend", m_probe->backend);
        av_buffer_unref(&m_hwDevice);
        avcodec_free_context(&m_ctx);
        m_probe = nullptr;
        return false;
    }
    QString openh264Reason;
    if (!configureOpenH264LibraryOption(
            *m_probe, m_ctx, ConfigUtil->openh264_enabled,
            ConfigUtil->openh264_library_path, api().optSet, &openh264Reason))
    {
        LOG_WARN("FFmpeg decoder optional OpenH264 probe unavailable; backend={}, reason={}",
                 m_probe->backend ? m_probe->backend : "",
                 openh264Reason.toStdString());
        av_buffer_unref(&m_hwDevice);
        avcodec_free_context(&m_ctx);
        m_probe = nullptr;
        return false;
    }
    if (avcodec_open2(m_ctx, codec, nullptr) < 0)
    {
        LOG_WARN("FFmpeg decoder probe {} failed to open; trying next backend", m_probe->backend);
        av_buffer_unref(&m_hwDevice);
        avcodec_free_context(&m_ctx);
        m_probe = nullptr;
        return false;
    }
    m_packet = av_packet_alloc();
    if (!m_packet)
    {
        av_buffer_unref(&m_hwDevice);
        avcodec_free_context(&m_ctx);
        m_probe = nullptr;
        return false;
    }
    LOG_INFO("FFmpeg decoder using backend {}", m_probe->backend);
    return true;
}


bool FfmpegVideoDecoder::advanceProbe(const char *reason)
{
    if (m_probe)
    {
        LOG_WARN("FFmpeg decoder backend {} failed during decode: {}", m_probe->backend, reason);
    }

    for (size_t next = m_probeIndex + 1; next < m_probes.size(); ++next)
    {
        if (configureProbe(next))
            return true;
    }

    Release();
    m_probe = nullptr;
    m_probeIndex = m_probes.size();
    m_fatalDecoderError = true;
    return false;
}


AVPixelFormat FfmpegVideoDecoder::getFormat(AVCodecContext *ctx, const AVPixelFormat *formats)
{
    if (!formats)
        return AV_PIX_FMT_NONE;
    auto *self = static_cast<FfmpegVideoDecoder *>(ctx ? ctx->opaque : nullptr);
    if (self && self->m_probe)
    {
        for (const AVPixelFormat *fmt = formats; *fmt != AV_PIX_FMT_NONE; ++fmt)
            if (*fmt == self->m_probe->hardwarePixelFormat)
                return *fmt;
        if (self->m_probe->hardwarePixelFormat != AV_PIX_FMT_NONE)
            return AV_PIX_FMT_NONE;
    }
    return formats[0] == AV_PIX_FMT_NONE ? AV_PIX_FMT_NONE : formats[0];
}

} // namespace airan::media::ffmpeg

#endif
