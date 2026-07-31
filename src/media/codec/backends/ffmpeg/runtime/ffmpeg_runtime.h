#pragma once

#if defined(AIRAN_HAVE_FFMPEG)
#include "media/runtime/runtime_library.h"

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
#include <libavutil/hwcontext_d3d11va.h>
#endif

namespace airan::media::ffmpeg
{

struct FfmpegApi
{
    using FindCodecByNameFn = const AVCodec *(*)(const char *);
    using AllocContextFn = AVCodecContext *(*)(const AVCodec *);
    using FreeContextFn = void (*)(AVCodecContext **);
    using OpenCodecFn = int (*)(AVCodecContext *, const AVCodec *, AVDictionary **);
    using SendFrameFn = int (*)(AVCodecContext *, const AVFrame *);
    using ReceivePacketFn = int (*)(AVCodecContext *, AVPacket *);
    using SendPacketFn = int (*)(AVCodecContext *, const AVPacket *);
    using ReceiveFrameFn = int (*)(AVCodecContext *, AVFrame *);
    using FlushBuffersFn = void (*)(AVCodecContext *);
    using PacketAllocFn = AVPacket *(*)();
    using PacketFreeFn = void (*)(AVPacket **);
    using PacketUnrefFn = void (*)(AVPacket *);
    using NewPacketFn = int (*)(AVPacket *, int);
    using FrameAllocFn = AVFrame *(*)();
    using FrameFreeFn = void (*)(AVFrame **);
    using FrameUnrefFn = void (*)(AVFrame *);
    using FrameGetBufferFn = int (*)(AVFrame *, int);
    using FrameMakeWritableFn = int (*)(AVFrame *);
    using AvFreeFn = void (*)(void *);
    using AvStrErrorFn = int (*)(int, char *, size_t);
    using HwDeviceCtxAllocFn = AVBufferRef *(*)(enum AVHWDeviceType);
    using HwDeviceCtxInitFn = int (*)(AVBufferRef *);
    using CreateHwDeviceFn = int (*)(AVBufferRef **, enum AVHWDeviceType, const char *, AVDictionary *, int);
    using CreateDerivedHwDeviceFn = int (*)(AVBufferRef **, enum AVHWDeviceType, AVBufferRef *, int);
    using BufferCreateFn = AVBufferRef *(*)(uint8_t *, size_t, void (*)(void *, uint8_t *), void *, int);
    using BufferRefFn = AVBufferRef *(*)(const AVBufferRef *);
    using BufferUnrefFn = void (*)(AVBufferRef **);
    using HwFrameCtxAllocFn = AVBufferRef *(*)(AVBufferRef *);
    using HwFrameCtxInitFn = int (*)(AVBufferRef *);
    using HwFrameGetBufferFn = int (*)(AVBufferRef *, AVFrame *, int);
    using HwFrameTransferDataFn = int (*)(AVFrame *, const AVFrame *, int);
    using OptSetFn = int (*)(void *, const char *, const char *, int);
    using FilterGetByNameFn = const AVFilter *(*)(const char *);
    using FilterGraphAllocFn = AVFilterGraph *(*)();
    using FilterGraphFreeFn = void (*)(AVFilterGraph **);
    using FilterGraphCreateFilterFn = int (*)(AVFilterContext **, const AVFilter *, const char *, const char *, void *, AVFilterGraph *);
    using FilterLinkFn = int (*)(AVFilterContext *, unsigned, AVFilterContext *, unsigned);
    using FilterGraphConfigFn = int (*)(AVFilterGraph *, void *);
    using BufferSrcParametersAllocFn = AVBufferSrcParameters *(*)();
    using BufferSrcParametersSetFn = int (*)(AVFilterContext *, AVBufferSrcParameters *);
    using BufferSrcAddFrameFlagsFn = int (*)(AVFilterContext *, AVFrame *, int);
    using BufferSinkGetFrameFn = int (*)(AVFilterContext *, AVFrame *);
    using BufferSinkGetHwFramesCtxFn = AVBufferRef *(*)(const AVFilterContext *);
    using SwsGetCachedContextFn = SwsContext *(*)(SwsContext *, int, int, AVPixelFormat, int, int, AVPixelFormat, int, SwsFilter *, SwsFilter *, const double *);
    using SwsScaleFn = int (*)(SwsContext *, const uint8_t *const[], const int[], int, int, uint8_t *const[], const int[]);
    using SwsFreeContextFn = void (*)(SwsContext *);

