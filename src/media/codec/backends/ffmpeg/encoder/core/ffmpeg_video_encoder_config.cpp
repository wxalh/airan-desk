#include "media/codec/backends/ffmpeg/encoder/core/ffmpeg_video_encoder.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include "media/codec/airan_video_bitrate_profile.h"
#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_probe.h"
#include "media/codec/backends/ffmpeg/codec/ffmpeg_openh264_option.h"
#include "media/codec/backends/ffmpeg/encoder/util/ffmpeg_encoder_util.h"
#include "common/logger_manager.h"
#include "util/config/config_util.h"

#include <modules/video_coding/include/video_error_codes.h>
#include <rtc_base/logging.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace airan::media::ffmpeg
{
namespace
{
int encoderThreadCount(const webrtc::VideoEncoder::Settings &settings)
{
#if AIRAN_WEBRTC_MILESTONE >= 144
    return settings.encoder_thread_limit.value_or((std::max)(1, settings.number_of_cores));
#else
    return (std::max)(1, settings.number_of_cores);
#endif
}

bool validCodecDimensions(const webrtc::VideoCodec &settings)
{
    return settings.width > 0 && settings.height > 0;
}

bool backendContains(const CodecProbe *probe, const char *token)
{
    return probe && probe->backend && token && std::strstr(probe->backend, token) != nullptr;
}

bool isPlatformSystemMemoryHardwareProbe(const CodecProbe *probe)
{
    return backendContains(probe, "_mf") ||
           backendContains(probe, "_v4l2m2m") ||
           backendContains(probe, "_rkmpp");
}

bool isQsvProbe(const CodecProbe *probe)
{
    return probe && probe->deviceType == AV_HWDEVICE_TYPE_QSV;
}

bool isHardwareEncodeProbe(const CodecProbe *probe)
{
    return probe &&
           (probe->deviceType != AV_HWDEVICE_TYPE_NONE ||
            probe->hardwarePixelFormat != AV_PIX_FMT_NONE ||
            isPlatformSystemMemoryHardwareProbe(probe) ||
            (probe->zeroCopy && probe->zeroCopy[0] != '\0'));
}

bool isSoftwareEncodeProbe(const CodecProbe *probe)
{
    return probe && !isHardwareEncodeProbe(probe);
}

bool isNativeGpuEncodeProbe(const CodecProbe *probe)
{
    if (!isHardwareEncodeProbe(probe))
        return false;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    return probe->hardwarePixelFormat == AV_PIX_FMT_D3D11 ||
           probe->deviceType == AV_HWDEVICE_TYPE_QSV;
#else
    return probe->hardwarePixelFormat != AV_PIX_FMT_NONE &&
           probe->hardwarePixelFormat != AV_PIX_FMT_QSV;
#endif
}

const char *attemptStageName(FfmpegEncodeAttemptStage stage)
{
    switch (stage)
    {
    case FfmpegEncodeAttemptStage::NativeGpu:
        return "native-gpu";
    case FfmpegEncodeAttemptStage::CpuHardware:
        return "cpu-hardware";
    case FfmpegEncodeAttemptStage::Software:
        return "software";
    default:
        return "unknown";
    }
}

bool usesStrictCbrRateControl(const CodecProbe *probe)
{
    if (!probe)
        return false;
    return probe->deviceType == AV_HWDEVICE_TYPE_VAAPI ||
           probe->deviceType == AV_HWDEVICE_TYPE_D3D12VA ||
           probe->deviceType == AV_HWDEVICE_TYPE_VULKAN ||
           backendContains(probe, "_nvenc") ||
           backendContains(probe, "_amf") ||
           backendContains(probe, "_mf");
}

bool usesQsvH264Encoder(const CodecProbe *probe, CodecKind codec)
{
    return probe && probe->deviceType == AV_HWDEVICE_TYPE_QSV && codec == CodecKind::H264;
}

bool usesHealthyNetworkDesktopQualityBoost(const CodecProbe *probe,
                                           float packetLossRate,
                                           int64_t rttMs)
{
    return isHardwareEncodeProbe(probe) &&
           packetLossRate < 0.03f &&
           (rttMs <= 0 || rttMs < 150);
}

airan::media::DesktopVideoQualityProfile healthyNetworkDesktopProfile(const CodecProbe *probe,
                                                                      float packetLossRate,
                                                                      int64_t rttMs)
{
    if (!usesHealthyNetworkDesktopQualityBoost(probe, packetLossRate, rttMs))
        return airan::media::DesktopVideoQualityProfile::WeakClear;
    if (packetLossRate < 0.01f && (rttMs <= 0 || rttMs < 80))
        return airan::media::DesktopVideoQualityProfile::LanHd;
    return airan::media::DesktopVideoQualityProfile::WeakClear;
}

} // namespace

int FfmpegVideoEncoder::configuredTemporalLayers() const
{
    if (!m_hasCodecSettings)
        return 1;
    switch (m_codecSettings.codecType)
    {
    case webrtc::kVideoCodecH264:
        return (std::max)(1, static_cast<int>(m_codecSettings.H264().numberOfTemporalLayers));
    case webrtc::kVideoCodecVP8:
        return (std::max)(1, static_cast<int>(m_codecSettings.VP8().numberOfTemporalLayers));
    case webrtc::kVideoCodecVP9:
        return (std::max)(1, static_cast<int>(m_codecSettings.VP9().numberOfTemporalLayers));
    default:
        return 1;
    }
}

int FfmpegVideoEncoder::configuredSpatialLayers() const
{
    if (!m_hasCodecSettings)
        return 1;
    if (m_codecSettings.codecType == webrtc::kVideoCodecVP9)
        return (std::max)(1, static_cast<int>(m_codecSettings.VP9().numberOfSpatialLayers));
    return (std::max)(1, static_cast<int>(m_codecSettings.numberOfSimulcastStreams));
}

bool FfmpegVideoEncoder::hasHardwareEncodeProbe() const
{
    const std::vector<const CodecProbe *> probes =
        m_probe ? std::vector<const CodecProbe *>{m_probe}
                : (!m_probes.empty() ? m_probes : selectProbes(m_codec, true));
    for (const auto *probe : probes)
    {
        if (!probe)
            continue;
        if (probe->deviceType != AV_HWDEVICE_TYPE_NONE ||
            probe->hardwarePixelFormat != AV_PIX_FMT_NONE ||
            (probe->zeroCopy && probe->zeroCopy[0] != '\0') ||
            (probe->backend &&
             (std::strstr(probe->backend, "_mf") ||
              std::strstr(probe->backend, "_v4l2m2m") ||
              std::strstr(probe->backend, "_rkmpp"))))
            return true;
    }
    return false;
}

bool FfmpegVideoEncoder::hasNativeHandleEncodeProbe() const
{
    const std::vector<const CodecProbe *> probes =
        m_probe ? std::vector<const CodecProbe *>{m_probe}
                : (!m_probes.empty() ? m_probes : selectProbes(m_codec, true));
    for (const auto *probe : probes)
    {
        if (!probe)
            continue;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
        if (probe->hardwarePixelFormat == AV_PIX_FMT_D3D11 ||
            probe->deviceType == AV_HWDEVICE_TYPE_QSV)
            return true;
#endif
    }
    return false;
}

uint32_t FfmpegVideoEncoder::currentMaxBitrateBps() const
{
    uint32_t maxBitrate = m_codecSettings.maxBitrate > 0
                              ? codecBitrateBps(m_codecSettings.maxBitrate)
                              : 0;
    if (m_bandwidthAllocationBps > 0)
        maxBitrate = maxBitrate > 0 ? (std::min)(maxBitrate, m_bandwidthAllocationBps)
                                    : m_bandwidthAllocationBps;
    if (maxBitrate == 0)
        maxBitrate = m_targetBitrateBps > 0 ? m_targetBitrateBps : 2500000;
    if (m_codecSettings.width > 0 && m_codecSettings.height > 0)
    {
        const int fps = m_codecSettings.maxFramerate > 0 ? codecFramerateFps(m_codecSettings.maxFramerate) : 15;
        auto profile = healthyNetworkDesktopProfile(m_probe, m_packetLossRate, m_rttMs);
        auto desktopLimits = airan::media::desktopVideoBitrateLimits(
            m_codecSettings.width,
            m_codecSettings.height,
            fps,
            profile);
        if (profile == airan::media::DesktopVideoQualityProfile::LanHd &&
            m_bandwidthAllocationBps > 0 &&
            m_bandwidthAllocationBps < static_cast<uint32_t>(desktopLimits.start_bps))
        {
            desktopLimits = airan::media::desktopVideoBitrateLimits(
                m_codecSettings.width,
                m_codecSettings.height,
                fps,
                airan::media::DesktopVideoQualityProfile::WeakClear);
        }
        maxBitrate = (std::max)(maxBitrate, static_cast<uint32_t>(desktopLimits.min_bps));
        if (usesHealthyNetworkDesktopQualityBoost(m_probe, m_packetLossRate, m_rttMs))
        {
            maxBitrate = (std::max)(maxBitrate, static_cast<uint32_t>(desktopLimits.start_bps));
        }
    }
    return maxBitrate;
}

uint32_t FfmpegVideoEncoder::currentTargetBitrateBps() const
{
    uint32_t bitrate = m_adjustedBitrateAllocation.get_sum_bps();
    if (bitrate == 0)
        bitrate = m_targetBitrateAllocation.get_sum_bps();
    if (bitrate == 0)
        bitrate = m_targetBitrateBps > 0
                      ? m_targetBitrateBps
                      : (m_codecSettings.startBitrate > 0 ? codecBitrateBps(m_codecSettings.startBitrate) : 2500000);
    uint32_t minBitrate = m_codecSettings.minBitrate > 0 ? codecBitrateBps(m_codecSettings.minBitrate) : 0;
    if (m_codecSettings.width > 0 && m_codecSettings.height > 0)
    {
        const int fps = m_codecSettings.maxFramerate > 0 ? codecFramerateFps(m_codecSettings.maxFramerate) : 15;
        auto profile = healthyNetworkDesktopProfile(m_probe, m_packetLossRate, m_rttMs);
        auto desktopLimits = airan::media::desktopVideoBitrateLimits(
            m_codecSettings.width,
            m_codecSettings.height,
            fps,
            profile);
        if (profile == airan::media::DesktopVideoQualityProfile::LanHd &&
            m_bandwidthAllocationBps > 0 &&
            m_bandwidthAllocationBps < static_cast<uint32_t>(desktopLimits.start_bps))
        {
            desktopLimits = airan::media::desktopVideoBitrateLimits(
                m_codecSettings.width,
                m_codecSettings.height,
                fps,
                airan::media::DesktopVideoQualityProfile::WeakClear);
        }
        minBitrate = (std::max)(minBitrate, static_cast<uint32_t>(desktopLimits.min_bps));
        if (usesHealthyNetworkDesktopQualityBoost(m_probe, m_packetLossRate, m_rttMs))
        {
            minBitrate = (std::max)(minBitrate, static_cast<uint32_t>(desktopLimits.start_bps));
        }
    }
    const uint32_t maxBitrate = currentMaxBitrateBps();
    if (minBitrate > 0)
        bitrate = (std::max)(bitrate, minBitrate);
    if (maxBitrate > 0)
        bitrate = (std::min)(bitrate, maxBitrate);
    return bitrate;
}

int FfmpegVideoEncoder::currentMaxQp() const
{
    if (m_codecSettings.qpMax > 0)
        return static_cast<int>(m_codecSettings.qpMax);
    switch (m_codec)
    {
    case CodecKind::AV1:
        return 255;
    case CodecKind::VP9:
        return 63;
    case CodecKind::H264:
    case CodecKind::VP8:
    default:
        return 56;
    }
}


int FfmpegVideoEncoder::configureProbe(size_t probeIndex)
{
    if (!m_hasCodecSettings || probeIndex >= m_probes.size())
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;

    Release();

    m_probeIndex = probeIndex;
    m_probe = m_probes[probeIndex];
    if (!m_probe)
        return WEBRTC_VIDEO_CODEC_ERROR;
    if (m_attemptStage == FfmpegEncodeAttemptStage::NativeGpu && !isNativeGpuEncodeProbe(m_probe))
        return kAiranCodecTryNextBackend;
    if (m_attemptStage == FfmpegEncodeAttemptStage::CpuHardware && !isHardwareEncodeProbe(m_probe))
        return kAiranCodecTryNextBackend;
    if (m_attemptStage == FfmpegEncodeAttemptStage::Software && !isSoftwareEncodeProbe(m_probe))
        return kAiranCodecTryNextBackend;
    if (!probeRuntimeDependenciesAvailable(*m_probe))
    {
        LOG_WARN("Airan FFmpeg encoder runtime dependencies unavailable; backend={}",
                 m_probe->backend ? m_probe->backend : "");
        Release();
        return kAiranCodecTryNextBackend;
    }

    const AVCodec *codec = avcodec_find_encoder_by_name(m_probe->ffmpegCodec);
    if (!codec)
    {
        LOG_WARN("Airan FFmpeg encoder codec unavailable during configure; backend={}",
                 m_probe->backend ? m_probe->backend : "");
        Release();
        return kAiranCodecTryNextBackend;
    }

    const auto cleanupAttempt = [this]() {
        av_packet_free(&m_packet);
        av_buffer_unref(&m_hwFrames);
        av_buffer_unref(&m_hwDevice);
        avcodec_free_context(&m_ctx);
        m_nativeD3D11SessionBound = false;
    };

    const auto allocateContext = [this, codec](AVPixelFormat pixFmt) -> bool {
        if (!validCodecDimensions(m_codecSettings))
        {
            LOG_WARN("Airan FFmpeg encoder refused to allocate context with invalid dimensions: {}x{}",
                     m_codecSettings.width, m_codecSettings.height);
            return false;
        }

        m_ctx = avcodec_alloc_context3(codec);
        if (!m_ctx)
            return false;

        m_ctx->width = m_codecSettings.width;
        m_ctx->height = m_codecSettings.height;
        const int codecFps = codecFramerateFps(m_codecSettings.maxFramerate);
        m_ctx->time_base = AVRational{1, codecFps};
        m_ctx->pkt_timebase = m_ctx->time_base;
        m_ctx->framerate = AVRational{codecFps, 1};
        m_ctx->bit_rate = currentTargetBitrateBps();
        m_ctx->rc_min_rate = m_codecSettings.minBitrate > 0 ? codecBitrateBps(m_codecSettings.minBitrate) : 0;
        m_ctx->rc_max_rate = currentMaxBitrateBps();
        m_ctx->bit_rate_tolerance = static_cast<int>((std::max)(m_ctx->bit_rate / 2, int64_t{100000}));
        if (usesStrictCbrRateControl(m_probe))
        {
            m_ctx->rc_min_rate = static_cast<int>(m_ctx->bit_rate);
            m_ctx->rc_max_rate = static_cast<int>(m_ctx->bit_rate);
            m_ctx->rc_buffer_size = static_cast<int>((std::max<int64_t>)(m_ctx->bit_rate * 3 / 2, int64_t{750000}));
        }
        m_ctx->gop_size = (std::max)(30, codecFps * 2);
        m_ctx->max_b_frames = 0;
        m_ctx->qmax = currentMaxQp();
        if (m_probe && m_probe->deviceType == AV_HWDEVICE_TYPE_VAAPI && m_codec == CodecKind::H264)
            m_ctx->qmax = (std::min)(m_ctx->qmax, 51);
        if (usesQsvH264Encoder(m_probe, m_codec))
            m_ctx->qmax = (std::min)(m_ctx->qmax, 38);
        m_ctx->thread_count = encoderThreadCount(m_encoderSettings);
        m_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        m_ctx->pix_fmt = pixFmt;
        m_ctx->color_range = AVCOL_RANGE_MPEG;
        m_ctx->colorspace = AVCOL_SPC_BT709;
        m_ctx->color_primaries = AVCOL_PRI_BT709;
        m_ctx->color_trc = AVCOL_TRC_BT709;
        m_ctx->chroma_sample_location = AVCHROMA_LOC_LEFT;
        configureH264CodecContext(m_ctx);
        return true;
    };

    std::vector<AVPixelFormat> pixelFormats;
    const auto addPixelFormat = [&pixelFormats](AVPixelFormat format) {
        if (format == AV_PIX_FMT_NONE)
            return;
        if (std::find(pixelFormats.begin(), pixelFormats.end(), format) == pixelFormats.end())
            pixelFormats.push_back(format);
    };

    if (m_attemptStage == FfmpegEncodeAttemptStage::NativeGpu)
    {
        addPixelFormat(m_probe->hardwarePixelFormat);
    }
    else if (m_attemptStage == FfmpegEncodeAttemptStage::CpuHardware)
    {
        if (m_probe->softwarePixelFormat != AV_PIX_FMT_NONE &&
            encoderProbeAcceptsPixelFormat(*m_probe, m_probe->softwarePixelFormat))
            addPixelFormat(m_probe->softwarePixelFormat);

        if (!isPlatformSystemMemoryHardwareProbe(m_probe) && !isQsvProbe(m_probe))
            addPixelFormat(m_probe->hardwarePixelFormat);
    }
    else if (m_attemptStage == FfmpegEncodeAttemptStage::Software)
    {
        if (m_probe->softwarePixelFormat != AV_PIX_FMT_NONE &&
            encoderProbeAcceptsPixelFormat(*m_probe, m_probe->softwarePixelFormat))
            addPixelFormat(m_probe->softwarePixelFormat);
    }
    else
    {
        addPixelFormat(m_probe->hardwarePixelFormat);
    }
    if (pixelFormats.empty())
        return kAiranCodecTryNextBackend;

    for (const AVPixelFormat pixFmt : pixelFormats)
    {
        cleanupAttempt();
        if (!allocateContext(pixFmt))
        {
            Release();
            return WEBRTC_VIDEO_CODEC_MEMORY;
        }
        if (openCurrentContext())
        {
            if (pixFmt != m_probe->hardwarePixelFormat && m_probe->hardwarePixelFormat != AV_PIX_FMT_NONE)
            {
                LOG_INFO("Airan FFmpeg encoder opened with system-memory input; backend={}",
                         m_probe->backend ? m_probe->backend : "");
            }
            return WEBRTC_VIDEO_CODEC_OK;
        }
    }

    cleanupAttempt();
    m_fatalEncoderError = true;
    return kAiranCodecTryNextBackend;
}


bool FfmpegVideoEncoder::configureStage(FfmpegEncodeAttemptStage stage, size_t startProbeIndex, const char *reason)
{
    m_attemptStage = stage;
    for (size_t probeIndex = startProbeIndex; probeIndex < m_probes.size(); ++probeIndex)
    {
        const auto *probe = m_probes[probeIndex];
        if (stage == FfmpegEncodeAttemptStage::NativeGpu && !isNativeGpuEncodeProbe(probe))
            continue;
        if (stage == FfmpegEncodeAttemptStage::CpuHardware && !isHardwareEncodeProbe(probe))
            continue;
        if (stage == FfmpegEncodeAttemptStage::Software && !isSoftwareEncodeProbe(probe))
            continue;

        LOG_INFO("Airan FFmpeg encoder trying probe; stage={}, backend={}, reason={}",
                 attemptStageName(stage),
                 probe && probe->backend ? probe->backend : "",
                 reason ? reason : "initial");
        const int result = configureProbe(probeIndex);
        if (result == WEBRTC_VIDEO_CODEC_OK)
            return true;
        if (result == WEBRTC_VIDEO_CODEC_MEMORY)
            return false;
    }
    return false;
}


bool FfmpegVideoEncoder::shouldDelayNativeSessionOpen() const
{
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    return m_probe && m_ctx &&
           (m_probe->hardwarePixelFormat == AV_PIX_FMT_D3D11 || m_probe->deviceType == AV_HWDEVICE_TYPE_QSV) &&
           m_probe->softwarePixelFormat != AV_PIX_FMT_NONE &&
           m_ctx->pix_fmt == m_probe->softwarePixelFormat;
#else
    return false;
#endif
}


bool FfmpegVideoEncoder::openCurrentContext()
{
    if (!m_ctx || !m_probe)
        return false;
    if (m_ctx->width <= 0 || m_ctx->height <= 0)
    {
        LOG_WARN("Airan FFmpeg encoder refused to open invalid context; backend={}, size={}x{}",
                 m_probe->backend ? m_probe->backend : "",
                 m_ctx->width, m_ctx->height);
        return false;
    }

    const bool wantsHardwareFrames =
        m_probe->hardwarePixelFormat != AV_PIX_FMT_NONE && m_ctx->pix_fmt == m_probe->hardwarePixelFormat;
    if (wantsHardwareFrames && !setupHardwareFrames())
    {
        LOG_WARN("Airan FFmpeg encoder hardware frame setup failed; backend={}",
                 m_probe->backend ? m_probe->backend : "");
        return false;
    }

    QString openh264Reason;
    if (!configureOpenH264LibraryOption(
            *m_probe, m_ctx, ConfigUtil->openh264_enabled,
            ConfigUtil->openh264_library_path, api().optSet, &openh264Reason))
    {
        LOG_WARN("Airan FFmpeg encoder optional OpenH264 probe unavailable; backend={}, reason={}",
                 m_probe->backend ? m_probe->backend : "",
                 openh264Reason.toStdString());
        return false;
    }
    configureRealtimeEncoderOptions(m_ctx, m_probe);
    const AVCodec *codec = avcodec_find_encoder_by_name(m_probe->ffmpegCodec);
    if (!codec)
        return false;
    const int openResult = avcodec_open2(m_ctx, codec, nullptr);
    if (openResult < 0)
    {
        LOG_WARN("Airan FFmpeg encoder open failed; backend={}, pix_fmt={}, error={}",
                 m_probe->backend ? m_probe->backend : "",
                 static_cast<int>(m_ctx->pix_fmt),
                 ffmpegErrorText(openResult));
        return false;
    }

    m_packet = av_packet_alloc();
    if (!m_packet)
        return false;

    m_nextPts = 0;
    m_fatalEncoderError = false;
    if (shouldDelayNativeSessionOpen())
    {
        LOG_DEBUG("Airan FFmpeg native D3D11 encoder session will bind on first texture frame; backend={}",
                  m_probe->backend ? m_probe->backend : "");
    }
    return true;
}


bool FfmpegVideoEncoder::advanceProbe(const char *reason)
{
    const auto previousStage = m_attemptStage;
    if (configureStage(m_attemptStage, m_probeIndex + 1, reason))
        return true;

    if (previousStage == FfmpegEncodeAttemptStage::NativeGpu &&
        configureStage(FfmpegEncodeAttemptStage::CpuHardware, 0, "native gpu path exhausted"))
    {
        return true;
    }
    if ((previousStage == FfmpegEncodeAttemptStage::NativeGpu ||
         previousStage == FfmpegEncodeAttemptStage::CpuHardware) &&
        configureStage(FfmpegEncodeAttemptStage::Software, 0, "hardware path exhausted"))
    {
        return true;
    }

    Release();
    m_probe = nullptr;
    m_probeIndex = m_probes.size();
    m_fatalEncoderError = true;
    LOG_WARN("Airan FFmpeg encoder exhausted all probes; reason={}", reason ? reason : "unknown");
    return false;
}

} // namespace airan::media::ffmpeg

#endif
