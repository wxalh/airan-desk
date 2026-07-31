#include "media/codec/backends/ffmpeg/encoder/util/ffmpeg_encoder_util.h"

#if defined(AIRAN_HAVE_FFMPEG)

#if defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm>
#include <limits>

namespace airan::media::ffmpeg
{
namespace
{

void logFfmpegEncoderSehCrash(const char *api, const char *backend)
{
#if defined(_WIN32)
    std::string message = "Airan FFmpeg encoder crashed inside ";
    message += api ? api : "FFmpeg";
    message += "; backend=";
    message += backend ? backend : "";
    message += "\n";
    OutputDebugStringA(message.c_str());
#else
    (void)api;
    (void)backend;
#endif
}
} /* namespace */


int safeAvcodecSendFrame(AVCodecContext *ctx, const AVFrame *frame, const char *backend)
{
#if defined(_WIN32)
    __try
    {
        return avcodec_send_frame(ctx, frame);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        logFfmpegEncoderSehCrash("avcodec_send_frame", backend);
        return AVERROR_EXTERNAL;
    }
#else
    (void)backend;
    return avcodec_send_frame(ctx, frame);
#endif
}


int safeAvcodecReceivePacket(AVCodecContext *ctx, AVPacket *packet, const char *backend)
{
#if defined(_WIN32)
    __try
    {
        return avcodec_receive_packet(ctx, packet);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        logFfmpegEncoderSehCrash("avcodec_receive_packet", backend);
        return AVERROR_EXTERNAL;
    }
#else
    (void)backend;
    return avcodec_receive_packet(ctx, packet);
#endif
}


uint32_t codecBitrateBps(uint32_t bitrateKbps)
{
    constexpr uint32_t kMaxCodecBitrateBps = static_cast<uint32_t>((std::numeric_limits<int>::max)());
    return bitrateKbps > kMaxCodecBitrateBps / 1000
               ? kMaxCodecBitrateBps
               : bitrateKbps * 1000;
}


int codecFramerateFps(uint32_t framerate)
{
    constexpr uint32_t kMaxCodecFramerate = 1000;
    return static_cast<int>((std::min)((std::max)(framerate, uint32_t{1}), kMaxCodecFramerate));
}
} /* namespace airan::media::ffmpeg */
#endif
