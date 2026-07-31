#include "ui/settings/audio/settings_audio_backend.h"
#include "ui/settings/audio/settings_audio_backend_win_common.h"

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)

#include <algorithm>
#include <cmath>
#include <cstring>

#include <audioclient.h>

namespace SettingsAudioBackend
{

bool playToneOnOutput(const QString &configuredOutput)
{
    bool shouldUninit = false;
    if (!Win::initializeCom(&shouldUninit))
        return false;

    IMMDeviceEnumerator *enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&enumerator));
    if (FAILED(hr) || !enumerator)
    {
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    IMMDevice *device = Win::resolveWindowsDevice(enumerator, eRender, configuredOutput);
    IAudioClient *audioClient = nullptr;
    WAVEFORMATEX *mixFormat = nullptr;
    IAudioRenderClient *renderClient = nullptr;

    if (device)
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&audioClient));
    if (!device || FAILED(hr) || !audioClient ||
        FAILED(audioClient->GetMixFormat(&mixFormat)) || !mixFormat)
    {
        Win::releaseCom(renderClient);
        if (mixFormat)
            CoTaskMemFree(mixFormat);
        Win::releaseCom(audioClient);
        Win::releaseCom(device);
        Win::releaseCom(enumerator);
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    constexpr REFERENCE_TIME kBufferDuration = 2000000;
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kBufferDuration, 0, mixFormat, nullptr);
    if (SUCCEEDED(hr))
        hr = audioClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void **>(&renderClient));

    UINT32 bufferFrames = 0;
    if (SUCCEEDED(hr))
        audioClient->GetBufferSize(&bufferFrames);
    if (SUCCEEDED(hr))
        hr = audioClient->Start();
    if (FAILED(hr) || !renderClient)
    {
        Win::releaseCom(renderClient);
        CoTaskMemFree(mixFormat);
        Win::releaseCom(audioClient);
        Win::releaseCom(device);
        Win::releaseCom(enumerator);
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    const int sampleRate = qMax(8000, static_cast<int>(mixFormat->nSamplesPerSec));
    const UINT32 totalFrames = static_cast<UINT32>(sampleRate / 3);
    const int channels = qMax(1, static_cast<int>(mixFormat->nChannels));
    const bool isFloat = mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                         (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                          reinterpret_cast<WAVEFORMATEXTENSIBLE *>(mixFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

    UINT32 written = 0;
    double phase = 0.0;
    const double omega = 2.0 * 3.14159265358979323846 * 880.0 / static_cast<double>(sampleRate);
    while (written < totalFrames)
    {
        UINT32 padding = 0;
        if (FAILED(audioClient->GetCurrentPadding(&padding)))
            break;
        const UINT32 available = bufferFrames > padding ? (bufferFrames - padding) : 0;
        if (available == 0)
        {
            Sleep(5);
            continue;
        }

        const UINT32 frames = qMin(available, totalFrames - written);
        BYTE *data = nullptr;
        if (FAILED(renderClient->GetBuffer(frames, &data)) || !data)
            break;

        if (isFloat && mixFormat->wBitsPerSample == 32)
        {
            float *out = reinterpret_cast<float *>(data);
            for (UINT32 i = 0; i < frames; ++i)
            {
                const float s = static_cast<float>(std::sin(phase) * 0.18);
                phase += omega;
                for (int c = 0; c < channels; ++c)
                    out[static_cast<size_t>(i) * static_cast<size_t>(channels) + static_cast<size_t>(c)] = s;
            }
        }
        else if (mixFormat->wBitsPerSample == 16)
        {
            qint16 *out = reinterpret_cast<qint16 *>(data);
            for (UINT32 i = 0; i < frames; ++i)
            {
                const qint16 s = static_cast<qint16>(std::sin(phase) * 6000.0);
                phase += omega;
                for (int c = 0; c < channels; ++c)
                    out[static_cast<size_t>(i) * static_cast<size_t>(channels) + static_cast<size_t>(c)] = s;
            }
        }
        else
        {
            memset(data, 0, static_cast<size_t>(frames) * static_cast<size_t>(mixFormat->nBlockAlign));
        }

        renderClient->ReleaseBuffer(frames, 0);
        written += frames;
    }

    Sleep(80);
    audioClient->Stop();
    Win::releaseCom(renderClient);
    CoTaskMemFree(mixFormat);
    Win::releaseCom(audioClient);
    Win::releaseCom(device);
    Win::releaseCom(enumerator);
    if (shouldUninit)
        CoUninitialize();
    return true;
}

} // namespace SettingsAudioBackend
#endif
