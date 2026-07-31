#include "system_audio_loopback_worker.h"

#include "common/logger_manager.h"
#include "util/config/config_util.h"

#include <QtGlobal>

#if defined(Q_OS_WIN)

#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr const char *kAudioDeviceNoneValue = "__none__";


bool isNoneDeviceValue(const QString &device)
{
    return device.trimmed().compare(QString::fromLatin1(kAudioDeviceNoneValue), Qt::CaseInsensitive) == 0;
}


QString configuredDeviceForSource(SystemAudioLoopbackWorker::Source source)
{
    return source == SystemAudioLoopbackWorker::Source::System
               ? ConfigUtil->audio_loopback_device.trimmed()
               : ConfigUtil->audio_mic_device.trimmed();
}


const char *sourceName(SystemAudioLoopbackWorker::Source source)
{
    return source == SystemAudioLoopbackWorker::Source::System ? "system" : "microphone";
}


bool endpointFlowMatches(IMMDevice *device, EDataFlow expectedFlow)
{
    if (!device)
        return false;

    ComPtr<IMMEndpoint> endpoint;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&endpoint))) || !endpoint)
        return false;

    EDataFlow actualFlow = eAll;
    return SUCCEEDED(endpoint->GetDataFlow(&actualFlow)) && actualFlow == expectedFlow;
}


ComPtr<IMMDevice> resolveWasapiEndpoint(IMMDeviceEnumerator *enumerator,
                                        SystemAudioLoopbackWorker::Source source,
                                        EDataFlow dataFlow)
{
    ComPtr<IMMDevice> device;
    if (!enumerator)
        return device;

    const QString configuredDevice = configuredDeviceForSource(source);
    if (!configuredDevice.isEmpty())
    {
        IMMDevice *configured = nullptr;
        const std::wstring id = configuredDevice.toStdWString();
        if (!id.empty() && SUCCEEDED(enumerator->GetDevice(id.c_str(), &configured)) && configured)
        {
            device.Attach(configured);
            if (endpointFlowMatches(device.Get(), dataFlow))
                return device;

            LOG_WARN("Configured WASAPI endpoint flow does not match requested source; falling back to default: source={}, device={}",
                     sourceName(source),
                     configuredDevice);
            device.Reset();
        }
        else
        {
            LOG_WARN("Configured WASAPI device was not found, falling back to default: source={}, device={}",
                     sourceName(source),
                     configuredDevice);
        }
    }

    IMMDevice *fallback = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(dataFlow, eConsole, &fallback)) && fallback)
        device.Attach(fallback);
    return device;
}


int16_t clampToS16(float sample)
{
    sample = std::max(-1.0f, std::min(1.0f, sample));
    return static_cast<int16_t>(std::lrint(sample * 32767.0f));
}


bool isFloatFormat(const WAVEFORMATEX *format)
{
    if (!format)
        return false;
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return true;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
        return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    return false;
}


bool isPcmFormat(const WAVEFORMATEX *format)
{
    if (!format)
        return false;
    if (format->wFormatTag == WAVE_FORMAT_PCM)
        return true;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
        return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM);
    }
    return false;
}


std::vector<int16_t> convertWasapiPacketToS16(const BYTE *data, UINT32 frameCount, const WAVEFORMATEX *format)
{
    const size_t sourceChannels = std::max<size_t>(1, format ? format->nChannels : 2);
    std::vector<int16_t> out(static_cast<size_t>(frameCount) * sourceChannels);
    if (!data || !format)
    {
        std::fill(out.begin(), out.end(), 0);
        return out;
    }

    if (isFloatFormat(format) && format->wBitsPerSample == 32)
    {
        const auto *samples = reinterpret_cast<const float *>(data);
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = clampToS16(samples[i]);
        return out;
    }

    if (isPcmFormat(format) && format->wBitsPerSample == 16)
    {
        std::memcpy(out.data(), data, out.size() * sizeof(int16_t));
        return out;
    }

    LOG_WARN("Unsupported WASAPI capture format: tag={}, bits={}, channels={}",
             format->wFormatTag, format->wBitsPerSample, format->nChannels);
    std::fill(out.begin(), out.end(), 0);
    return out;
}


