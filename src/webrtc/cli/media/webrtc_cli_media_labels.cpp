#include "webrtc/cli/media/webrtc_cli_media_labels.h"

#include "rtc/core/rtc.hpp"

namespace webrtc_cli_internal
{

QString localOsName()
{
#if defined(Q_OS_ANDROID)
    return QStringLiteral("android");
#elif defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("unknown");
#endif
}

QString desktopCaptureMethodLabel()
{
    const QString actual = QString::fromStdString(rtc::currentDesktopCaptureBackend()).trimmed();
    if (!actual.isEmpty())
        return actual;
    return QStringLiteral("auto");
}

QString encoderBackendFromImplementation(const QString &implementation)
{
    const QString normalized = implementation.toLower();
    if (normalized.contains(QStringLiteral("openh264")) ||
        normalized.contains(QStringLiteral("libx264")) ||
        normalized.contains(QStringLiteral("software")) ||
        normalized.contains(QStringLiteral("libvpx")) ||
        normalized.contains(QStringLiteral("google")))
        return QStringLiteral("software");
    if (normalized.contains(QStringLiteral("mediafoundation")) ||
        normalized == QStringLiteral("mf") ||
        normalized.contains(QStringLiteral("mf ")))
        return QStringLiteral("mf");
    if (normalized.contains(QStringLiteral("qsv")) ||
        normalized.contains(QStringLiteral("intel")) ||
        normalized.contains(QStringLiteral("quick sync")))
        return QStringLiteral("qsv");
    if (normalized.contains(QStringLiteral("nvenc")) ||
        normalized.contains(QStringLiteral("nvidia")))
        return QStringLiteral("nvidia");
    if (normalized.contains(QStringLiteral("amf")) ||
        normalized.contains(QStringLiteral("amd")))
        return QStringLiteral("amf");
    if (normalized.contains(QStringLiteral("vaapi")))
        return QStringLiteral("vaapi");
    if (normalized.contains(QStringLiteral("videotoolbox")))
        return QStringLiteral("videotoolbox");
    if (normalized.contains(QStringLiteral("hardware")))
        return QStringLiteral("hw");
    return implementation.isEmpty() ? QStringLiteral("hw_preferred") : normalized.simplified().replace(QLatin1Char(' '), QLatin1Char('_'));
}

QString encoderTypeFromBackend(const QString &backend)
{
    if (backend == QStringLiteral("software"))
        return QStringLiteral("software");
    if (backend == QStringLiteral("hw_preferred"))
        return QStringLiteral("hardware_preferred");
    if (backend == QStringLiteral("auto"))
        return QStringLiteral("auto");
    return QStringLiteral("hardware");
}

QString readableEncoderName(const QString &implementation, const QString &codec)
{
    QString codecName = codec.trimmed().isEmpty() ? QStringLiteral("H264") : codec.trimmed();
    codecName.replace(QLatin1Char('/'), QLatin1Char('_'));
    codecName = codecName.toUpper();
    const QString backend = encoderBackendFromImplementation(implementation);
    return QStringLiteral("%1_%2").arg(codecName, backend);
}

QString encoderTypeFromImplementation(const QString &implementation)
{
    return encoderTypeFromBackend(encoderBackendFromImplementation(implementation));
}

} /* namespace webrtc_cli_internal */
