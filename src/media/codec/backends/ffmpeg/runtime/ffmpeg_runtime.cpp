#include "media/codec/backends/ffmpeg/runtime/ffmpeg_runtime.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include <mutex>

namespace airan::media::ffmpeg
{


bool FfmpegApi::load()
{
    diagnostics.clear();
    if (!openRuntimeLibraries())
        return false;

    resolveRuntimeSymbols();
    if (!runtimeSymbolsReady())
    {
        diagnostics = "FFmpeg DLLs loaded but one or more required symbols are missing";
        return false;
    }

    updateLoadedDiagnostics();
    return true;
}

FfmpegApi &api()
{
    static FfmpegApi api;
    static std::once_flag flag;
    static bool isLoaded = false;
    std::call_once(flag, []() { isLoaded = api.load(); });
    (void)isLoaded;
    return api;
}

bool loaded()
{
    return api().findEncoderByName != nullptr;
}

const char *diagnostics()
{
    return api().diagnostics.c_str();
}

} // namespace airan::media::ffmpeg

#endif
