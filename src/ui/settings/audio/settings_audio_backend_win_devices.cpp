#include "ui/settings/audio/settings_audio_backend.h"
#include "ui/settings/audio/settings_audio_backend_win_common.h"

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)

#include <mmdeviceapi.h>

namespace SettingsAudioBackend
{

QList<AudioDeviceItem> enumerateAudioDevices()
{
    QList<AudioDeviceItem> devices;

    bool shouldUninit = false;
    if (!Win::initializeCom(&shouldUninit))
        return devices;

    IMMDeviceEnumerator *enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&enumerator));
    if (FAILED(hr) || !enumerator)
    {
        if (shouldUninit)
            CoUninitialize();
        return devices;
    }

    auto appendFlow = [&](EDataFlow flow, bool loopbackFlag)
    {
        IMMDeviceCollection *collection = nullptr;
        if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection)) || !collection)
            return;

        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; ++i)
        {
            IMMDevice *device = nullptr;
            if (FAILED(collection->Item(i, &device)) || !device)
                continue;

            AudioDeviceItem item;
            item.id = Win::windowsDeviceId(device);
            item.displayName = Win::windowsFriendlyName(device);
            if (item.displayName.isEmpty())
                item.displayName = item.id;
            item.loopback = loopbackFlag;
            devices.push_back(item);
            device->Release();
        }
        collection->Release();
    };

    appendFlow(eCapture, false);
    appendFlow(eRender, true);

    Win::releaseCom(enumerator);
    if (shouldUninit)
        CoUninitialize();

    return devices;
}

} // namespace SettingsAudioBackend
#endif
