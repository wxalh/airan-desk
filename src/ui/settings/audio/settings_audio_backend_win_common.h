#ifndef SETTINGS_AUDIO_BACKEND_WIN_COMMON_H
#define SETTINGS_AUDIO_BACKEND_WIN_COMMON_H

#include "ui/settings/audio/settings_audio_backend.h"

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)

#include <windows.h>
#include <mmdeviceapi.h>

namespace SettingsAudioBackend::Win
{
    
    template <typename T>
    void releaseCom(T *&ptr)
    {
        if (ptr)
        {
            ptr->Release();
            ptr = nullptr;
        }
    }

    
    bool initializeCom(bool *shouldUninit);

    
    QString windowsDeviceId(IMMDevice *device);

    
    QString windowsFriendlyName(IMMDevice *device);

    
    IMMDevice *resolveWindowsDevice(IMMDeviceEnumerator *enumerator, EDataFlow flow, const QString &configured);
}

#endif

#endif /* SETTINGS_AUDIO_BACKEND_WIN_COMMON_H */
