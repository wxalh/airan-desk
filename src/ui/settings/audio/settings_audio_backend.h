#ifndef AIRAN_SETTINGS_AUDIO_BACKEND_H
#define AIRAN_SETTINGS_AUDIO_BACKEND_H

#include <QList>
#include <QString>

#include <atomic>

class QObject;

namespace SettingsAudioBackend
{


struct AudioDeviceItem
{
    QString id;
    QString displayName;
    bool loopback{false};
};


QString noneDeviceValue();


QList<AudioDeviceItem> enumerateAudioDevices();


bool playToneOnOutput(const QString &configuredOutput);


void runMicLevelTest(const QString &configuredInput, std::atomic_bool *running, QObject *receiver);

} // namespace SettingsAudioBackend

#endif // AIRAN_SETTINGS_AUDIO_BACKEND_H
