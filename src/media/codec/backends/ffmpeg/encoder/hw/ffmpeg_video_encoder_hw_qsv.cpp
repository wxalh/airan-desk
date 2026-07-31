#include "media/codec/backends/ffmpeg/encoder/core/ffmpeg_video_encoder.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include "media/codec/backends/ffmpeg/encoder/util/ffmpeg_encoder_util.h"
#include "common/logger_manager.h"

#include <rtc_base/logging.h>

#include <cstdio>

namespace airan::media::ffmpeg
{


void FfmpegVideoEncoder::releaseQsvHwMapGraph()
{
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    if (m_qsvFilterGraph)
        avfilter_graph_free(&m_qsvFilterGraph);
    m_qsvBufferSrc = nullptr;
    m_qsvBufferSink = nullptr;
    av_buffer_unref(&m_qsvFilterFrames);
    m_qsvFilterWidth = 0;
    m_qsvFilterHeight = 0;
#endif
}


bool FfmpegVideoEncoder::ensureQsvHwMapGraph(AVFrame *d3d11Frame)
{
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    if (!d3d11Frame || !d3d11Frame->hw_frames_ctx || !m_probe || m_probe->deviceType != AV_HWDEVICE_TYPE_QSV)
        return false;

    auto *frames = reinterpret_cast<AVHWFramesContext *>(d3d11Frame->hw_frames_ctx->data);
    if (!frames)
        return false;

    const bool sameFrames = m_qsvFilterFrames && m_qsvFilterFrames->data == d3d11Frame->hw_frames_ctx->data;
    if (m_qsvFilterGraph && sameFrames &&
        m_qsvFilterWidth == frames->width && m_qsvFilterHeight == frames->height)
        return true;

    releaseQsvHwMapGraph();

    AVFilterGraph *graph = avfilter_graph_alloc();
    if (!graph)
        return false;

    AVFilterContext *src = nullptr;
    AVFilterContext *hwmap = nullptr;
    AVFilterContext *format = nullptr;
    AVFilterContext *sink = nullptr;

    char args[256] = {};
    std::snprintf(args, sizeof(args),
                  "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                  frames->width, frames->height,
                  static_cast<int>(AV_PIX_FMT_D3D11),
                  m_ctx && m_ctx->time_base.num > 0 ? m_ctx->time_base.num : 1,
                  m_ctx && m_ctx->time_base.den > 0 ? m_ctx->time_base.den : 30,
                  1, 1);

    int ret = avfilter_graph_create_filter(&src, avfilter_get_by_name("buffer"), "in", args, nullptr, graph);
    if (ret < 0)
    {
        LOG_WARN("Airan FFmpeg QSV hwmap buffer source failed: {}", ffmpegErrorText(ret));
        avfilter_graph_free(&graph);
        return false;
    }

    AVBufferSrcParameters *params = av_buffersrc_parameters_alloc();
    if (!params)
    {
        avfilter_graph_free(&graph);
        return false;
    }
    params->hw_frames_ctx = av_buffer_ref(d3d11Frame->hw_frames_ctx);
    ret = av_buffersrc_parameters_set(src, params);
    av_free(params);
    if (ret < 0)
    {
        LOG_WARN("Airan FFmpeg QSV hwmap source parameters failed: {}", ffmpegErrorText(ret));
        avfilter_graph_free(&graph);
        return false;
    }

    ret = avfilter_graph_create_filter(&hwmap, avfilter_get_by_name("hwmap"), "hwmap",
                                       "derive_device=qsv", nullptr, graph);
    if (ret < 0)
    {
        LOG_WARN("Airan FFmpeg QSV hwmap filter creation failed: {}", ffmpegErrorText(ret));
        avfilter_graph_free(&graph);
        return false;
    }

    ret = avfilter_graph_create_filter(&format, avfilter_get_by_name("format"), "format",
                                       "pix_fmts=qsv", nullptr, graph);
    if (ret < 0)
    {
        LOG_WARN("Airan FFmpeg QSV format filter creation failed: {}", ffmpegErrorText(ret));
        avfilter_graph_free(&graph);
        return false;
    }

    ret = avfilter_graph_create_filter(&sink, avfilter_get_by_name("buffersink"), "out",
                                       nullptr, nullptr, graph);
    if (ret < 0)
    {
        LOG_WARN("Airan FFmpeg QSV buffersink creation failed: {}", ffmpegErrorText(ret));
        avfilter_graph_free(&graph);
        return false;
    }

    ret = avfilter_link(src, 0, hwmap, 0);
    if (ret >= 0)
        ret = avfilter_link(hwmap, 0, format, 0);
    if (ret >= 0)
        ret = avfilter_link(format, 0, sink, 0);
    if (ret >= 0)
        ret = avfilter_graph_config(graph, nullptr);
    if (ret < 0)
    {
        LOG_WARN("Airan FFmpeg QSV hwmap graph config failed: {}", ffmpegErrorText(ret));
        avfilter_graph_free(&graph);
        return false;
    }

    m_qsvFilterGraph = graph;
    m_qsvBufferSrc = src;
    m_qsvBufferSink = sink;
    m_qsvFilterFrames = av_buffer_ref(d3d11Frame->hw_frames_ctx);
    m_qsvFilterWidth = frames->width;
    m_qsvFilterHeight = frames->height;
    LOG_INFO("Airan FFmpeg QSV D3D11 hwmap graph ready: {}x{}",
             m_qsvFilterWidth, m_qsvFilterHeight);
    return true;
#else
    (void)d3d11Frame;
    return false;
#endif
}


AVFrame *FfmpegVideoEncoder::mapD3D11FrameToQsv(AVFrame *d3d11Frame)
{
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    if (!ensureQsvHwMapGraph(d3d11Frame))
        return nullptr;

    const int retAdd = av_buffersrc_add_frame_flags(m_qsvBufferSrc, d3d11Frame, 0);
    if (retAdd < 0)
    {
        LOG_WARN("Airan FFmpeg QSV hwmap add_frame failed: {}", ffmpegErrorText(retAdd));
        releaseQsvHwMapGraph();
        return nullptr;
    }

    AVFrame *qsvFrame = av_frame_alloc();
    if (!qsvFrame)
        return nullptr;
    const int retGet = av_buffersink_get_frame(m_qsvBufferSink, qsvFrame);
    if (retGet < 0)
    {
        LOG_WARN("Airan FFmpeg QSV hwmap get_frame failed: {}", ffmpegErrorText(retGet));
        av_frame_free(&qsvFrame);
        releaseQsvHwMapGraph();
        return nullptr;
    }

    if (!m_nativeD3D11SessionBound && qsvFrame->hw_frames_ctx &&
        !rebindQsvEncoderSessionWithFrames(qsvFrame->hw_frames_ctx))
    {
        LOG_WARN("Airan FFmpeg QSV encoder rebind with hwmap frames failed");
        av_frame_free(&qsvFrame);
        return nullptr;
    }

    return qsvFrame;
#else
    (void)d3d11Frame;
    return nullptr;
#endif
}

} // namespace airan::media::ffmpeg

#endif
