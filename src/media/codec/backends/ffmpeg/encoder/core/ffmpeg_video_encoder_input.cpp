#include "media/codec/backends/ffmpeg/encoder/core/ffmpeg_video_encoder.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include "media/codec/backends/ffmpeg/encoder/util/ffmpeg_encoder_util.h"
#include "common/logger_manager.h"

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
#include "rtc/core/native_d3d11_video_frame_buffer.h"
#endif

#include <api/video/video_frame.h>
#include <modules/video_coding/include/video_error_codes.h>
#include <rtc_base/logging.h>

#include <vector>

namespace airan::media::ffmpeg
{
namespace
{

void logEncodeTransition(const airan::media::PathTransition &transition)
{
    const bool changed =
        transition.previous_encode_path != transition.current_encode_path ||
        transition.previous_capture_path != transition.current_capture_path ||
        transition.fallback_reason != airan::media::FallbackReason::kNone ||
        transition.encode_breaker_open ||
        transition.failure_count > 0 ||
        transition.threshold_triggered ||
        transition.device_event ||
        transition.encoder_rebuild ||
        transition.pipeline_reinit;
    if (!changed)
        return;

    LOG_INFO("Airan encode path transition: capture={}->{}, encode={}->{}, reason={}, encodeBreaker={}, failures={}, threshold={}, deviceEvent={}, encoderRebuild={}, pipelineReinit={}",
             airan::media::toString(transition.previous_capture_path),
             airan::media::toString(transition.current_capture_path),
             airan::media::toString(transition.previous_encode_path),
             airan::media::toString(transition.current_encode_path),
             airan::media::toString(transition.fallback_reason),
             transition.encode_breaker_open ? 1 : 0,
             transition.failure_count,
             transition.threshold_triggered ? 1 : 0,
             transition.device_event ? 1 : 0,
             transition.encoder_rebuild ? 1 : 0,
             transition.pipeline_reinit ? 1 : 0);
}

} // namespace


int32_t FfmpegVideoEncoder::Encode(const webrtc::VideoFrame &frame,
                                   const std::vector<webrtc::VideoFrameType> *frameTypes)
{
    if (!m_ctx || !m_callback)
        return WEBRTC_VIDEO_CODEC_ERROR;
    if (m_ctx->width <= 0 || m_ctx->height <= 0)
    {
        LOG_WARN("Airan FFmpeg encoder rejected frame because codec context size is invalid; backend={}, size={}x{}",
                 m_probe ? m_probe->backend : "",
                 m_ctx->width, m_ctx->height);
        m_fatalEncoderError = true;
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }
    if (!frame.video_frame_buffer() ||
        frame.video_frame_buffer()->width() <= 0 ||
        frame.video_frame_buffer()->height() <= 0)
    {
        LOG_WARN("Airan FFmpeg encoder dropped invalid input frame; backend={}, frame={}x{}",
                 m_probe ? m_probe->backend : "",
                 frame.video_frame_buffer() ? frame.video_frame_buffer()->width() : 0,
                 frame.video_frame_buffer() ? frame.video_frame_buffer()->height() : 0);
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }
    if (m_fatalEncoderError)
        return WEBRTC_VIDEO_CODEC_ERROR;
    if (m_mediaPaused)
        return WEBRTC_VIDEO_CODEC_OK;
    if (m_dropNextFrameByCallback)
    {
        m_dropNextFrameByCallback = false;
        if (m_callback)
            m_callback->OnDroppedFrame(webrtc::EncodedImageCallback::DropReason::kDroppedByEncoder);
        return WEBRTC_VIDEO_CODEC_NO_OUTPUT;
    }

    const bool hasNativeD3D11Input =
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
        rtc::asD3D11TextureFrameBuffer(frame.video_frame_buffer().get()) != nullptr;
#else
        false;
#endif
    const bool usingHardwareEncoder =
        m_probe && (m_probe->deviceType != AV_HWDEVICE_TYPE_NONE ||
                    m_probe->hardwarePixelFormat != AV_PIX_FMT_NONE ||
                    (m_probe->zeroCopy && m_probe->zeroCopy[0] != '\0'));

    m_encodeFallback.setCurrentCapturePath(
        hasNativeD3D11Input ? airan::media::CapturePath::NativeGpuCapture
                            : airan::media::CapturePath::WebRtcDerivedCpuCapture);

    const auto tryEncodeCurrentProbe = [&]() -> int32_t {
        if (m_attemptStage == FfmpegEncodeAttemptStage::NativeGpu &&
            hasNativeD3D11Input && m_nativeD3D11Healthy && !m_nativeD3D11DisabledForSession)
        {
            AVFrame *nativeFrame = createHardwareFrameFromNativeD3D11(frame);
            if (nativeFrame)
            {
                const int32_t result = encodeFrame(frame, frameTypes, nativeFrame);
                av_frame_free(&nativeFrame);
                if (result == WEBRTC_VIDEO_CODEC_OK)
                {
                    logEncodeTransition(
                        m_encodeFallback.markEncodeSuccess(airan::media::EncodePath::GpuCopyHwEncode));
                    return result;
                }
                m_nativeD3D11Healthy = false;
                m_nativeD3D11DisabledForSession = true;
                logEncodeTransition(
                    m_encodeFallback.markEncodeFailure(airan::media::FallbackReason::DeviceError,
                                                       airan::media::EncodePath::CpuReadbackHwEncode));
                LOG_WARN("Airan FFmpeg native D3D11 encode failed; backend={}. Native path disabled for this encoder session",
                         m_probe ? m_probe->backend : "");
            }
            else if (hasNativeD3D11Input && m_probe && m_probe->deviceType != AV_HWDEVICE_TYPE_NONE)
            {
                m_nativeD3D11Healthy = false;
                m_nativeD3D11DisabledForSession = true;
                logEncodeTransition(
                    m_encodeFallback.markEncodeFailure(airan::media::FallbackReason::HandleIncompatible,
                                                       airan::media::EncodePath::CpuReadbackHwEncode));
                LOG_WARN("Airan FFmpeg failed to build native D3D11 frame; backend={}. Native path disabled for this encoder session; falling back to CPU readback hardware encode",
                         m_probe ? m_probe->backend : "");
            }
            return kAiranCodecTryNextBackend;
        }

        if (m_attemptStage == FfmpegEncodeAttemptStage::NativeGpu)
            return kAiranCodecTryNextBackend;

        auto i420 = frame.video_frame_buffer() ? frame.video_frame_buffer()->ToI420() : nullptr;
        if (!i420)
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;

        AVFrame *swFrame = av_frame_alloc();
        AVFrame *hwFrame = nullptr;
        if (!swFrame)
            return WEBRTC_VIDEO_CODEC_MEMORY;

        const bool useHardwareFrame = m_ctx->hw_frames_ctx != nullptr;
        swFrame->format = useHardwareFrame ? AV_PIX_FMT_NV12 :
                          (m_probe && m_probe->softwarePixelFormat != AV_PIX_FMT_NONE ? m_probe->softwarePixelFormat : AV_PIX_FMT_YUV420P);
        swFrame->width = m_ctx->width;
        swFrame->height = m_ctx->height;
        swFrame->color_range = AVCOL_RANGE_MPEG;
        swFrame->colorspace = AVCOL_SPC_BT709;
        swFrame->color_primaries = AVCOL_PRI_BT709;
        swFrame->color_trc = AVCOL_TRC_BT709;
        swFrame->chroma_location = AVCHROMA_LOC_LEFT;
        if (av_frame_get_buffer(swFrame, 32) < 0 || av_frame_make_writable(swFrame) < 0 ||
            !copyI420ToFrame(*i420, swFrame))
        {
            av_frame_free(&swFrame);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }

        AVFrame *inputFrame = swFrame;
        if (useHardwareFrame)
        {
            hwFrame = av_frame_alloc();
            if (!hwFrame || av_hwframe_get_buffer(m_ctx->hw_frames_ctx, hwFrame, 0) < 0 ||
                av_hwframe_transfer_data(hwFrame, swFrame, 0) < 0)
            {
                av_frame_free(&hwFrame);
                av_frame_free(&swFrame);
                const auto degradedPath =
                    hasNativeD3D11Input ? airan::media::EncodePath::CpuReadbackHwEncode
                                        : airan::media::EncodePath::CpuUploadHwEncode;
                logEncodeTransition(
                    m_encodeFallback.markEncodeFailure(airan::media::FallbackReason::DeviceError,
                                                       degradedPath));
                LOG_WARN("Airan FFmpeg upload-hw frame transfer failed; backend={}",
                         m_probe ? m_probe->backend : "");
                return kAiranCodecTryNextBackend;
            }
            hwFrame->color_range = AVCOL_RANGE_MPEG;
            hwFrame->colorspace = AVCOL_SPC_BT709;
            hwFrame->color_primaries = AVCOL_PRI_BT709;
            hwFrame->color_trc = AVCOL_TRC_BT709;
            hwFrame->chroma_location = AVCHROMA_LOC_LEFT;
            inputFrame = hwFrame;
        }

        const int32_t encodeResult = encodeFrame(frame, frameTypes, inputFrame);
        av_frame_free(&hwFrame);
        av_frame_free(&swFrame);
        if (encodeResult == WEBRTC_VIDEO_CODEC_OK)
        {
            const auto successfulPath =
                useHardwareFrame || usingHardwareEncoder
                    ? (hasNativeD3D11Input ? airan::media::EncodePath::CpuReadbackHwEncode
                                           : airan::media::EncodePath::CpuUploadHwEncode)
                    : airan::media::EncodePath::CpuSoftwareEncode;
            logEncodeTransition(
                m_encodeFallback.markEncodeSuccess(successfulPath));
        }
        else if (encodeResult == kAiranCodecTryNextBackend)
        {
            const auto degradedPath =
                useHardwareFrame || usingHardwareEncoder
                    ? (hasNativeD3D11Input ? airan::media::EncodePath::CpuReadbackHwEncode
                                           : airan::media::EncodePath::CpuUploadHwEncode)
                    : airan::media::EncodePath::CpuSoftwareEncode;
            logEncodeTransition(
                m_encodeFallback.markEncodeFailure(airan::media::FallbackReason::DeviceError,
                                                   degradedPath));
        }
        return encodeResult;
    };

    for (size_t attempts = m_probeIndex; attempts < m_probes.size(); ++attempts)
    {
        const int32_t result = tryEncodeCurrentProbe();
        if (result != kAiranCodecTryNextBackend)
            return result;
        if (!advanceProbe("encode failed"))
            return WEBRTC_VIDEO_CODEC_ERROR;
    }

    m_fatalEncoderError = true;
    return WEBRTC_VIDEO_CODEC_ERROR;
}

} // namespace airan::media::ffmpeg

#endif
