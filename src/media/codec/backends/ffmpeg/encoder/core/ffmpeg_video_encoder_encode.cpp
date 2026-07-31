#include "media/codec/backends/ffmpeg/encoder/core/ffmpeg_video_encoder.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include "media/codec/airan_video_bitrate_profile.h"
#include "media/codec/backends/ffmpeg/encoder/util/ffmpeg_encoder_util.h"
#include "common/logger_manager.h"

#include <api/video/encoded_image.h>
#include <modules/video_coding/include/video_error_codes.h>
#include <rtc_base/logging.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace airan::media::ffmpeg
{
namespace
{
uint32_t frameRtpTimestamp(const webrtc::VideoFrame &frame)
{
#if AIRAN_WEBRTC_MILESTONE >= 144
    return frame.rtp_timestamp();
#else
    return frame.timestamp();
#endif
}

void setEncodedImageRtpTimestamp(webrtc::EncodedImage &image, uint32_t timestamp)
{
#if AIRAN_WEBRTC_MILESTONE >= 144
    image.SetRtpTimestamp(timestamp);
#else
    image.SetTimestamp(timestamp);
#endif
}

void setEncodedImageFrameType(webrtc::EncodedImage &image, webrtc::VideoFrameType frameType)
{
#if AIRAN_WEBRTC_MILESTONE >= 144
    image.SetFrameType(frameType);
#else
    image._frameType = frameType;
#endif
}

bool backendContains(const CodecProbe *probe, const char *token)
{
    return probe && probe->backend && token && std::strstr(probe->backend, token) != nullptr;
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

bool isPlatformSystemMemoryHardwareProbe(const CodecProbe *probe)
{
    return backendContains(probe, "_mf") ||
           backendContains(probe, "_v4l2m2m") ||
           backendContains(probe, "_rkmpp");
}

bool isHardwareEncodeProbe(const CodecProbe *probe)
{
    return probe &&
           (probe->deviceType != AV_HWDEVICE_TYPE_NONE ||
            probe->hardwarePixelFormat != AV_PIX_FMT_NONE ||
            isPlatformSystemMemoryHardwareProbe(probe) ||
            (probe->zeroCopy && probe->zeroCopy[0] != '\0'));
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


int32_t FfmpegVideoEncoder::encodeFrame(const webrtc::VideoFrame &frame,
                                        const std::vector<webrtc::VideoFrameType> *frameTypes,
                                        AVFrame *inputFrame)
{
    if (!m_ctx || !m_callback || !inputFrame || !m_packet)
        return WEBRTC_VIDEO_CODEC_ERROR;

    inputFrame->pts = m_nextPts++;
    const bool keyFrameRequested =
        (frameTypes && std::find(frameTypes->begin(), frameTypes->end(), webrtc::VideoFrameType::kVideoFrameKey) != frameTypes->end()) ||
        m_forceKeyFrameOnResume ||
        m_forceKeyFrameForRecovery;
    if (keyFrameRequested)
    {
        inputFrame->pict_type = AV_PICTURE_TYPE_I;
        m_forceKeyFrameOnResume = false;
        m_forceKeyFrameForRecovery = false;
    }

    const int sendResult = safeAvcodecSendFrame(m_ctx, inputFrame, m_probe ? m_probe->backend : nullptr);
    if (sendResult < 0)
    {
        LOG_WARN("Airan FFmpeg avcodec_send_frame failed; backend={}, ctx_pix_fmt={}, frame_format={}, "
                 "ctx_size={}x{}, frame_size={}x{}, ctx_hw_frames={}, frame_hw_frames={}, error={}",
                 m_probe ? m_probe->backend : "",
                 static_cast<int>(m_ctx->pix_fmt),
                 inputFrame->format,
                 m_ctx->width, m_ctx->height,
                 inputFrame->width, inputFrame->height,
                 m_ctx->hw_frames_ctx ? 1 : 0,
                 inputFrame->hw_frames_ctx ? 1 : 0,
                 ffmpegErrorText(sendResult));
        m_fatalEncoderError = true;
        return kAiranCodecTryNextBackend;
    }

    for (;;)
    {
        const int receiveResult = safeAvcodecReceivePacket(m_ctx, m_packet, m_probe ? m_probe->backend : nullptr);
        if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF)
            break;
        if (receiveResult < 0)
        {
            LOG_WARN("Airan FFmpeg avcodec_receive_packet failed; backend={}, error={}",
                     m_probe ? m_probe->backend : "",
                     ffmpegErrorText(receiveResult));
            m_fatalEncoderError = true;
            return kAiranCodecTryNextBackend;
        }

        const bool packetIsKeyFrame = (m_packet->flags & AV_PKT_FLAG_KEY) != 0;
        const std::vector<uint8_t> encoded = m_codec == CodecKind::H264
                                                 ? encodedPacketData(m_ctx, m_packet, packetIsKeyFrame)
                                                 : std::vector<uint8_t>(m_packet->data, m_packet->data + m_packet->size);
        if (encoded.empty())
        {
            av_packet_unref(m_packet);
            continue;
        }

        webrtc::EncodedImage image;
        image.SetEncodedData(webrtc::EncodedImageBuffer::Create(encoded.data(), encoded.size()));
        setEncodedImageRtpTimestamp(image, frameRtpTimestamp(frame));
        image.ntp_time_ms_ = frame.ntp_time_ms();
        image.capture_time_ms_ = frame.timestamp_us() / 1000;
        image._encodedWidth = static_cast<uint32_t>(m_ctx->width);
        image._encodedHeight = static_cast<uint32_t>(m_ctx->height);
        image.SetSpatialIndex(0);
        image.SetTemporalIndex(0);
        image.SetSpatialLayerFrameSize(0, encoded.size());
        image.qp_ = -1;
        setEncodedImageFrameType(image, packetIsKeyFrame ? webrtc::VideoFrameType::kVideoFrameKey : webrtc::VideoFrameType::kVideoFrameDelta);

        webrtc::CodecSpecificInfo codecInfo;
        codecInfo.codecType = webrtcCodecType(m_codec);
#if AIRAN_WEBRTC_MILESTONE >= 144
        codecInfo.scalability_mode = webrtc::ScalabilityMode::kL1T1;
#endif
        if (m_codec == CodecKind::H264)
        {
            codecInfo.codecSpecific.H264.packetization_mode = m_packetizationMode;
            codecInfo.codecSpecific.H264.temporal_idx = 0;
            codecInfo.codecSpecific.H264.base_layer_sync = packetIsKeyFrame;
            codecInfo.codecSpecific.H264.idr_frame = packetIsKeyFrame;
        }
        else if (m_codec == CodecKind::VP8)
        {
            codecInfo.codecSpecific.VP8.temporalIdx = 0;
            codecInfo.codecSpecific.VP8.layerSync = packetIsKeyFrame;
            codecInfo.codecSpecific.VP8.keyIdx = -1;
        }
        else if (m_codec == CodecKind::VP9)
        {
            codecInfo.codecSpecific.VP9.first_frame_in_picture = true;
            codecInfo.codecSpecific.VP9.inter_pic_predicted = !packetIsKeyFrame;
            codecInfo.codecSpecific.VP9.flexible_mode = false;
            codecInfo.codecSpecific.VP9.ss_data_available = packetIsKeyFrame;
            codecInfo.codecSpecific.VP9.temporal_idx = 0;
            codecInfo.codecSpecific.VP9.temporal_up_switch = true;
            codecInfo.codecSpecific.VP9.inter_layer_predicted = false;
            codecInfo.codecSpecific.VP9.gof_idx = 0;
            codecInfo.codecSpecific.VP9.num_spatial_layers = 1;
            codecInfo.codecSpecific.VP9.first_active_layer = 0;
            codecInfo.codecSpecific.VP9.spatial_layer_resolution_present = true;
            codecInfo.codecSpecific.VP9.width[0] = static_cast<uint16_t>(m_ctx->width);
            codecInfo.codecSpecific.VP9.height[0] = static_cast<uint16_t>(m_ctx->height);
            codecInfo.codecSpecific.VP9.num_ref_pics = packetIsKeyFrame ? 0 : 1;
            if (!packetIsKeyFrame)
                codecInfo.codecSpecific.VP9.p_diff[0] = 1;
            codecInfo.codecSpecific.VP9.gof.SetGofInfoVP9(webrtc::kTemporalStructureMode1);
        }
        const auto callbackResult = m_callback->OnEncodedImage(image, &codecInfo);
        if (callbackResult.drop_next_frame)
            m_dropNextFrameByCallback = true;
        if (callbackResult.error != webrtc::EncodedImageCallback::Result::OK)
        {
            LOG_WARN("Airan FFmpeg encoded image callback failed; backend={}",
                     m_probe ? m_probe->backend : "");
            av_packet_unref(m_packet);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
        ++m_encodedFrameCount;
        av_packet_unref(m_packet);
    }
    return WEBRTC_VIDEO_CODEC_OK;
}


void FfmpegVideoEncoder::SetRates(const RateControlParameters &parameters)
{
    m_targetBitrateAllocation = parameters.target_bitrate;
    m_adjustedBitrateAllocation = parameters.bitrate;
    const uint32_t bitrateBps = parameters.bitrate.get_sum_bps();
    const bool wasPaused = m_mediaPaused;
    m_targetBitrateBps = bitrateBps;
    m_bandwidthAllocationBps = static_cast<uint32_t>((std::max<int64_t>)(0, parameters.bandwidth_allocation.bps_or(0)));
    m_targetFramerateFps = std::isfinite(parameters.framerate_fps) && parameters.framerate_fps > 0.0
                                ? (std::min)(parameters.framerate_fps, 1000.0)
                                : static_cast<double>(codecFramerateFps(m_codecSettings.maxFramerate));
    m_mediaPaused = bitrateBps == 0;

    if (m_mediaPaused)
    {
        LOG_INFO("Airan FFmpeg encoder paused by WebRTC zero target bitrate");
        return;
    }

    if (wasPaused)
    {
        m_forceKeyFrameOnResume = true;
        LOG_INFO("Airan FFmpeg encoder resumed; next output frame will be key frame");
    }

    if (m_ctx)
    {
        m_ctx->bit_rate = currentTargetBitrateBps();
        m_ctx->rc_min_rate = m_codecSettings.minBitrate > 0 ? codecBitrateBps(m_codecSettings.minBitrate) : 0;
        m_ctx->rc_max_rate = currentMaxBitrateBps();
        m_ctx->bit_rate_tolerance = static_cast<int>((std::max)(m_ctx->bit_rate / 2, int64_t{100000}));
        if (m_codecSettings.width > 0 &&
            m_codecSettings.height > 0 &&
            usesHealthyNetworkDesktopQualityBoost(m_probe, m_packetLossRate, m_rttMs))
        {
            const int fps = m_targetFramerateFps > 0.0
                                ? static_cast<int>((std::max)(1.0, m_targetFramerateFps))
                                : codecFramerateFps(m_codecSettings.maxFramerate);
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
            m_ctx->rc_min_rate = (std::max<int64_t>)(m_ctx->rc_min_rate, desktopLimits.start_bps);
            m_ctx->rc_max_rate = (std::max<int64_t>)(m_ctx->rc_max_rate, desktopLimits.start_bps);
            m_ctx->bit_rate = (std::max<int64_t>)(m_ctx->bit_rate, desktopLimits.start_bps);
        }
        if (usesStrictCbrRateControl(m_probe))
        {
            m_ctx->rc_min_rate = static_cast<int>(m_ctx->bit_rate);
            m_ctx->rc_max_rate = static_cast<int>(m_ctx->bit_rate);
            m_ctx->rc_buffer_size = static_cast<int>((std::max<int64_t>)(m_ctx->bit_rate / 2, int64_t{500000}));
        }
        if (m_targetFramerateFps > 0.0)
        {
            m_ctx->time_base = AVRational{1, static_cast<int>((std::max)(1.0, m_targetFramerateFps))};
            m_ctx->pkt_timebase = m_ctx->time_base;
            m_ctx->framerate = AVRational{static_cast<int>((std::max)(1.0, m_targetFramerateFps)), 1};
        }
    }
}

void FfmpegVideoEncoder::OnPacketLossRateUpdate(float packet_loss_rate)
{
    m_packetLossRate = packet_loss_rate;
    if (packet_loss_rate >= 0.10f)
        m_forceKeyFrameForRecovery = true;
    LOG_DEBUG("Airan FFmpeg encoder packet loss update: lossRate={}", packet_loss_rate);
}

void FfmpegVideoEncoder::OnRttUpdate(int64_t rtt_ms)
{
    m_rttMs = rtt_ms;
    LOG_DEBUG("Airan FFmpeg encoder RTT update: rttMs={}", rtt_ms);
}

void FfmpegVideoEncoder::OnLossNotification(const LossNotification &loss_notification)
{
    const bool needsRecovery =
        loss_notification.last_received_decodable.has_value() && !*loss_notification.last_received_decodable;
    const bool dependenciesLost =
        loss_notification.dependencies_of_last_received_decodable.has_value() &&
        !*loss_notification.dependencies_of_last_received_decodable;
    if (needsRecovery || dependenciesLost)
        m_forceKeyFrameForRecovery = true;
    LOG_DEBUG("Airan FFmpeg encoder loss notification: lastReceived={}, lastDecodable={}, recoveryKeyFrame={}",
              loss_notification.timestamp_of_last_received,
              loss_notification.timestamp_of_last_decodable,
              m_forceKeyFrameForRecovery ? 1 : 0);
}


webrtc::VideoEncoder::EncoderInfo FfmpegVideoEncoder::GetEncoderInfo() const
{
    EncoderInfo info;
    info.implementation_name = m_probe ? std::string("FFmpeg ") + m_probe->backend : std::string("FFmpeg ") + codecName(m_codec);
    info.is_hardware_accelerated = hasHardwareEncodeProbe();
    info.supports_native_handle = hasNativeHandleEncodeProbe();
    info.has_trusted_rate_controller = false;
    info.requested_resolution_alignment = 2;
    info.apply_alignment_to_all_simulcast_layers = true;
    info.scaling_settings = ScalingSettings::kOff;
    info.supports_simulcast = false;
    if (info.supports_native_handle)
        info.preferred_pixel_formats.push_back(webrtc::VideoFrameBuffer::Type::kNative);
    info.preferred_pixel_formats.push_back(webrtc::VideoFrameBuffer::Type::kI420);
    info.is_qp_trusted = false;
    if (m_codecSettings.width > 0 && m_codecSettings.height > 0)
    {
#if AIRAN_WEBRTC_MILESTONE >= 144
        info.mapped_resolution = webrtc::VideoEncoder::Resolution(m_codecSettings.width, m_codecSettings.height);
#endif
        const int configuredPixels = static_cast<int>((std::min<int64_t>)(
            static_cast<int64_t>((std::numeric_limits<int>::max)()),
            static_cast<int64_t>(m_codecSettings.width) * static_cast<int64_t>(m_codecSettings.height)));
        const int configuredFps = m_codecSettings.maxFramerate > 0 ? codecFramerateFps(m_codecSettings.maxFramerate) : 60;
        const auto configuredLimits = airan::media::desktopVideoBitrateLimits(
            m_codecSettings.width,
            m_codecSettings.height,
            configuredFps,
            airan::media::DesktopVideoQualityProfile::LanHd);
        const uint32_t codecMaxBitrateBps = m_codecSettings.maxBitrate > 0
                                                ? codecBitrateBps(m_codecSettings.maxBitrate)
                                                : 0;
        const int configuredStart = static_cast<int>(
            m_codecSettings.startBitrate > 0 ? codecBitrateBps(m_codecSettings.startBitrate) : configuredLimits.start_bps);
        const int configuredMin = static_cast<int>(
            m_codecSettings.minBitrate > 0 ? codecBitrateBps(m_codecSettings.minBitrate) : configuredLimits.min_bps);
        const int configuredMax = static_cast<int>(
            codecMaxBitrateBps > 0 ? codecMaxBitrateBps : configuredLimits.max_bps);

        const auto addLimit = [&info, configuredMin, configuredMax](
                                  int pixels,
                                  int startBps,
                                  int minBps,
                                  int maxBps) {
            if (pixels <= 0)
                return;
            const int boundedMax = configuredMax > 0 ? (std::min)(maxBps, configuredMax) : maxBps;
            const int boundedMin = (std::min)((std::max)(minBps, configuredMin), boundedMax);
            const int boundedStart = (std::min)((std::max)(startBps, boundedMin), boundedMax);
            info.resolution_bitrate_limits.emplace_back(pixels, boundedStart, boundedMin, boundedMax);
        };

        const auto addComputedLimit = [&addLimit, configuredFps](int width, int height) {
            const auto limits = airan::media::desktopVideoBitrateLimits(
                width,
                height,
                configuredFps,
                airan::media::DesktopVideoQualityProfile::LanHd);
            addLimit(width * height, limits.start_bps, limits.min_bps, limits.max_bps);
        };

        addComputedLimit(320, 180);
        addComputedLimit(640, 360);
        addComputedLimit(1280, 720);
        addComputedLimit(1920, 1080);
        addComputedLimit(2560, 1440);
        addComputedLimit(3840, 2160);
        if (configuredPixels != 320 * 180 &&
            configuredPixels != 640 * 360 &&
            configuredPixels != 1280 * 720 &&
            configuredPixels != 1920 * 1080 &&
            configuredPixels != 2560 * 1440 &&
            configuredPixels != 3840 * 2160)
        {
            addLimit(configuredPixels, configuredStart, configuredMin, configuredMax);
        }
    }
    return info;
}

} // namespace airan::media::ffmpeg

#endif
