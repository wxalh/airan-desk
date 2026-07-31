#include "media/codec/backends/ffmpeg/codec/ffmpeg_openh264_option.h"

#include <cstring>

namespace airan::media::ffmpeg
{

#if defined(AIRAN_HAVE_FFMPEG)
namespace
{
bool isOpenH264Probe(const CodecProbe &probe)
{
    return probe.ffmpegCodec && std::strcmp(probe.ffmpegCodec, "libopenh264") == 0;
}

void setReason(QString *reasonCode, const QString &reason)
{
    if (reasonCode)
        *reasonCode = reason;
}
} // namespace

bool configureOpenH264LibraryOption(const CodecProbe &probe,
                                    AVCodecContext *context,
                                    bool enabled,
                                    const QString &configuredPath,
                                    OpenH264OptionSetter optionSetter,
                                    QString *reasonCode,
                                    const QString &storageRoot)
{
    if (reasonCode)
        reasonCode->clear();
    if (!isOpenH264Probe(probe))
        return true;
    if (!context || !context->priv_data || !optionSetter)
    {
        setReason(reasonCode, QStringLiteral("invalid-context"));
        return false;
    }

    const openh264::ValidationResult availability =
        openh264::currentAvailability(enabled, configuredPath, storageRoot);
    if (availability.availability != openh264::Availability::Ready)
    {
        setReason(reasonCode, availability.reasonCode);
        return false;
    }

    const QByteArray utf8Path = availability.absolutePath.toUtf8();
    if (utf8Path.isEmpty() ||
        optionSetter(context->priv_data, "openh264_library", utf8Path.constData(), 0) < 0)
    {
        setReason(reasonCode, QStringLiteral("option-unavailable"));
        return false;
    }
    return true;
}
#endif

} // namespace airan::media::ffmpeg
