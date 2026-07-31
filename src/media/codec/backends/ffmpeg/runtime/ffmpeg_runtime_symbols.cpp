#include "media/codec/backends/ffmpeg/runtime/ffmpeg_runtime.h"

#if defined(AIRAN_HAVE_FFMPEG)

namespace airan::media::ffmpeg
{


void FfmpegApi::resolveRuntimeSymbols()
{
    findEncoderByName = avcodec.resolve<FindCodecByNameFn>("avcodec_find_encoder_by_name");
    findDecoderByName = avcodec.resolve<FindCodecByNameFn>("avcodec_find_decoder_by_name");
    allocContext = avcodec.resolve<AllocContextFn>("avcodec_alloc_context3");
    freeContext = avcodec.resolve<FreeContextFn>("avcodec_free_context");
    openCodec = avcodec.resolve<OpenCodecFn>("avcodec_open2");
    sendFrame = avcodec.resolve<SendFrameFn>("avcodec_send_frame");
    receivePacket = avcodec.resolve<ReceivePacketFn>("avcodec_receive_packet");
    sendPacket = avcodec.resolve<SendPacketFn>("avcodec_send_packet");
    receiveFrame = avcodec.resolve<ReceiveFrameFn>("avcodec_receive_frame");
    flushBuffers = avcodec.resolve<FlushBuffersFn>("avcodec_flush_buffers");
    packetAlloc = avcodec.resolve<PacketAllocFn>("av_packet_alloc");
    packetFree = avcodec.resolve<PacketFreeFn>("av_packet_free");
    packetUnref = avcodec.resolve<PacketUnrefFn>("av_packet_unref");
    newPacket = avcodec.resolve<NewPacketFn>("av_new_packet");
    frameAlloc = avutil.resolve<FrameAllocFn>("av_frame_alloc");
    frameFree = avutil.resolve<FrameFreeFn>("av_frame_free");
    frameUnref = avutil.resolve<FrameUnrefFn>("av_frame_unref");
    frameGetBuffer = avutil.resolve<FrameGetBufferFn>("av_frame_get_buffer");
    frameMakeWritable = avutil.resolve<FrameMakeWritableFn>("av_frame_make_writable");
    avFree = avutil.resolve<AvFreeFn>("av_free");
    avStrError = avutil.resolve<AvStrErrorFn>("av_strerror");
    hwDeviceCtxAlloc = avutil.resolve<HwDeviceCtxAllocFn>("av_hwdevice_ctx_alloc");
    hwDeviceCtxInit = avutil.resolve<HwDeviceCtxInitFn>("av_hwdevice_ctx_init");
    createHwDevice = avutil.resolve<CreateHwDeviceFn>("av_hwdevice_ctx_create");
    createDerivedHwDevice = avutil.resolve<CreateDerivedHwDeviceFn>("av_hwdevice_ctx_create_derived");
    bufferCreate = avutil.resolve<BufferCreateFn>("av_buffer_create");
    bufferRef = avutil.resolve<BufferRefFn>("av_buffer_ref");
    bufferUnref = avutil.resolve<BufferUnrefFn>("av_buffer_unref");
    hwFrameCtxAlloc = avutil.resolve<HwFrameCtxAllocFn>("av_hwframe_ctx_alloc");
    hwFrameCtxInit = avutil.resolve<HwFrameCtxInitFn>("av_hwframe_ctx_init");
    hwFrameGetBuffer = avutil.resolve<HwFrameGetBufferFn>("av_hwframe_get_buffer");
    hwFrameTransferData = avutil.resolve<HwFrameTransferDataFn>("av_hwframe_transfer_data");
    optSet = avutil.resolve<OptSetFn>("av_opt_set");
    filterGetByName = avfilter.resolve<FilterGetByNameFn>("avfilter_get_by_name");
    filterGraphAlloc = avfilter.resolve<FilterGraphAllocFn>("avfilter_graph_alloc");
    filterGraphFree = avfilter.resolve<FilterGraphFreeFn>("avfilter_graph_free");
    filterGraphCreateFilter = avfilter.resolve<FilterGraphCreateFilterFn>("avfilter_graph_create_filter");
    filterLink = avfilter.resolve<FilterLinkFn>("avfilter_link");
    filterGraphConfig = avfilter.resolve<FilterGraphConfigFn>("avfilter_graph_config");
    bufferSrcParametersAlloc = avfilter.resolve<BufferSrcParametersAllocFn>("av_buffersrc_parameters_alloc");
    bufferSrcParametersSet = avfilter.resolve<BufferSrcParametersSetFn>("av_buffersrc_parameters_set");
    bufferSrcAddFrameFlags = avfilter.resolve<BufferSrcAddFrameFlagsFn>("av_buffersrc_add_frame_flags");
    bufferSinkGetFrame = avfilter.resolve<BufferSinkGetFrameFn>("av_buffersink_get_frame");
    bufferSinkGetHwFramesCtx = avfilter.resolve<BufferSinkGetHwFramesCtxFn>("av_buffersink_get_hw_frames_ctx");
    swsGetCachedContext = swscale.resolve<SwsGetCachedContextFn>("sws_getCachedContext");
    swsScale = swscale.resolve<SwsScaleFn>("sws_scale");
    swsFreeContext = swscale.resolve<SwsFreeContextFn>("sws_freeContext");
}


bool FfmpegApi::runtimeSymbolsReady() const
{
    return findEncoderByName && findDecoderByName && allocContext && freeContext && openCodec &&
           sendFrame && receivePacket && sendPacket && receiveFrame && flushBuffers && packetAlloc && packetFree &&
           packetUnref && newPacket && frameAlloc && frameFree && frameUnref && frameGetBuffer &&
           frameMakeWritable && avFree && avStrError &&
           hwDeviceCtxAlloc && hwDeviceCtxInit && createHwDevice && createDerivedHwDevice &&
           bufferCreate && bufferRef && bufferUnref && hwFrameCtxAlloc &&
           hwFrameCtxInit && hwFrameGetBuffer && hwFrameTransferData && optSet &&
           filterGetByName && filterGraphAlloc && filterGraphFree && filterGraphCreateFilter &&
           filterLink && filterGraphConfig && bufferSrcParametersAlloc && bufferSrcParametersSet &&
           bufferSrcAddFrameFlags && bufferSinkGetFrame && bufferSinkGetHwFramesCtx &&
           swsGetCachedContext && swsScale && swsFreeContext;
}

} // namespace airan::media::ffmpeg

#endif
