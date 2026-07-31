#include "ui/settings/audio/settings_audio_backend.h"
#include "ui/settings/audio/settings_audio_backend_internal.h"

#if !defined(Q_OS_WIN64) && !defined(Q_OS_WIN32) && !defined(Q_OS_LINUX) && !defined(Q_OS_MACOS)

#include <QApplication>
#include <QObject>

namespace SettingsAudioBackend
{

QList<AudioDeviceItem> enumerateAudioDevices()
{
    return {};
}

bool playToneOnOutput(const QString &)
{
    QApplication::beep();
    return true;
}

void runMicLevelTest(const QString &, std::atomic_bool *, QObject *receiver)
{
    Internal::postMicStopped(receiver);
}

} // namespace SettingsAudioBackend
#endif
