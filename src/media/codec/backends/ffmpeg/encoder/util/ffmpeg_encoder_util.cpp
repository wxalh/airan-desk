#include "media/codec/backends/ffmpeg/encoder/util/ffmpeg_encoder_util.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include <cstdio>

namespace airan::media::ffmpeg
{

std::string ffmpegErrorText(int error)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror && av_strerror(error, buffer, sizeof(buffer)) >= 0)
        return std::string(buffer);
    std::snprintf(buffer, sizeof(buffer), "ffmpeg error %d", error);
    return std::string(buffer);
}
} /* namespace airan::media::ffmpeg */
#endif
