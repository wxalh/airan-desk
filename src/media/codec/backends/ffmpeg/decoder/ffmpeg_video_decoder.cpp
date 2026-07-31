#include "media/codec/backends/ffmpeg/decoder/ffmpeg_video_decoder.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include "media/codec/backends/ffmpeg/encoder/util/ffmpeg_encoder_util.h"

#include <modules/video_coding/include/video_error_codes.h>
#include "common/logger_manager.h"

#include <cstring>
#include <limits>
#include <string>

namespace airan::media::ffmpeg
{
namespace
{
#if AIRAN_WEBRTC_MILESTONE >= 144
webrtc::VideoFrameType encodedFrameType(const webrtc::EncodedImage &image)
{
    return image.FrameType();
}

uint32_t encodedRtpTimestamp(const webrtc::EncodedImage &image)
{
    return image.RtpTimestamp();
}
#else
webrtc::VideoFrameType encodedFrameType(const webrtc::EncodedImage &image)
{
    return image._frameType;
}

uint32_t encodedRtpTimestamp(const webrtc::EncodedImage &image)
{
    return image.Timestamp();
}
#endif
} // namespace


FfmpegVideoDecoder::FfmpegVideoDecoder(CodecKind codec) : m_codec(codec)
{
}


FfmpegVideoDecoder::~FfmpegVideoDecoder()
{
    Release();
}


bool FfmpegVideoDecoder::Configure(const Settings &settings)
{
    Release();
    m_settings = settings;
    m_keyFrameRequired = true;
    m_probes = selectProbes(m_codec, false);
    if (m_probes.empty())
        return false;

    for (size_t i = 0; i < m_probes.size(); ++i)
    {
        if (configureProbe(i))
            return true;
    }
    Release();
    return false;
}


#if AIRAN_WEBRTC_MILESTONE >= 144
int32_t FfmpegVideoDecoder::Decode(const webrtc::EncodedImage &inputImage, int64_t renderTimeMs)
{
    return Decode(inputImage, false, renderTimeMs);
}
#endif

int32_t FfmpegVideoDecoder::Decode(const webrtc::EncodedImage &inputImage,
                                   bool missingFrames,
                                   int64_t renderTimeMs)
{
    if (m_fatalDecoderError)
        return WEBRTC_VIDEO_CODEC_ERROR;

    if (!m_ctx || !m_packet || !m_callback)
        return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    if (inputImage.size() > 0 && !inputImage.data())
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    if (inputImage.size() == 0)
    {
        avcodec_flush_buffers(m_ctx);
        m_keyFrameRequired = true;
        return WEBRTC_VIDEO_CODEC_NO_OUTPUT;
    }
    if (inputImage.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
    {
        LOG_WARN("FFmpeg decoder rejected oversized encoded frame: size={} bytes", inputImage.size());
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }

    if (missingFrames)
    {
        ++m_missingFrameCount;
        avcodec_flush_buffers(m_ctx);
        m_keyFrameRequired = true;
    }

    const int encodedWidth = static_cast<int>(inputImage._encodedWidth);
    const int encodedHeight = static_cast<int>(inputImage._encodedHeight);
    const bool hasEncodedSize = encodedWidth > 0 && encodedHeight > 0;
    const bool encodedSizeChanged = hasEncodedSize &&
                                    m_encodedWidth > 0 &&
                                    m_encodedHeight > 0 &&
                                    (encodedWidth != m_encodedWidth || encodedHeight != m_encodedHeight);
    if (encodedSizeChanged)
    {
        if (encodedFrameType(inputImage) != webrtc::VideoFrameType::kVideoFrameKey)
        {
            LOG_WARN("FFmpeg decoder encoded size changed on delta frame: {}x{} -> {}x{}; requesting key frame",
                     m_encodedWidth, m_encodedHeight, encodedWidth, encodedHeight);
            m_keyFrameRequired = true;
            return WEBRTC_VIDEO_CODEC_OK_REQUEST_KEYFRAME;
        }

        const size_t currentProbeIndex = m_probeIndex;
        LOG_DEBUG("FFmpeg decoder rebuilding for key-frame size change: {}x{} -> {}x{}, backend={}",
                  m_encodedWidth, m_encodedHeight, encodedWidth, encodedHeight,
                  (m_probe ? m_probe->backend : ""));
        if (!configureProbe(currentProbeIndex))
            return advanceProbe("resolution_change") ? WEBRTC_VIDEO_CODEC_NO_OUTPUT : WEBRTC_VIDEO_CODEC_ERROR;
        m_keyFrameRequired = true;
    }

    const bool inputIsKeyFrame = encodedFrameType(inputImage) == webrtc::VideoFrameType::kVideoFrameKey;
    if (m_keyFrameRequired)
    {
        if (!inputIsKeyFrame)
            return WEBRTC_VIDEO_CODEC_ERROR;
        m_keyFrameRequired = false;
    }
    if (hasEncodedSize)
    {
        m_encodedWidth = encodedWidth;
        m_encodedHeight = encodedHeight;
    }

    while (true)
    {
        if (av_new_packet(m_packet, static_cast<int>(inputImage.size())) < 0)
            return WEBRTC_VIDEO_CODEC_MEMORY;
        std::memcpy(m_packet->data, inputImage.data(), inputImage.size());
        m_packet->pts = encodedRtpTimestamp(inputImage);

        const int sendResult = avcodec_send_packet(m_ctx, m_packet);
        av_packet_unref(m_packet);
        if (sendResult < 0)
        {
            LOG_WARN("FFmpeg decoder avcodec_send_packet failed: backend={}, error={}",
                     (m_probe ? m_probe->backend : ""), ffmpegErrorText(sendResult));
            m_keyFrameRequired = true;
            const bool advanced = advanceProbe("send_packet");
            if (advanced && inputIsKeyFrame)
            {
                LOG_DEBUG("FFmpeg decoder retrying key frame after backend switch to {}",
                          (m_probe ? m_probe->backend : ""));
                m_keyFrameRequired = false;
                continue;
            }
            return advanced ? WEBRTC_VIDEO_CODEC_OK_REQUEST_KEYFRAME : WEBRTC_VIDEO_CODEC_ERROR;
        }

        AVFrame *frame = av_frame_alloc();
        AVFrame *swFrame = av_frame_alloc();
        if (!frame || !swFrame)
        {
            av_frame_free(&frame);
            av_frame_free(&swFrame);
            return WEBRTC_VIDEO_CODEC_MEMORY;
        }

        int result = WEBRTC_VIDEO_CODEC_NO_OUTPUT;
        const char *backendFailureReason = nullptr;
        while (true)
        {
            const int receiveResult = avcodec_receive_frame(m_ctx, frame);
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF)
                break;
            if (receiveResult < 0)
            {
                LOG_WARN("FFmpeg decoder avcodec_receive_frame failed: backend={}, error={}",
                         (m_probe ? m_probe->backend : ""), ffmpegErrorText(receiveResult));
                backendFailureReason = "receive_frame";
                break;
            }

            if (m_probe && frame->format == AV_PIX_FMT_D3D11 && emitD3D11Frame(frame, inputImage, renderTimeMs))
            {
                result = WEBRTC_VIDEO_CODEC_OK;
                av_frame_unref(frame);
                av_frame_unref(swFrame);
                continue;
            }

            AVFrame *decoded = frame;
            if (m_probe && frame->format == m_probe->hardwarePixelFormat)
            {
                if (av_hwframe_transfer_data(swFrame, frame, 0) < 0)
                {
                    LOG_WARN("FFmpeg decoder hwframe transfer failed: backend={}, frameFormat={}",
                             (m_probe ? m_probe->backend : ""), frame->format);
                    backendFailureReason = "hwframe_transfer";
                    break;
                }
                decoded = swFrame;
            }

            if (!emitFrame(decoded, inputImage, renderTimeMs))
            {
                result = WEBRTC_VIDEO_CODEC_ERROR;
                break;
            }
            result = WEBRTC_VIDEO_CODEC_OK;
            av_frame_unref(frame);
            av_frame_unref(swFrame);
        }

        av_frame_free(&frame);
        av_frame_free(&swFrame);

        if (backendFailureReason)
        {
            m_keyFrameRequired = true;
            const bool advanced = advanceProbe(backendFailureReason);
            if (advanced && inputIsKeyFrame && result != WEBRTC_VIDEO_CODEC_OK)
            {
                LOG_DEBUG("FFmpeg decoder retrying key frame after backend switch to {}",
                          (m_probe ? m_probe->backend : ""));
                m_keyFrameRequired = false;
                continue;
            }
            return advanced ? WEBRTC_VIDEO_CODEC_OK_REQUEST_KEYFRAME : WEBRTC_VIDEO_CODEC_ERROR;
        }

        return result;
    }
}


int32_t FfmpegVideoDecoder::RegisterDecodeCompleteCallback(webrtc::DecodedImageCallback *callback)
{
    m_callback = callback;
    return WEBRTC_VIDEO_CODEC_OK;
}


int32_t FfmpegVideoDecoder::Release()
{
    sws_freeContext(m_sws);
    m_sws = nullptr;
    av_packet_free(&m_packet);
    av_buffer_unref(&m_hwDevice);
    avcodec_free_context(&m_ctx);
    m_probe = nullptr;
    m_probeIndex = 0;
    m_fatalDecoderError = false;
    m_keyFrameRequired = true;
    m_encodedWidth = 0;
    m_encodedHeight = 0;
    return WEBRTC_VIDEO_CODEC_OK;
}


webrtc::VideoDecoder::DecoderInfo FfmpegVideoDecoder::GetDecoderInfo() const
{
    DecoderInfo info;
    info.implementation_name = m_probe ? std::string("FFmpeg ") + m_probe->backend : "FFmpeg";
    info.is_hardware_accelerated = m_probe &&
                                   (m_probe->deviceType != AV_HWDEVICE_TYPE_NONE ||
                                    m_probe->hardwarePixelFormat != AV_PIX_FMT_NONE ||
                                    (m_probe->zeroCopy && m_probe->zeroCopy[0] != '\0'));
    return info;
}

} // namespace airan::media::ffmpeg

#endif
