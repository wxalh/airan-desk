#include "media/codec/backends/ffmpeg/runtime/ffmpeg_runtime.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include <iterator>

namespace airan::media::ffmpeg
{


bool FfmpegApi::openRuntimeLibraries()
{
#if defined(_WIN32)
    const char *const avutilNames[] = {"codecs/avutil-60.dll", "codecs/avutil-59.dll", "avutil-60.dll", "avutil-59.dll", "avutil.dll"};
    const char *const swresampleNames[] = {"codecs/swresample-6.dll", "codecs/swresample-5.dll", "swresample-6.dll", "swresample-5.dll", "swresample.dll"};
    const char *const avcodecNames[] = {"codecs/avcodec-62.dll", "codecs/avcodec-61.dll", "avcodec-62.dll", "avcodec-61.dll", "avcodec.dll"};
    const char *const avfilterNames[] = {"codecs/avfilter-11.dll", "codecs/avfilter-10.dll", "avfilter-11.dll", "avfilter-10.dll", "avfilter.dll"};
    const char *const swscaleNames[] = {"codecs/swscale-9.dll", "codecs/swscale-8.dll", "swscale-9.dll", "swscale-8.dll", "swscale.dll"};
#elif defined(__linux__)
    const char *const avutilNames[] = {"codecs/libavutil.so.60", "codecs/libavutil.so.59", "libavutil.so.60", "libavutil.so.59", "libavutil.so"};
    const char *const swresampleNames[] = {"codecs/libswresample.so.6", "codecs/libswresample.so.5", "libswresample.so.6", "libswresample.so.5", "libswresample.so"};
    const char *const avcodecNames[] = {"codecs/libavcodec.so.62", "codecs/libavcodec.so.61", "libavcodec.so.62", "libavcodec.so.61", "libavcodec.so"};
    const char *const avfilterNames[] = {"codecs/libavfilter.so.11", "codecs/libavfilter.so.10", "libavfilter.so.11", "libavfilter.so.10", "libavfilter.so"};
    const char *const swscaleNames[] = {"codecs/libswscale.so.9", "codecs/libswscale.so.8", "libswscale.so.9", "libswscale.so.8", "libswscale.so"};
#else
    const char *const avutilNames[] = {nullptr};
    const char *const swresampleNames[] = {nullptr};
    const char *const avcodecNames[] = {nullptr};
    const char *const avfilterNames[] = {nullptr};
    const char *const swscaleNames[] = {nullptr};
#endif

    if (!avutil.openAny(avutilNames, std::size(avutilNames)))
    {
        diagnostics = "failed to load avutil runtime DLL";
        return false;
    }
    if (!swresample.openAny(swresampleNames, std::size(swresampleNames)))
    {
        diagnostics = "failed to load swresample runtime DLL";
        return false;
    }
    if (!avcodec.openAny(avcodecNames, std::size(avcodecNames)))
    {
        diagnostics = "failed to load avcodec runtime DLL";
        return false;
    }
    if (!avfilter.openAny(avfilterNames, std::size(avfilterNames)))
    {
        diagnostics = "failed to load avfilter runtime DLL";
        return false;
    }
    if (!swscale.openAny(swscaleNames, std::size(swscaleNames)))
    {
        diagnostics = "failed to load swscale runtime DLL";
        return false;
    }
    return true;
}

} // namespace airan::media::ffmpeg

#endif
