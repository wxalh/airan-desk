#include "media/codec/backends/ffmpeg/encoder/core/ffmpeg_video_encoder.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include "common/logger_manager.h"
#include "media/codec/backends/ffmpeg/encoder/util/ffmpeg_encoder_util.h"

#include <modules/video_coding/include/video_error_codes.h>
#include <rtc_base/logging.h>

namespace airan::media::ffmpeg
{


FfmpegVideoEncoder::FfmpegVideoEncoder(CodecKind codec, webrtc::H264PacketizationMode packetizationMode)
    : m_codec(codec), m_packetizationMode(packetizationMode)
{
}


FfmpegVideoEncoder::~FfmpegVideoEncoder()
{
    Release();
}


int FfmpegVideoEncoder::InitEncode(const webrtc::VideoCodec *codecSettings, const Settings &settings)
{
    if (!codecSettings || codecSettings->codecType != webrtcCodecType(m_codec))
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    if (!codecSettings->active)
        return WEBRTC_VIDEO_CODEC_OK;
    if (codecSettings->width == 0 || codecSettings->height == 0)
    {
        LOG_WARN("Airan FFmpeg encoder rejected invalid codec dimensions: {}x{}",
                 codecSettings->width, codecSettings->height);
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }
    const bool unsupportedLayering =
        codecSettings->numberOfSimulcastStreams > 1 ||
        (codecSettings->codecType == webrtc::kVideoCodecH264 &&
         codecSettings->H264().numberOfTemporalLayers > 1) ||
        (codecSettings->codecType == webrtc::kVideoCodecVP8 &&
         codecSettings->VP8().numberOfTemporalLayers > 1) ||
        (codecSettings->codecType == webrtc::kVideoCodecVP9 &&
         (codecSettings->VP9().numberOfTemporalLayers > 1 ||
          codecSettings->VP9().numberOfSpatialLayers > 1)) ||
        (codecSettings->GetScalabilityMode().has_value() &&
         *codecSettings->GetScalabilityMode() != webrtc::ScalabilityMode::kL1T1);
    if (unsupportedLayering)
        return WEBRTC_VIDEO_CODEC_ERR_SIMULCAST_PARAMETERS_NOT_SUPPORTED;

    m_codecSettings = *codecSettings;
    m_encoderSettings = settings;
    m_hasCodecSettings = true;
    m_targetBitrateBps = codecBitrateBps(codecSettings->startBitrate);
    m_targetFramerateFps = codecSettings->maxFramerate;
    m_probes = selectProbes(m_codec, true);
    Release();

    if (m_probes.empty())
        return WEBRTC_VIDEO_CODEC_ERROR;

    if (configureStage(FfmpegEncodeAttemptStage::NativeGpu, 0, "initial encode setup"))
        return WEBRTC_VIDEO_CODEC_OK;
    if (configureStage(FfmpegEncodeAttemptStage::CpuHardware, 0, "native gpu setup unavailable"))
        return WEBRTC_VIDEO_CODEC_OK;
    if (configureStage(FfmpegEncodeAttemptStage::Software, 0, "hardware setup unavailable"))
        return WEBRTC_VIDEO_CODEC_OK;

    m_fatalEncoderError = true;
    return WEBRTC_VIDEO_CODEC_ERROR;
}


int32_t FfmpegVideoEncoder::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback *callback)
{
    m_callback = callback;
    return WEBRTC_VIDEO_CODEC_OK;
}


int32_t FfmpegVideoEncoder::Release()
{
    av_packet_free(&m_packet);
    av_buffer_unref(&m_hwFrames);
    av_buffer_unref(&m_hwDevice);
    av_buffer_unref(&m_nativeD3D11Frames);
    av_buffer_unref(&m_nativeD3D11Device);
    releaseQsvHwMapGraph();
    releaseD3D11VideoProcessor();
    avcodec_free_context(&m_ctx);
    m_probe = nullptr;
    m_nativeD3D11SessionBound = false;
    m_nativeD3D11Healthy = true;
    m_nativeD3D11DisabledForSession = false;
    m_fatalEncoderError = false;
    m_mediaPaused = false;
    m_forceKeyFrameOnResume = false;
    m_forceKeyFrameForRecovery = false;
    return WEBRTC_VIDEO_CODEC_OK;
}

} // namespace airan::media::ffmpeg

#endif
