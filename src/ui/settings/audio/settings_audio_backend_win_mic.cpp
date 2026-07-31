#include "ui/settings/audio/settings_audio_backend.h"
#include "ui/settings/audio/settings_audio_backend_internal.h"
#include "ui/settings/audio/settings_audio_backend_win_common.h"

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)

#include <algorithm>
#include <cmath>

#include <audioclient.h>

namespace SettingsAudioBackend
{
namespace
{
    
    float sampleLevel(const BYTE *data, UINT32 frames, const WAVEFORMATEX *format, DWORD flags)
    {
        if (!data || !format || frames == 0 || (flags & AUDCLNT_BUFFERFLAGS_SILENT))
            return 0.0f;

        const int channels = std::max(1, static_cast<int>(format->nChannels));
        const int samples = static_cast<int>(frames) * channels;
        double sumSquares = 0.0;

        const bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                             (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                              reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        if (isFloat && format->wBitsPerSample == 32)
        {
            const float *in = reinterpret_cast<const float *>(data);
            for (int i = 0; i < samples; ++i)
            {
                const double s = std::clamp(static_cast<double>(in[i]), -1.0, 1.0);
                sumSquares += s * s;
            }
        }
        else if (format->wBitsPerSample == 16)
        {
            const qint16 *in = reinterpret_cast<const qint16 *>(data);
            for (int i = 0; i < samples; ++i)
            {
                const double s = static_cast<double>(in[i]) / 32768.0;
                sumSquares += s * s;
            }
        }
        else if (format->wBitsPerSample == 32)
        {
            const qint32 *in = reinterpret_cast<const qint32 *>(data);
            for (int i = 0; i < samples; ++i)
            {
                const double s = static_cast<double>(in[i]) / 2147483648.0;
                sumSquares += s * s;
            }
        }
        else
        {
            return 0.0f;
        }

        const double rms = std::sqrt(sumSquares / std::max(1, samples));
        return static_cast<float>(std::clamp(rms * 3.0, 0.0, 1.0));
    }
}


void runMicLevelTest(const QString &configuredInput, std::atomic_bool *running, QObject *receiver)
{
    bool shouldUninit = false;
    if (!Win::initializeCom(&shouldUninit))
    {
        Internal::postMicStopped(receiver);
        return;
    }

    IMMDeviceEnumerator *enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&enumerator));
    if (FAILED(hr) || !enumerator)
    {
        if (shouldUninit)
            CoUninitialize();
        Internal::postMicStopped(receiver);
        return;
    }

    IMMDevice *device = Win::resolveWindowsDevice(enumerator, eCapture, configuredInput);
    IAudioClient *audioClient = nullptr;
    WAVEFORMATEX *mixFormat = nullptr;
    IAudioCaptureClient *captureClient = nullptr;

    if (device)
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&audioClient));
    if (!device || FAILED(hr) || !audioClient ||
        FAILED(audioClient->GetMixFormat(&mixFormat)) || !mixFormat)
    {
        Win::releaseCom(captureClient);
        if (mixFormat)
            CoTaskMemFree(mixFormat);
        Win::releaseCom(audioClient);
        Win::releaseCom(device);
        Win::releaseCom(enumerator);
        if (shouldUninit)
            CoUninitialize();
        Internal::postMicStopped(receiver);
        return;
    }

    constexpr REFERENCE_TIME kBufferDuration = 2000000;
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kBufferDuration, 0, mixFormat, nullptr);
    if (SUCCEEDED(hr))
        hr = audioClient->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void **>(&captureClient));
    if (SUCCEEDED(hr))
        hr = audioClient->Start();

    if (FAILED(hr) || !captureClient)
    {
        Win::releaseCom(captureClient);
        CoTaskMemFree(mixFormat);
        Win::releaseCom(audioClient);
        Win::releaseCom(device);
        Win::releaseCom(enumerator);
        if (shouldUninit)
            CoUninitialize();
        Internal::postMicStopped(receiver);
        return;
    }

    DWORD lastPostTick = GetTickCount();
    while (running && running->load())
    {
        UINT32 packetFrames = 0;
        if (FAILED(captureClient->GetNextPacketSize(&packetFrames)))
            break;
        if (packetFrames == 0)
        {
            Sleep(20);
            const DWORD now = GetTickCount();
            if (now - lastPostTick > 250)
            {
                Internal::postMicLevel(receiver, 0.0f);
                lastPostTick = now;
            }
            continue;
        }

        BYTE *data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        if (FAILED(captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
            break;

        Internal::postMicLevel(receiver, sampleLevel(data, frames, mixFormat, flags));
        lastPostTick = GetTickCount();
        captureClient->ReleaseBuffer(frames);
    }

    audioClient->Stop();
    Win::releaseCom(captureClient);
    CoTaskMemFree(mixFormat);
    Win::releaseCom(audioClient);
    Win::releaseCom(device);
    Win::releaseCom(enumerator);
    if (shouldUninit)
        CoUninitialize();
    Internal::postMicStopped(receiver);
}

} // namespace SettingsAudioBackend
#endif
