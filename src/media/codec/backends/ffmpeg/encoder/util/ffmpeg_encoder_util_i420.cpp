#include "media/codec/backends/ffmpeg/encoder/util/ffmpeg_encoder_util.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include <algorithm>
#include <cstring>

namespace airan::media::ffmpeg
{
namespace
{

void copyPlane(uint8_t *dst, int dstStride, const uint8_t *src, int srcStride, int width, int height)
{
    for (int y = 0; y < height; ++y)
        std::memcpy(dst + y * dstStride, src + y * srcStride, static_cast<size_t>(width));
}

void fillPlane(uint8_t *dst, int dstStride, int width, int height, uint8_t value)
{
    for (int y = 0; y < height; ++y)
        std::memset(dst + y * dstStride, value, static_cast<size_t>(width));
}
} /* namespace */


bool copyI420ToFrame(const webrtc::I420BufferInterface &i420, AVFrame *frame)
{
    if (!frame)
        return false;
    const int copyWidth = (std::min)(i420.width(), frame->width);
    const int copyHeight = (std::min)(i420.height(), frame->height);
    if (copyWidth <= 0 || copyHeight <= 0)
        return false;

    if (frame->format == AV_PIX_FMT_NV12)
    {
        fillPlane(frame->data[0], frame->linesize[0], frame->width, frame->height, 16);
        copyPlane(frame->data[0], frame->linesize[0], i420.DataY(), i420.StrideY(), copyWidth, copyHeight);
        const int chromaWidth = (copyWidth + 1) / 2;
        const int chromaHeight = (copyHeight + 1) / 2;
        const int frameChromaWidth = (frame->width + 1) / 2;
        const int frameChromaHeight = (frame->height + 1) / 2;
        fillPlane(frame->data[1], frame->linesize[1], frameChromaWidth * 2, frameChromaHeight, 128);
        for (int y = 0; y < chromaHeight; ++y)
        {
            uint8_t *uv = frame->data[1] + y * frame->linesize[1];
            const uint8_t *u = i420.DataU() + y * i420.StrideU();
            const uint8_t *v = i420.DataV() + y * i420.StrideV();
            for (int x = 0; x < chromaWidth; ++x)
            {
                uv[x * 2] = u[x];
                uv[x * 2 + 1] = v[x];
            }
        }
        return true;
    }

    fillPlane(frame->data[0], frame->linesize[0], frame->width, frame->height, 16);
    copyPlane(frame->data[0], frame->linesize[0], i420.DataY(), i420.StrideY(), copyWidth, copyHeight);
    const int chromaCopyWidth = (copyWidth + 1) / 2;
    const int chromaCopyHeight = (copyHeight + 1) / 2;
    const int chromaFrameWidth = (frame->width + 1) / 2;
    const int chromaFrameHeight = (frame->height + 1) / 2;
    fillPlane(frame->data[1], frame->linesize[1], chromaFrameWidth, chromaFrameHeight, 128);
    fillPlane(frame->data[2], frame->linesize[2], chromaFrameWidth, chromaFrameHeight, 128);
    copyPlane(frame->data[1], frame->linesize[1], i420.DataU(), i420.StrideU(), chromaCopyWidth, chromaCopyHeight);
    copyPlane(frame->data[2], frame->linesize[2], i420.DataV(), i420.StrideV(), chromaCopyWidth, chromaCopyHeight);
    return true;
}
} /* namespace airan::media::ffmpeg */
#endif
