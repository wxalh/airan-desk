#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_probe_internal.h"
#include "media/codec/backends/ffmpeg/codec/ffmpeg_openh264_option.h"
#include "util/config/config_util.h"

#if defined(AIRAN_HAVE_FFMPEG) && defined(__linux__)
#include <signal.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace airan::media::ffmpeg
{

#if defined(AIRAN_HAVE_FFMPEG)
namespace
{
#if defined(__linux__)
constexpr useconds_t kProbePollIntervalUs = 10000;
constexpr int kProbeTimeoutPolls = 500;

bool runtimeLibraryHasSymbol(const char *libraryName, const char *symbolName)
{
    void *library = dlopen(libraryName, RTLD_LAZY | RTLD_LOCAL);
    if (!library)
        return false;

    dlerror();
    void *symbol = dlsym(library, symbolName);
    const bool found = symbol != nullptr && dlerror() == nullptr;
    dlclose(library);
    return found;
}

bool probeNeedsCompatibleLibva(const CodecProbe &probe)
{
    return probe.deviceType == AV_HWDEVICE_TYPE_VAAPI ||
           probe.deviceType == AV_HWDEVICE_TYPE_QSV ||
           probe.hardwarePixelFormat == AV_PIX_FMT_VAAPI ||
           probe.hardwarePixelFormat == AV_PIX_FMT_QSV;
}

bool childExitedSuccessfully(pid_t pid)
{
    int status = 0;
    for (int i = 0; i < kProbeTimeoutPolls; ++i)
    {
        const pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid)
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (result < 0)
            return false;
        usleep(kProbePollIntervalUs);
    }

    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return false;
}
#endif

AVPixelFormat probeGetFormat(AVCodecContext *ctx, const AVPixelFormat *formats)
{
    if (!formats)
        return AV_PIX_FMT_NONE;
    auto *probe = static_cast<const CodecProbe *>(ctx ? ctx->opaque : nullptr);
    if (probe)
    {
        for (const AVPixelFormat *fmt = formats; *fmt != AV_PIX_FMT_NONE; ++fmt)
            if (*fmt == probe->hardwarePixelFormat)
                return *fmt;
        if (probe->hardwarePixelFormat != AV_PIX_FMT_NONE)
            return AV_PIX_FMT_NONE;
    }
    return formats[0] == AV_PIX_FMT_NONE ? AV_PIX_FMT_NONE : formats[0];
}
} // namespace


bool probeRuntimeDependenciesAvailable(const CodecProbe &probe)
{
#if defined(__linux__)
    if (probeNeedsCompatibleLibva(probe) && !runtimeLibraryHasSymbol("libva.so.2", "vaSyncBuffer"))
        return false;
#else
    (void)probe;
#endif
    return true;
}


bool createHardwareDevice(const CodecProbe &probe, AVBufferRef **device)
{
    if (probe.deviceType == AV_HWDEVICE_TYPE_NONE)
        return true;
    return av_hwdevice_ctx_create(device, probe.deviceType, nullptr, nullptr, 0) >= 0;
}


bool createHardwareFrames(const CodecProbe &probe, AVCodecContext *ctx, AVBufferRef *device, AVBufferRef **framesRef)
{
    if (!ctx || !device || probe.hardwarePixelFormat == AV_PIX_FMT_NONE)
        return true;

    *framesRef = av_hwframe_ctx_alloc(device);
    if (!*framesRef)
        return false;

    auto *frames = reinterpret_cast<AVHWFramesContext *>((*framesRef)->data);
    frames->format = probe.hardwarePixelFormat;
    frames->sw_format = probe.softwarePixelFormat;
    frames->width = ctx->width;
    frames->height = ctx->height;
    frames->initial_pool_size = 8;
    return av_hwframe_ctx_init(*framesRef) >= 0;
}


bool openCodecProbeInProcess(const CodecProbe &probe, bool encoder, bool hardwareFrames)
{
    const AVCodec *codec = encoder ? avcodec_find_encoder_by_name(probe.ffmpegCodec)
                                   : avcodec_find_decoder_by_name(probe.ffmpegCodec);
    if (!codec)
        return false;

    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (!ctx)
        return false;
    if (!encoder)
    {
        ctx->opaque = const_cast<CodecProbe *>(&probe);
        ctx->get_format = &probeGetFormat;
    }

    const bool needsHardwareDevice = !encoder || hardwareFrames;
    AVBufferRef *device = nullptr;
    if (needsHardwareDevice && !createHardwareDevice(probe, &device))
    {
        avcodec_free_context(&ctx);
        return false;
    }
    if (device)
        ctx->hw_device_ctx = av_buffer_ref(device);
    if (device && !ctx->hw_device_ctx)
    {
        avcodec_free_context(&ctx);
        av_buffer_unref(&device);
        return false;
    }

    AVBufferRef *framesRef = nullptr;
    if (encoder)
    {
        ctx->width = 1280;
        ctx->height = 720;
        ctx->time_base = AVRational{1, 30};
        ctx->pkt_timebase = ctx->time_base;
        ctx->framerate = AVRational{30, 1};
        ctx->bit_rate = 4000000;
        ctx->gop_size = 60;
        ctx->max_b_frames = 0;
        ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        ctx->pix_fmt = hardwareFrames && probe.hardwarePixelFormat != AV_PIX_FMT_NONE ? probe.hardwarePixelFormat
                                                                                       : probe.softwarePixelFormat;

        if (hardwareFrames && device && probe.hardwarePixelFormat != AV_PIX_FMT_NONE)
        {
            if (!createHardwareFrames(probe, ctx, device, &framesRef))
            {
                av_buffer_unref(&framesRef);
                avcodec_free_context(&ctx);
                av_buffer_unref(&device);
                return false;
            }
            ctx->hw_frames_ctx = av_buffer_ref(framesRef);
            if (!ctx->hw_frames_ctx)
            {
                av_buffer_unref(&framesRef);
                avcodec_free_context(&ctx);
                av_buffer_unref(&device);
                return false;
            }
        }
    }

    QString openh264Reason;
    const bool optionReady = configureOpenH264LibraryOption(
        probe, ctx, ConfigUtil->openh264_enabled, ConfigUtil->openh264_library_path,
        api().optSet, &openh264Reason);
    const bool ok = optionReady && avcodec_open2(ctx, codec, nullptr) >= 0;
    av_buffer_unref(&framesRef);
    avcodec_free_context(&ctx);
    av_buffer_unref(&device);
    return ok;
}


#if defined(__linux__)
bool openCodecProbeIsolated(const CodecProbe &probe, bool encoder, bool hardwareFrames)
{
    const pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0)
    {
        const bool ok = openCodecProbeInProcess(probe, encoder, hardwareFrames);
        _exit(ok ? 0 : 1);
    }
    return childExitedSuccessfully(pid);
}
#endif
#endif


bool openCodecProbe(const CodecProbe &probe, bool encoder, bool hardwareFrames)
{
#if defined(AIRAN_HAVE_FFMPEG)
    if (!loaded())
        return false;
    if (!probeRuntimeDependenciesAvailable(probe))
        return false;
#if defined(__linux__)
    return openCodecProbeIsolated(probe, encoder, hardwareFrames);
#else
    return openCodecProbeInProcess(probe, encoder, hardwareFrames);
#endif
#else
    (void)probe;
    (void)encoder;
    (void)hardwareFrames;
    return false;
#endif
}

} // namespace airan::media::ffmpeg
