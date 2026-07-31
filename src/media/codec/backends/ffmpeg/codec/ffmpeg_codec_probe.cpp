#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_probe_internal.h"

namespace airan::media::ffmpeg
{

namespace
{
#if defined(AIRAN_HAVE_FFMPEG)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
bool codecPixelFormatsContain(const AVCodec *codec, AVPixelFormat pixelFormat)
{
    if (!codec || pixelFormat == AV_PIX_FMT_NONE)
        return false;
    if (!codec->pix_fmts)
        return true;
    for (const AVPixelFormat *format = codec->pix_fmts; *format != AV_PIX_FMT_NONE; ++format)
    {
        if (*format == pixelFormat)
            return true;
    }
    return false;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif
} // namespace


#if defined(AIRAN_HAVE_FFMPEG)
bool encoderProbeAcceptsPixelFormat(const CodecProbe &probe, AVPixelFormat pixelFormat)
{
    if (pixelFormat == AV_PIX_FMT_NONE || !loaded())
        return false;
    const AVCodec *codec = avcodec_find_encoder_by_name(probe.ffmpegCodec);
    return codecPixelFormatsContain(codec, pixelFormat);
}
#endif


bool openEncoderProbe(const CodecProbe &probe)
{
#if defined(AIRAN_HAVE_FFMPEG)
    if (probe.hardwarePixelFormat != AV_PIX_FMT_NONE && openCodecProbe(probe, true, true))
        return true;
    return encoderProbeAcceptsPixelFormat(probe, probe.softwarePixelFormat) && openCodecProbe(probe, true, false);
#else
    (void)probe;
    return false;
#endif
}

bool openDecoderProbe(const CodecProbe &probe)
{
#if defined(AIRAN_HAVE_FFMPEG)
    return openCodecProbe(probe, false, true);
#else
    (void)probe;
    return false;
#endif
}

} // namespace airan::media::ffmpeg
