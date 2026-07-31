#include "ui/settings/audio/settings_audio_backend.h"
#include "ui/settings/audio/settings_audio_backend_internal.h"

#include <QMetaObject>
#include <QObject>

namespace SettingsAudioBackend
{
namespace
{
const QString kAudioDeviceNoneValue = QStringLiteral("__none__");
}

QString noneDeviceValue()
{
    return kAudioDeviceNoneValue;
}

namespace Internal
{

void postMicLevel(QObject *receiver, float level)
{
    if (!receiver)
        return;
    QMetaObject::invokeMethod(receiver, "enqueueMicTestLevel", Qt::DirectConnection, Q_ARG(float, level));
}

void postMicStopped(QObject *receiver)
{
    QMetaObject::invokeMethod(receiver, "onMicTestStopped", Qt::QueuedConnection);
}

} // namespace Internal
} // namespace SettingsAudioBackend