    RuntimeLibrary avutil;
    RuntimeLibrary avcodec;
    RuntimeLibrary avfilter;
    RuntimeLibrary swresample;
    RuntimeLibrary swscale;
    FindCodecByNameFn findEncoderByName = nullptr;
    FindCodecByNameFn findDecoderByName = nullptr;
    AllocContextFn allocContext = nullptr;
    FreeContextFn freeContext = nullptr;
    OpenCodecFn openCodec = nullptr;
    SendFrameFn sendFrame = nullptr;
    ReceivePacketFn receivePacket = nullptr;
    SendPacketFn sendPacket = nullptr;
    ReceiveFrameFn receiveFrame = nullptr;
    FlushBuffersFn flushBuffers = nullptr;
    PacketAllocFn packetAlloc = nullptr;
    PacketFreeFn packetFree = nullptr;
    PacketUnrefFn packetUnref = nullptr;
    NewPacketFn newPacket = nullptr;
    FrameAllocFn frameAlloc = nullptr;
    FrameFreeFn frameFree = nullptr;
    FrameUnrefFn frameUnref = nullptr;
    FrameGetBufferFn frameGetBuffer = nullptr;
    FrameMakeWritableFn frameMakeWritable = nullptr;
    AvFreeFn avFree = nullptr;
    AvStrErrorFn avStrError = nullptr;
    HwDeviceCtxAllocFn hwDeviceCtxAlloc = nullptr;
    HwDeviceCtxInitFn hwDeviceCtxInit = nullptr;
    CreateHwDeviceFn createHwDevice = nullptr;
    CreateDerivedHwDeviceFn createDerivedHwDevice = nullptr;
    BufferCreateFn bufferCreate = nullptr;
    BufferRefFn bufferRef = nullptr;
    BufferUnrefFn bufferUnref = nullptr;
    HwFrameCtxAllocFn hwFrameCtxAlloc = nullptr;
    HwFrameCtxInitFn hwFrameCtxInit = nullptr;
    HwFrameGetBufferFn hwFrameGetBuffer = nullptr;
    HwFrameTransferDataFn hwFrameTransferData = nullptr;
    OptSetFn optSet = nullptr;
    FilterGetByNameFn filterGetByName = nullptr;
    FilterGraphAllocFn filterGraphAlloc = nullptr;
    FilterGraphFreeFn filterGraphFree = nullptr;
    FilterGraphCreateFilterFn filterGraphCreateFilter = nullptr;
    FilterLinkFn filterLink = nullptr;
    FilterGraphConfigFn filterGraphConfig = nullptr;
    BufferSrcParametersAllocFn bufferSrcParametersAlloc = nullptr;
    BufferSrcParametersSetFn bufferSrcParametersSet = nullptr;
    BufferSrcAddFrameFlagsFn bufferSrcAddFrameFlags = nullptr;
    BufferSinkGetFrameFn bufferSinkGetFrame = nullptr;
    BufferSinkGetHwFramesCtxFn bufferSinkGetHwFramesCtx = nullptr;
    SwsGetCachedContextFn swsGetCachedContext = nullptr;
    SwsScaleFn swsScale = nullptr;
    SwsFreeContextFn swsFreeContext = nullptr;
    std::string diagnostics;

    bool load();
    bool openRuntimeLibraries();
    void resolveRuntimeSymbols();
    bool runtimeSymbolsReady() const;
    void updateLoadedDiagnostics();
};

FfmpegApi &api();
bool loaded();
const char *diagnostics();

} // namespace airan::media::ffmpeg

