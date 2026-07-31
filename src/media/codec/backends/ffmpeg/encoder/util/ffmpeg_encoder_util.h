#pragma once

#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_types.h"

#include <api/video/i420_buffer.h>

#include <cstdint>
#include <string>
#include <vector>

#if defined(AIRAN_HAVE_FFMPEG)
namespace airan::media::ffmpeg
{

std::string ffmpegErrorText(int error);
std::vector<uint8_t> encodedPacketData(const AVCodecContext *ctx, const AVPacket *packet, bool h264KeyFrame);
int safeAvcodecSendFrame(AVCodecContext *ctx, const AVFrame *frame, const char *backend);
int safeAvcodecReceivePacket(AVCodecContext *ctx, AVPacket *packet, const char *backend);
void configureH264CodecContext(AVCodecContext *ctx);
void configureRealtimeEncoderOptions(AVCodecContext *ctx, const CodecProbe *probe);
bool copyI420ToFrame(const webrtc::I420BufferInterface &i420, AVFrame *frame);
uint32_t codecBitrateBps(uint32_t bitrateKbps);
int codecFramerateFps(uint32_t framerate);

} // namespace airan::media::ffmpeg
#endif
