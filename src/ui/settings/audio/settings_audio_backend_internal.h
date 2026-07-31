#ifndef AIRAN_SETTINGS_AUDIO_BACKEND_INTERNAL_H
#define AIRAN_SETTINGS_AUDIO_BACKEND_INTERNAL_H

class QObject;

namespace SettingsAudioBackend::Internal
{


void postMicLevel(QObject *receiver, float level);


void postMicStopped(QObject *receiver);

} // namespace SettingsAudioBackend::Internal

#endif // AIRAN_SETTINGS_AUDIO_BACKEND_INTERNAL_H
