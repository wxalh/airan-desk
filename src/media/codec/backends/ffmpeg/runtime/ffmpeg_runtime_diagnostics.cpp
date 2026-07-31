#include "media/codec/backends/ffmpeg/runtime/ffmpeg_runtime.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include <sstream>

namespace airan::media::ffmpeg
{


void FfmpegApi::updateLoadedDiagnostics()
{
    std::ostringstream stream;
    stream << "loaded avutil=" << avutil.loadedName()
           << ", swresample=" << swresample.loadedName()
           << ", avcodec=" << avcodec.loadedName()
           << ", avfilter=" << avfilter.loadedName()
           << ", swscale=" << swscale.loadedName();
    diagnostics = stream.str();
}

} // namespace airan::media::ffmpeg

#endif
