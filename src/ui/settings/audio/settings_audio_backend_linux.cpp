#include "ui/settings/audio/settings_audio_backend.h"
#include "ui/settings/audio/settings_audio_backend_internal.h"

#if defined(Q_OS_LINUX)

#include <QApplication>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QStringList>

namespace SettingsAudioBackend
{
namespace
{
QByteArray runPactl(const QStringList &arguments, int timeoutMs)
{
    QProcess process;
    process.start(QStringLiteral("pactl"), arguments);
    if (!process.waitForStarted(timeoutMs) || !process.waitForFinished(timeoutMs))
    {
        if (process.state() != QProcess::NotRunning)
        {
            process.kill();
            process.waitForFinished(200);
        }
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};
    return process.readAllStandardOutput();
}

QString defaultPulseSinkName()
{
    const QString sink = QString::fromUtf8(
                             runPactl(QStringList() << QStringLiteral("get-default-sink"), 500))
                             .trimmed();
    if (!sink.isEmpty())
        return sink;

    const QByteArray info = runPactl(QStringList() << QStringLiteral("info"), 500);
    if (info.isEmpty())
        return {};

    const QString output = QString::fromUtf8(info);
    for (const QString &line : output.split(QLatin1Char('\n')))
    {
        if (!line.startsWith(QStringLiteral("Default Sink:")))
            continue;
        return line.mid(QStringLiteral("Default Sink:").size()).trimmed();
    }
    return {};
}
} // namespace

QList<AudioDeviceItem> enumerateAudioDevices()
{
    QList<AudioDeviceItem> devices;
    QProcess process;
    process.start(QStringLiteral("pactl"),
                  QStringList() << QStringLiteral("list") << QStringLiteral("short") << QStringLiteral("sources"));
    if (!process.waitForFinished(1200) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return devices;

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    for (const QString &line : output.split(QLatin1Char('\n')))
    {
        const QStringList parts = line.split(QLatin1Char('\t'));
        if (parts.size() < 2)
            continue;
        const QString name = parts[1].trimmed();
        if (name.isEmpty())
            continue;
        AudioDeviceItem item;
        item.id = name;
        item.displayName = name;
        item.loopback = name.endsWith(QStringLiteral(".monitor"));
        devices.push_back(item);
    }
    return devices;
}

bool playToneOnOutput(const QString &configuredOutput)
{
    QString sink = configuredOutput.trimmed();
    if (sink.endsWith(QStringLiteral(".monitor")))
        sink.chop(static_cast<int>(QStringLiteral(".monitor").size()));

    if (sink == QStringLiteral("@DEFAULT_MONITOR@") || sink.isEmpty())
        sink = defaultPulseSinkName();

    const QStringList candidates{
        QStringLiteral("/usr/share/sounds/freedesktop/stereo/bell.oga"),
        QStringLiteral("/usr/share/sounds/freedesktop/stereo/complete.oga"),
        QStringLiteral("/usr/share/sounds/alsa/Front_Center.wav")};
    QString soundFile;
    for (const QString &candidate : candidates)
    {
        if (QFile::exists(candidate))
        {
            soundFile = candidate;
            break;
        }
    }

    if (!soundFile.isEmpty())
    {
        QStringList args;
        if (!sink.isEmpty())
            args << QStringLiteral("--device=") + sink;
        args << soundFile;
        if (QProcess::startDetached(QStringLiteral("paplay"), args))
            return true;
    }

    QApplication::beep();
    return true;
}

void runMicLevelTest(const QString &, std::atomic_bool *, QObject *receiver)
{
    Internal::postMicStopped(receiver);
}

} // namespace SettingsAudioBackend
#endif
