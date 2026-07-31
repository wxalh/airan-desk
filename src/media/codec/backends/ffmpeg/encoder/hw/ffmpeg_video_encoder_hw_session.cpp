#include "media/codec/backends/ffmpeg/encoder/core/ffmpeg_video_encoder.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include "media/codec/backends/ffmpeg/encoder/util/ffmpeg_encoder_util.h"
#include "common/logger_manager.h"

#include <rtc_base/logging.h>

namespace airan::media::ffmpeg
{


bool FfmpegVideoEncoder::rebindD3D11EncoderSession(AVBufferRef *device, AVBufferRef *frames)
{
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    if (!m_probe || !device || !frames || m_probe->hardwarePixelFormat != AV_PIX_FMT_D3D11)
        return false;

    const AVCodec *codec = avcodec_find_encoder_by_name(m_probe->ffmpegCodec);
    if (!codec)
        return false;

    const int width = m_ctx ? m_ctx->width : 0;
    const int height = m_ctx ? m_ctx->height : 0;
    auto *framesCtx = reinterpret_cast<AVHWFramesContext *>(frames->data);
    const AVPixelFormat swFormat =
        framesCtx && framesCtx->sw_format != AV_PIX_FMT_NONE ? framesCtx->sw_format : AV_PIX_FMT_NV12;
    const AVRational timeBase = m_ctx ? m_ctx->time_base : AVRational{1, 30};
    const AVRational frameRate = m_ctx ? m_ctx->framerate : AVRational{30, 1};
    const int64_t bitRate = m_ctx ? m_ctx->bit_rate : 2500000;
    const int64_t minRate = m_ctx ? m_ctx->rc_min_rate : bitRate;
    const int64_t maxRate = m_ctx ? m_ctx->rc_max_rate : bitRate;
    const int bufferSize = m_ctx ? m_ctx->rc_buffer_size : 0;
    const int gopSize = m_ctx ? m_ctx->gop_size : 60;

    AVCodecContext *newCtx = avcodec_alloc_context3(codec);
    if (!newCtx)
        return false;

    newCtx->width = width;
    newCtx->height = height;
    newCtx->time_base = timeBase;
    newCtx->pkt_timebase = timeBase;
    newCtx->framerate = frameRate;
    newCtx->bit_rate = bitRate;
    newCtx->rc_min_rate = minRate;
    newCtx->rc_max_rate = maxRate;
    newCtx->rc_buffer_size = bufferSize;
    newCtx->gop_size = gopSize;
    newCtx->max_b_frames = 0;
    newCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    newCtx->pix_fmt = AV_PIX_FMT_D3D11;
    newCtx->sw_pix_fmt = swFormat;
    newCtx->color_range = AVCOL_RANGE_MPEG;
    newCtx->colorspace = AVCOL_SPC_BT709;
    newCtx->color_primaries = AVCOL_PRI_BT709;
    newCtx->color_trc = AVCOL_TRC_BT709;
    newCtx->chroma_sample_location = AVCHROMA_LOC_LEFT;
    configureH264CodecContext(newCtx);
    newCtx->hw_device_ctx = av_buffer_ref(device);
    newCtx->hw_frames_ctx = av_buffer_ref(frames);
    if (!newCtx->hw_device_ctx || !newCtx->hw_frames_ctx)
    {
        avcodec_free_context(&newCtx);
        return false;
    }

    if (newCtx->priv_data)
    {
        configureRealtimeEncoderOptions(newCtx, m_probe);
        if (swFormat == AV_PIX_FMT_BGRA || swFormat == AV_PIX_FMT_BGR0 ||
            swFormat == AV_PIX_FMT_RGBA || swFormat == AV_PIX_FMT_RGB0)
        {
            av_opt_set(newCtx->priv_data, "rgb_mode", "yuv420", 0);
        }
    }

    const int openResult = avcodec_open2(newCtx, codec, nullptr);
    if (openResult < 0)
    {
        LOG_WARN("Airan FFmpeg D3D11 encoder open failed; backend={}, sw_format={}, error={}",
                 m_probe ? m_probe->backend : "",
                 static_cast<int>(swFormat),
                 ffmpegErrorText(openResult));
        avcodec_free_context(&newCtx);
        return false;
    }

    AVPacket *newPacket = av_packet_alloc();
    if (!newPacket)
    {
        avcodec_free_context(&newCtx);
        return false;
    }

    av_packet_free(&m_packet);
    av_buffer_unref(&m_hwFrames);
    av_buffer_unref(&m_hwDevice);
    avcodec_free_context(&m_ctx);
    m_ctx = newCtx;
    m_packet = newPacket;
    m_nativeD3D11SessionBound = true;
    return m_nativeD3D11SessionBound;
#else
    (void)device;
    (void)frames;
    return false;
#endif
}


bool FfmpegVideoEncoder::rebindQsvEncoderSession(AVBufferRef *device, AVBufferRef *frames)
{
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    if (!m_probe || !device || !frames || m_probe->deviceType != AV_HWDEVICE_TYPE_QSV)
        return false;

    AVBufferRef *qsvDevice = nullptr;
    const int deriveResult = av_hwdevice_ctx_create_derived(&qsvDevice, AV_HWDEVICE_TYPE_QSV, device, 0);
    if (deriveResult < 0 || !qsvDevice)
    {
        LOG_WARN("Airan FFmpeg QSV derive device failed: {}", ffmpegErrorText(deriveResult));
        return false;
    }

    AVBufferRef *qsvFrames = av_hwframe_ctx_alloc(qsvDevice);
    if (!qsvFrames)
    {
        av_buffer_unref(&qsvDevice);
        return false;
    }
    auto *framesCtx = reinterpret_cast<AVHWFramesContext *>(qsvFrames->data);
    auto *d3d11Frames = reinterpret_cast<AVHWFramesContext *>(frames->data);
    framesCtx->format = AV_PIX_FMT_QSV;
    framesCtx->sw_format = d3d11Frames ? d3d11Frames->sw_format : AV_PIX_FMT_NV12;
    framesCtx->width = d3d11Frames ? d3d11Frames->width : (m_ctx ? m_ctx->width : 0);
    framesCtx->height = d3d11Frames ? d3d11Frames->height : (m_ctx ? m_ctx->height : 0);
    framesCtx->initial_pool_size = 16;
    const int framesResult = av_hwframe_ctx_init(qsvFrames);
    if (framesResult < 0)
    {
        LOG_WARN("Airan FFmpeg QSV frame context init failed: {}", ffmpegErrorText(framesResult));
        av_buffer_unref(&qsvFrames);
        av_buffer_unref(&qsvDevice);
        return false;
    }

    const AVCodec *codec = avcodec_find_encoder_by_name(m_probe->ffmpegCodec);
    if (!codec)
    {
        av_buffer_unref(&qsvFrames);
        av_buffer_unref(&qsvDevice);
        return false;
    }

    const int width = m_ctx ? m_ctx->width : framesCtx->width;
    const int height = m_ctx ? m_ctx->height : framesCtx->height;
    const AVRational timeBase = m_ctx ? m_ctx->time_base : AVRational{1, 30};
    const AVRational frameRate = m_ctx ? m_ctx->framerate : AVRational{30, 1};
    const int64_t bitRate = m_ctx ? m_ctx->bit_rate : 2500000;
    const int64_t minRate = m_ctx ? m_ctx->rc_min_rate : bitRate;
    const int64_t maxRate = m_ctx ? m_ctx->rc_max_rate : bitRate;
    const int bufferSize = m_ctx ? m_ctx->rc_buffer_size : 0;
    const int gopSize = m_ctx ? m_ctx->gop_size : 60;

    AVCodecContext *newCtx = avcodec_alloc_context3(codec);
    if (!newCtx)
    {
        av_buffer_unref(&qsvFrames);
        av_buffer_unref(&qsvDevice);
        return false;
    }
    newCtx->width = width;
    newCtx->height = height;
    newCtx->time_base = timeBase;
    newCtx->pkt_timebase = timeBase;
    newCtx->framerate = frameRate;
    newCtx->bit_rate = bitRate;
    newCtx->rc_min_rate = minRate;
    newCtx->rc_max_rate = maxRate;
    newCtx->rc_buffer_size = bufferSize;
    newCtx->gop_size = gopSize;
    newCtx->max_b_frames = 0;
    newCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    newCtx->pix_fmt = AV_PIX_FMT_QSV;
    newCtx->color_range = AVCOL_RANGE_MPEG;
    newCtx->colorspace = AVCOL_SPC_BT709;
    newCtx->color_primaries = AVCOL_PRI_BT709;
    newCtx->color_trc = AVCOL_TRC_BT709;
    newCtx->chroma_sample_location = AVCHROMA_LOC_LEFT;
    configureH264CodecContext(newCtx);
    newCtx->hw_device_ctx = av_buffer_ref(qsvDevice);
    newCtx->hw_frames_ctx = av_buffer_ref(qsvFrames);
    if (!newCtx->hw_device_ctx || !newCtx->hw_frames_ctx)
    {
        avcodec_free_context(&newCtx);
        av_buffer_unref(&qsvFrames);
        av_buffer_unref(&qsvDevice);
        return false;
    }

    if (newCtx->priv_data)
        configureRealtimeEncoderOptions(newCtx, m_probe);

    const int qsvOpenResult = avcodec_open2(newCtx, codec, nullptr);
    if (qsvOpenResult < 0)
    {
        LOG_WARN("Airan FFmpeg QSV encoder open failed: {}", ffmpegErrorText(qsvOpenResult));
        avcodec_free_context(&newCtx);
        av_buffer_unref(&qsvFrames);
        av_buffer_unref(&qsvDevice);
        return false;
    }

    AVPacket *newPacket = av_packet_alloc();
    if (!newPacket)
    {
        avcodec_free_context(&newCtx);
        av_buffer_unref(&qsvFrames);
        av_buffer_unref(&qsvDevice);
        return false;
    }

    av_packet_free(&m_packet);
    av_buffer_unref(&m_hwFrames);
    av_buffer_unref(&m_hwDevice);
    avcodec_free_context(&m_ctx);
    m_ctx = newCtx;
    m_packet = newPacket;
    m_hwDevice = qsvDevice;
    m_hwFrames = qsvFrames;
    m_nativeD3D11SessionBound = true;
    return true;
#else
    (void)device;
    (void)frames;
    return false;
#endif
}


bool FfmpegVideoEncoder::rebindQsvEncoderSessionWithFrames(AVBufferRef *frames)
{
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    if (!m_probe || !frames || m_probe->deviceType != AV_HWDEVICE_TYPE_QSV)
        return false;

    auto *framesCtx = reinterpret_cast<AVHWFramesContext *>(frames->data);
    if (!framesCtx || !framesCtx->device_ref)
        return false;

    const AVCodec *codec = avcodec_find_encoder_by_name(m_probe->ffmpegCodec);
    if (!codec)
        return false;

    const int width = m_ctx ? m_ctx->width : framesCtx->width;
    const int height = m_ctx ? m_ctx->height : framesCtx->height;
    const AVRational timeBase = m_ctx ? m_ctx->time_base : AVRational{1, 30};
    const AVRational frameRate = m_ctx ? m_ctx->framerate : AVRational{30, 1};
    const int64_t bitRate = m_ctx ? m_ctx->bit_rate : 2500000;
    const int64_t minRate = m_ctx ? m_ctx->rc_min_rate : bitRate;
    const int64_t maxRate = m_ctx ? m_ctx->rc_max_rate : bitRate;
    const int bufferSize = m_ctx ? m_ctx->rc_buffer_size : 0;
    const int gopSize = m_ctx ? m_ctx->gop_size : 60;

    AVCodecContext *newCtx = avcodec_alloc_context3(codec);
    if (!newCtx)
        return false;
    newCtx->width = width;
    newCtx->height = height;
    newCtx->time_base = timeBase;
    newCtx->pkt_timebase = timeBase;
    newCtx->framerate = frameRate;
    newCtx->bit_rate = bitRate;
    newCtx->rc_min_rate = minRate;
    newCtx->rc_max_rate = maxRate;
    newCtx->rc_buffer_size = bufferSize;
    newCtx->gop_size = gopSize;
    newCtx->max_b_frames = 0;
    newCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    newCtx->pix_fmt = AV_PIX_FMT_QSV;
    newCtx->color_range = AVCOL_RANGE_MPEG;
    newCtx->colorspace = AVCOL_SPC_BT709;
    newCtx->color_primaries = AVCOL_PRI_BT709;
    newCtx->color_trc = AVCOL_TRC_BT709;
    newCtx->chroma_sample_location = AVCHROMA_LOC_LEFT;
    configureH264CodecContext(newCtx);
    newCtx->hw_device_ctx = av_buffer_ref(framesCtx->device_ref);
    newCtx->hw_frames_ctx = av_buffer_ref(frames);
    if (!newCtx->hw_device_ctx || !newCtx->hw_frames_ctx)
    {
        avcodec_free_context(&newCtx);
        return false;
    }

    if (newCtx->priv_data)
        configureRealtimeEncoderOptions(newCtx, m_probe);

    const int openResult = avcodec_open2(newCtx, codec, nullptr);
    if (openResult < 0)
    {
        LOG_WARN("Airan FFmpeg QSV encoder open with hwmap frames failed: {}",
                 ffmpegErrorText(openResult));
        avcodec_free_context(&newCtx);
        return false;
    }

    AVPacket *newPacket = av_packet_alloc();
    if (!newPacket)
    {
        avcodec_free_context(&newCtx);
        return false;
    }

    av_packet_free(&m_packet);
    av_buffer_unref(&m_hwFrames);
    av_buffer_unref(&m_hwDevice);
    avcodec_free_context(&m_ctx);
    m_ctx = newCtx;
    m_packet = newPacket;
    m_hwDevice = av_buffer_ref(framesCtx->device_ref);
    m_hwFrames = av_buffer_ref(frames);
    m_nativeD3D11SessionBound = true;
    LOG_INFO("Airan FFmpeg QSV encoder rebound using D3D11 hwmap frames");
    return true;
#else
    (void)frames;
    return false;
#endif
}

} // namespace airan::media::ffmpeg

#endif
