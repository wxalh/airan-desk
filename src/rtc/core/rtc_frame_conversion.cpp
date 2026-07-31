#include "rtc/core/rtc_internal.h"

#include <libyuv/convert_argb.h>

namespace rtc
{

bool convertFrameToQtBgra(const webrtc::VideoFrame &frame, uint8_t *dst, int width, int height)
{
    if (!dst || width <= 0 || height <= 0)
        return false;

    auto buffer = frame.video_frame_buffer();
    if (!buffer)
        return false;

    auto i420 = buffer->ToI420();
    if (!i420)
        return false;

    return libyuv::I420ToARGBMatrix(i420->DataY(),
                                    i420->StrideY(),
                                    i420->DataU(),
                                    i420->StrideU(),
                                    i420->DataV(),
                                    i420->StrideV(),
                                    dst,
                                    width * 4,
                                    &libyuv::kYuvH709Constants,
                                    width,
                                    height) == 0;
}

} // namespace rtc