#define avcodec_find_encoder_by_name airan::media::ffmpeg::api().findEncoderByName
#define avcodec_find_decoder_by_name airan::media::ffmpeg::api().findDecoderByName
#define avcodec_alloc_context3 airan::media::ffmpeg::api().allocContext
#define avcodec_free_context airan::media::ffmpeg::api().freeContext
#define avcodec_open2 airan::media::ffmpeg::api().openCodec
#define avcodec_send_frame airan::media::ffmpeg::api().sendFrame
#define avcodec_receive_packet airan::media::ffmpeg::api().receivePacket
#define avcodec_send_packet airan::media::ffmpeg::api().sendPacket
#define avcodec_receive_frame airan::media::ffmpeg::api().receiveFrame
#define avcodec_flush_buffers airan::media::ffmpeg::api().flushBuffers
#define av_packet_alloc airan::media::ffmpeg::api().packetAlloc
#define av_packet_free airan::media::ffmpeg::api().packetFree
#define av_packet_unref airan::media::ffmpeg::api().packetUnref
#define av_new_packet airan::media::ffmpeg::api().newPacket
#define av_frame_alloc airan::media::ffmpeg::api().frameAlloc
#define av_frame_free airan::media::ffmpeg::api().frameFree
#define av_frame_unref airan::media::ffmpeg::api().frameUnref
#define av_frame_get_buffer airan::media::ffmpeg::api().frameGetBuffer
#define av_frame_make_writable airan::media::ffmpeg::api().frameMakeWritable
#define av_free airan::media::ffmpeg::api().avFree
#define av_strerror airan::media::ffmpeg::api().avStrError
#define av_hwdevice_ctx_alloc airan::media::ffmpeg::api().hwDeviceCtxAlloc
#define av_hwdevice_ctx_init airan::media::ffmpeg::api().hwDeviceCtxInit
#define av_hwdevice_ctx_create airan::media::ffmpeg::api().createHwDevice
#define av_hwdevice_ctx_create_derived airan::media::ffmpeg::api().createDerivedHwDevice
#define av_buffer_create airan::media::ffmpeg::api().bufferCreate
#define av_buffer_ref airan::media::ffmpeg::api().bufferRef
#define av_buffer_unref airan::media::ffmpeg::api().bufferUnref
#define av_hwframe_ctx_alloc airan::media::ffmpeg::api().hwFrameCtxAlloc
#define av_hwframe_ctx_init airan::media::ffmpeg::api().hwFrameCtxInit
#define av_hwframe_get_buffer airan::media::ffmpeg::api().hwFrameGetBuffer
#define av_hwframe_transfer_data airan::media::ffmpeg::api().hwFrameTransferData
#define av_opt_set airan::media::ffmpeg::api().optSet
#define avfilter_get_by_name airan::media::ffmpeg::api().filterGetByName
#define avfilter_graph_alloc airan::media::ffmpeg::api().filterGraphAlloc
#define avfilter_graph_free airan::media::ffmpeg::api().filterGraphFree
#define avfilter_graph_create_filter airan::media::ffmpeg::api().filterGraphCreateFilter
#define avfilter_link airan::media::ffmpeg::api().filterLink
#define avfilter_graph_config airan::media::ffmpeg::api().filterGraphConfig
#define av_buffersrc_parameters_alloc airan::media::ffmpeg::api().bufferSrcParametersAlloc
#define av_buffersrc_parameters_set airan::media::ffmpeg::api().bufferSrcParametersSet
#define av_buffersrc_add_frame_flags airan::media::ffmpeg::api().bufferSrcAddFrameFlags
#define av_buffersink_get_frame airan::media::ffmpeg::api().bufferSinkGetFrame
#define av_buffersink_get_hw_frames_ctx airan::media::ffmpeg::api().bufferSinkGetHwFramesCtx
#define sws_getCachedContext airan::media::ffmpeg::api().swsGetCachedContext
#define sws_scale airan::media::ffmpeg::api().swsScale
#define sws_freeContext airan::media::ffmpeg::api().swsFreeContext

#endif
