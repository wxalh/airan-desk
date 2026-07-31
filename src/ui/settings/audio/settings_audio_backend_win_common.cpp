#include "ui/settings/audio/settings_audio_backend_win_common.h"

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)

#include <functiondiscoverykeys_devpkey.h>
#include <propidl.h>

namespace SettingsAudioBackend::Win
{
    
    bool initializeCom(bool *shouldUninit)
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (shouldUninit)
            *shouldUninit = SUCCEEDED(hr);
        return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }

    
    QString windowsDeviceId(IMMDevice *device)
    {
        if (!device)
            return QString();

        LPWSTR id = nullptr;
        if (FAILED(device->GetId(&id)) || !id)
            return QString();
        const QString result = QString::fromWCharArray(id);
        CoTaskMemFree(id);
        return result;
    }

    
    QString windowsFriendlyName(IMMDevice *device)
    {
        if (!device)
            return QString();

        IPropertyStore *store = nullptr;
        if (FAILED(device->OpenPropertyStore(STGM_READ, &store)) || !store)
            return QString();

        PROPVARIANT varName;
        PropVariantInit(&varName);
        QString name;
        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &varName)) && varName.vt == VT_LPWSTR && varName.pwszVal)
            name = QString::fromWCharArray(varName.pwszVal);
        PropVariantClear(&varName);
        store->Release();
        return name;
    }

    
    IMMDevice *resolveWindowsDevice(IMMDeviceEnumerator *enumerator, EDataFlow flow, const QString &configured)
    {
        if (!enumerator || configured.compare(noneDeviceValue(), Qt::CaseInsensitive) == 0)
            return nullptr;

        const QString trimmed = configured.trimmed();
        if (!trimmed.isEmpty())
        {
            IMMDevice *direct = nullptr;
            const std::wstring wid = trimmed.toStdWString();
            if (!wid.empty() && SUCCEEDED(enumerator->GetDevice(wid.c_str(), &direct)) && direct)
                return direct;
        }

        if (!trimmed.isEmpty())
        {
            IMMDeviceCollection *collection = nullptr;
            if (SUCCEEDED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection)) && collection)
            {
                const QString hintLower = trimmed.toLower();
                UINT count = 0;
                collection->GetCount(&count);

                IMMDevice *best = nullptr;
                int bestScore = -1;
                for (UINT i = 0; i < count; ++i)
                {
                    IMMDevice *candidate = nullptr;
                    if (FAILED(collection->Item(i, &candidate)) || !candidate)
                        continue;

                    const QString id = windowsDeviceId(candidate);
                    const QString name = windowsFriendlyName(candidate);
                    int score = -1;
                    if (!id.isEmpty() && id.compare(trimmed, Qt::CaseInsensitive) == 0)
                        score = 4;
                    else if (!name.isEmpty() && name.compare(trimmed, Qt::CaseInsensitive) == 0)
                        score = 3;
                    else if (id.toLower().contains(hintLower) || name.toLower().contains(hintLower))
                        score = 2;

                    if (score > bestScore)
                    {
                        releaseCom(best);
                        best = candidate;
                        bestScore = score;
                    }
                    else
                    {
                        candidate->Release();
                    }
                }
                collection->Release();
                if (best)
                    return best;
            }
        }

        IMMDevice *fallback = nullptr;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &fallback)) && fallback)
            return fallback;
        return nullptr;
    }
}

#endif