void captureWasapiEndpoint(const std::atomic_bool &stopRequested,
                           SystemAudioLoopbackWorker *owner,
                           SystemAudioLoopbackWorker::Source source,
                           EDataFlow dataFlow,
                           DWORD streamFlags)
{
    const QString configuredDevice = configuredDeviceForSource(source);
    if (isNoneDeviceValue(configuredDevice))
    {
        LOG_INFO("WASAPI capture is disabled by settings: source={}",
                 source == SystemAudioLoopbackWorker::Source::System ? "system" : "microphone");
        return;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE)
        hr = S_OK;
    if (FAILED(hr))
    {
        LOG_ERROR("WASAPI capture COM initialization failed: hr=0x{:08x}", static_cast<unsigned>(hr));
        return;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr))
    {
        LOG_ERROR("WASAPI device enumerator creation failed: hr=0x{:08x}", static_cast<unsigned>(hr));
        if (comInitialized)
            CoUninitialize();
        return;
    }

    ComPtr<IMMDevice> device = resolveWasapiEndpoint(enumerator.Get(), source, dataFlow);
    if (!device)
    {
        LOG_ERROR("WASAPI endpoint is unavailable: source={}, flow={}", sourceName(source), static_cast<int>(dataFlow));
        if (comInitialized)
            CoUninitialize();
        return;
    }

    ComPtr<IAudioClient> audioClient;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(audioClient.GetAddressOf()));
    if (FAILED(hr))
    {
        LOG_ERROR("WASAPI audio client activation failed: hr=0x{:08x}", static_cast<unsigned>(hr));
        if (comInitialized)
            CoUninitialize();
        return;
    }

    WAVEFORMATEX *mixFormat = nullptr;
    hr = audioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr) || !mixFormat)
    {
        LOG_ERROR("WASAPI mix format query failed: hr=0x{:08x}", static_cast<unsigned>(hr));
        if (comInitialized)
            CoUninitialize();
        return;
    }

    constexpr REFERENCE_TIME bufferDuration = 10000000;
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, bufferDuration, 0, mixFormat, nullptr);
    if (FAILED(hr))
    {
        LOG_ERROR("WASAPI capture initialization failed: flow={}, hr=0x{:08x}", static_cast<int>(dataFlow), static_cast<unsigned>(hr));
        CoTaskMemFree(mixFormat);
        if (comInitialized)
            CoUninitialize();
        return;
    }

    ComPtr<IAudioCaptureClient> captureClient;
    hr = audioClient->GetService(IID_PPV_ARGS(&captureClient));
    if (FAILED(hr))
    {
        LOG_ERROR("WASAPI capture service query failed: hr=0x{:08x}", static_cast<unsigned>(hr));
        CoTaskMemFree(mixFormat);
        if (comInitialized)
            CoUninitialize();
        return;
    }

    hr = audioClient->Start();
    if (FAILED(hr))
    {
        LOG_ERROR("WASAPI capture start failed: hr=0x{:08x}", static_cast<unsigned>(hr));
        CoTaskMemFree(mixFormat);
        if (comInitialized)
            CoUninitialize();
        return;
    }

    LOG_INFO("WASAPI capture started: source={}, sampleRate={}, channels={}, bits={}",
             sourceName(source),
             mixFormat->nSamplesPerSec, mixFormat->nChannels, mixFormat->wBitsPerSample);

    while (!stopRequested.load())
    {
        UINT32 packetFrames = 0;
        hr = captureClient->GetNextPacketSize(&packetFrames);
        if (FAILED(hr))
        {
            LOG_WARN("WASAPI packet query failed: hr=0x{:08x}", static_cast<unsigned>(hr));
            break;
        }

        if (packetFrames == 0)
        {
            Sleep(5);
            continue;
        }

        BYTE *data = nullptr;
        DWORD flags = 0;
        UINT64 devicePosition = 0;
        UINT64 qpcPosition = 0;
        hr = captureClient->GetBuffer(&data, &packetFrames, &flags, &devicePosition, &qpcPosition);
        if (FAILED(hr))
        {
            LOG_WARN("WASAPI buffer query failed: hr=0x{:08x}", static_cast<unsigned>(hr));
            break;
        }

        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0)
            data = nullptr;

        const std::vector<int16_t> converted = convertWasapiPacketToS16(data, packetFrames, mixFormat);
        owner->appendSamples(source, converted.data(), packetFrames, static_cast<int>(mixFormat->nSamplesPerSec), std::max<size_t>(1, mixFormat->nChannels));
        captureClient->ReleaseBuffer(packetFrames);
    }

    audioClient->Stop();
    CoTaskMemFree(mixFormat);
    if (comInitialized)
        CoUninitialize();
    LOG_INFO("WASAPI capture stopped: source={}", sourceName(source));
}
} // namespace


void SystemAudioLoopbackWorker::captureSystemAudio()
{
    captureWasapiEndpoint(m_stopRequested, this, Source::System, eRender, AUDCLNT_STREAMFLAGS_LOOPBACK);
}


void SystemAudioLoopbackWorker::captureMicrophoneAudio()
{
    captureWasapiEndpoint(m_stopRequested, this, Source::Microphone, eCapture, 0);
}

#endif // defined(Q_OS_WIN)
