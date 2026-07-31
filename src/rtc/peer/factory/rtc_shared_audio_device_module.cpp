#include "rtc/peer/factory/rtc_shared_audio_device_module.h"

#include "common/logger_manager.h"
#include "webrtc/audio/system_audio_loopback_worker.h"

#include <api/make_ref_counted.h>
#if AIRAN_WEBRTC_MILESTONE >= 144
#include <api/audio/audio_device.h>
#else
#include <modules/audio_device/include/audio_device.h>
#endif

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <utility>

namespace rtc
{
namespace
{
constexpr uint32_t kVirtualMicLevel = 100;
constexpr int kTransportSampleRate = 48000;
constexpr size_t kTransportChannels = 1;
constexpr size_t kTransportFramesPer10Ms = kTransportSampleRate / 100;

class SharedAudioDeviceModule : public webrtc::AudioDeviceModule
{
public:
    explicit SharedAudioDeviceModule(scoped_refptr<webrtc::AudioDeviceModule> platform)
        : m_platform(std::move(platform))
    {
    }

    ~SharedAudioDeviceModule() override
    {
        stopCustomRecording();
    }

    void setMixedSystemAndMicrophone(bool enabled)
    {
        const bool previous = m_mixedSystemAndMicrophone.exchange(enabled);
        if (previous == enabled)
        {
            if (enabled && m_recording.load() && !m_customRecording.load())
                startCustomRecording();
            return;
        }
        LOG_INFO("Shared audio recording mode changed: mixedSystemAndMicrophone={}", enabled);
        if (!m_recording.load())
            return;
        if (enabled)
        {
            if (m_platform)
                m_platform->StopRecording();
            startCustomRecording();
        }
        else
        {
            stopCustomRecording();
            if (m_platform)
                m_platform->StartRecording();
        }
    }

    int32_t ActiveAudioLayer(AudioLayer *audioLayer) const override
    {
        return m_platform ? m_platform->ActiveAudioLayer(audioLayer) : -1;
    }

    int32_t RegisterAudioCallback(webrtc::AudioTransport *audioCallback) override
    {
        {
            std::lock_guard<std::mutex> lock(m_transportMutex);
            m_audioTransport = audioCallback;
        }
        return m_platform ? m_platform->RegisterAudioCallback(audioCallback) : 0;
    }

    int32_t Init() override
    {
        m_initialized.store(true);
        return m_platform ? m_platform->Init() : 0;
    }

    int32_t Terminate() override
    {
        stopCustomRecording();
        m_recordingInitialized.store(false);
        m_playoutInitialized.store(false);
        m_initialized.store(false);
        return m_platform ? m_platform->Terminate() : 0;
    }

    bool Initialized() const override
    {
        return m_platform ? m_platform->Initialized() : m_initialized.load();
    }

    int16_t PlayoutDevices() override
    {
        return m_platform ? m_platform->PlayoutDevices() : 0;
    }

    int16_t RecordingDevices() override
    {
        return m_platform ? m_platform->RecordingDevices() : 0;
    }

    int32_t PlayoutDeviceName(uint16_t index,
                              char name[webrtc::kAdmMaxDeviceNameSize],
                              char guid[webrtc::kAdmMaxGuidSize]) override
    {
        return m_platform ? m_platform->PlayoutDeviceName(index, name, guid) : -1;
    }

    int32_t RecordingDeviceName(uint16_t index,
                                char name[webrtc::kAdmMaxDeviceNameSize],
                                char guid[webrtc::kAdmMaxGuidSize]) override
    {
        return m_platform ? m_platform->RecordingDeviceName(index, name, guid) : -1;
    }

    int32_t SetPlayoutDevice(uint16_t index) override
    {
        return m_platform ? m_platform->SetPlayoutDevice(index) : -1;
    }

    int32_t SetPlayoutDevice(WindowsDeviceType device) override
    {
        return m_platform ? m_platform->SetPlayoutDevice(device) : -1;
    }

    int32_t SetRecordingDevice(uint16_t index) override
    {
        return m_platform ? m_platform->SetRecordingDevice(index) : -1;
    }

    int32_t SetRecordingDevice(WindowsDeviceType device) override
    {
        return m_platform ? m_platform->SetRecordingDevice(device) : -1;
    }

    int32_t PlayoutIsAvailable(bool *available) override
    {
        return m_platform ? m_platform->PlayoutIsAvailable(available) : setBool(available, false);
    }

    int32_t InitPlayout() override
    {
        const int32_t result = m_platform ? m_platform->InitPlayout() : 0;
        if (result == 0)
            m_playoutInitialized.store(true);
        return result;
    }

    bool PlayoutIsInitialized() const override
    {
        return m_platform ? m_platform->PlayoutIsInitialized() : m_playoutInitialized.load();
    }

    int32_t RecordingIsAvailable(bool *available) override
    {
        if (m_mixedSystemAndMicrophone.load())
            return setBool(available, true);
        return m_platform ? m_platform->RecordingIsAvailable(available) : setBool(available, false);
    }

    int32_t InitRecording() override
    {
        if (m_mixedSystemAndMicrophone.load())
        {
            m_recordingInitialized.store(true);
            return 0;
        }
        const int32_t result = m_platform ? m_platform->InitRecording() : 0;
        if (result == 0)
            m_recordingInitialized.store(true);
        return result;
    }

    bool RecordingIsInitialized() const override
    {
        if (m_mixedSystemAndMicrophone.load())
            return m_recordingInitialized.load();
        return m_platform ? m_platform->RecordingIsInitialized() : m_recordingInitialized.load();
    }

    int32_t StartPlayout() override
    {
        return m_platform ? m_platform->StartPlayout() : 0;
    }

    int32_t StopPlayout() override
    {
        return m_platform ? m_platform->StopPlayout() : 0;
    }

    bool Playing() const override
    {
        return m_platform ? m_platform->Playing() : false;
    }

    int32_t StartRecording() override
    {
        const bool wasRecording = m_recording.exchange(true);
        if (wasRecording && !m_mixedSystemAndMicrophone.load())
            return 0;
        if (m_mixedSystemAndMicrophone.load())
            return startCustomRecording();
        LOG_INFO("Shared audio recording uses platform ADM microphone path");
        return m_platform ? m_platform->StartRecording() : 0;
    }

    int32_t StopRecording() override
    {
        if (!m_recording.exchange(false))
            return 0;
        if (m_mixedSystemAndMicrophone.load())
            return stopCustomRecording();
        LOG_INFO("Shared audio recording stops platform ADM microphone path");
        return m_platform ? m_platform->StopRecording() : 0;
    }

    bool Recording() const override
    {
        if (m_mixedSystemAndMicrophone.load())
            return m_customRecording.load();
        return m_platform ? m_platform->Recording() : m_recording.load();
    }

    int32_t InitSpeaker() override
    {
        return m_platform ? m_platform->InitSpeaker() : 0;
    }

    bool SpeakerIsInitialized() const override
    {
        return m_platform ? m_platform->SpeakerIsInitialized() : true;
    }

    int32_t InitMicrophone() override
    {
        if (m_mixedSystemAndMicrophone.load())
            return 0;
        return m_platform ? m_platform->InitMicrophone() : 0;
    }

    bool MicrophoneIsInitialized() const override
    {
        if (m_mixedSystemAndMicrophone.load())
            return true;
        return m_platform ? m_platform->MicrophoneIsInitialized() : true;
    }

    int32_t SpeakerVolumeIsAvailable(bool *available) override { return setBool(available, false); }
    int32_t SetSpeakerVolume(uint32_t) override { return -1; }
    int32_t SpeakerVolume(uint32_t *volume) const override { return setUint(volume, 0); }
    int32_t MaxSpeakerVolume(uint32_t *maxVolume) const override { return setUint(maxVolume, 0); }
    int32_t MinSpeakerVolume(uint32_t *minVolume) const override { return setUint(minVolume, 0); }
    int32_t MicrophoneVolumeIsAvailable(bool *available) override { return setBool(available, false); }
    int32_t SetMicrophoneVolume(uint32_t) override { return -1; }
    int32_t MicrophoneVolume(uint32_t *volume) const override { return setUint(volume, kVirtualMicLevel); }
    int32_t MaxMicrophoneVolume(uint32_t *maxVolume) const override { return setUint(maxVolume, kVirtualMicLevel); }
    int32_t MinMicrophoneVolume(uint32_t *minVolume) const override { return setUint(minVolume, 0); }
    int32_t SpeakerMuteIsAvailable(bool *available) override { return setBool(available, false); }
    int32_t SetSpeakerMute(bool) override { return -1; }
    int32_t SpeakerMute(bool *enabled) const override { return setBool(enabled, false); }
    int32_t MicrophoneMuteIsAvailable(bool *available) override { return setBool(available, false); }
    int32_t SetMicrophoneMute(bool) override { return -1; }
    int32_t MicrophoneMute(bool *enabled) const override { return setBool(enabled, false); }

    int32_t StereoPlayoutIsAvailable(bool *available) const override
    {
        return m_platform ? m_platform->StereoPlayoutIsAvailable(available) : setBool(available, true);
    }

    int32_t SetStereoPlayout(bool enable) override
    {
        return m_platform ? m_platform->SetStereoPlayout(enable) : 0;
    }

    int32_t StereoPlayout(bool *enabled) const override
    {
        return m_platform ? m_platform->StereoPlayout(enabled) : setBool(enabled, true);
    }

    int32_t StereoRecordingIsAvailable(bool *available) const override
    {
        if (m_mixedSystemAndMicrophone.load())
            return setBool(available, true);
        return m_platform ? m_platform->StereoRecordingIsAvailable(available) : setBool(available, true);
    }

    int32_t SetStereoRecording(bool enable) override
    {
        if (m_mixedSystemAndMicrophone.load())
            return 0;
        return m_platform ? m_platform->SetStereoRecording(enable) : 0;
    }

    int32_t StereoRecording(bool *enabled) const override
    {
        if (m_mixedSystemAndMicrophone.load())
            return setBool(enabled, true);
        return m_platform ? m_platform->StereoRecording(enabled) : setBool(enabled, true);
    }

    int32_t PlayoutDelay(uint16_t *delayMS) const override
    {
        return m_platform ? m_platform->PlayoutDelay(delayMS) : setUint16(delayMS, 0);
    }

    bool BuiltInAECIsAvailable() const override { return m_platform ? m_platform->BuiltInAECIsAvailable() : false; }
    bool BuiltInAGCIsAvailable() const override { return m_platform ? m_platform->BuiltInAGCIsAvailable() : false; }
    bool BuiltInNSIsAvailable() const override { return m_platform ? m_platform->BuiltInNSIsAvailable() : false; }
    int32_t EnableBuiltInAEC(bool enable) override { return m_platform ? m_platform->EnableBuiltInAEC(enable) : -1; }
    int32_t EnableBuiltInAGC(bool enable) override { return m_platform ? m_platform->EnableBuiltInAGC(enable) : -1; }
    int32_t EnableBuiltInNS(bool enable) override { return m_platform ? m_platform->EnableBuiltInNS(enable) : -1; }
    int32_t GetPlayoutUnderrunCount() const override { return m_platform ? m_platform->GetPlayoutUnderrunCount() : -1; }
#if AIRAN_WEBRTC_MILESTONE >= 144
    std::optional<Stats> GetStats() const override { return m_platform ? m_platform->GetStats() : std::nullopt; }
#endif

private:
    static int32_t setBool(bool *out, bool value)
    {
        if (out)
            *out = value;
        return 0;
    }

    static int32_t setUint(uint32_t *out, uint32_t value)
    {
        if (out)
            *out = value;
        return 0;
    }

    static int32_t setUint16(uint16_t *out, uint16_t value)
    {
        if (out)
            *out = value;
        return 0;
    }

    int32_t startCustomRecording()
    {
        if (m_customRecording.exchange(true))
            return 0;
        m_recordingInitialized.store(true);
        auto callback = [this](const int16_t *samples, size_t frames, int sampleRate, size_t channels) {
            deliverRecordedFrame(samples, frames, sampleRate, channels);
        };
        m_worker = std::make_unique<SystemAudioLoopbackWorker>(std::move(callback), true, true);
        m_worker->start();
        LOG_INFO("Shared mixed audio recording started through custom shared loopback worker");
        return 0;
    }

    int32_t stopCustomRecording()
    {
        if (!m_customRecording.exchange(false))
            return 0;
        if (m_worker)
        {
            m_worker->stop();
            m_worker.reset();
        }
        LOG_INFO("Shared mixed audio recording stopped");
        return 0;
    }

    void deliverRecordedFrame(const int16_t *samples, size_t frames, int sampleRate, size_t channels)
    {
        if (!samples || frames == 0 || channels == 0)
            return;
        if (sampleRate != kTransportSampleRate || frames != kTransportFramesPer10Ms || channels != kTransportChannels)
        {
            bool expected = false;
            if (m_badFrameWarningLogged.compare_exchange_strong(expected, true))
            {
                LOG_WARN("Dropping custom audio frame with unsupported transport shape: sampleRate={}, frames={}, channels={}",
                         sampleRate,
                         frames,
                         channels);
            }
            return;
        }

        uint32_t newMicLevel = kVirtualMicLevel;
        int peakLevel = 0;
        for (size_t i = 0; i < frames * channels; ++i)
            peakLevel = std::max(peakLevel, std::abs(static_cast<int>(samples[i])));
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_nextDeliveredLevelLogTime)
        {
            LOG_DEBUG("Delivering custom audio frame to WebRTC: sampleRate={}, frames={}, channels={}, peak={}",
                      sampleRate,
                      frames,
                      channels,
                      peakLevel);
            m_nextDeliveredLevelLogTime = now + std::chrono::seconds(1);
        }
        // Keep the transport lock held through the callback. Registering a
        // null/new transport must wait until this invocation has finished,
        // otherwise the raw AudioTransport pointer can be used after the
        // WebRTC audio pipeline has released it.
        std::lock_guard<std::mutex> lock(m_transportMutex);
        if (!m_audioTransport)
            return;
        m_audioTransport->RecordedDataIsAvailable(samples,
                                                  frames,
                                                  sizeof(int16_t),
                                                  channels,
                                                  static_cast<uint32_t>(sampleRate),
                                                  0,
                                                  0,
                                                  kVirtualMicLevel,
                                                  false,
                                                  newMicLevel);
    }

    scoped_refptr<webrtc::AudioDeviceModule> m_platform;
    std::unique_ptr<SystemAudioLoopbackWorker> m_worker;
    mutable std::mutex m_transportMutex;
    webrtc::AudioTransport *m_audioTransport{nullptr};
    std::atomic_bool m_mixedSystemAndMicrophone{false};
    std::atomic_bool m_initialized{false};
    std::atomic_bool m_playoutInitialized{false};
    std::atomic_bool m_recordingInitialized{false};
    std::atomic_bool m_recording{false};
    std::atomic_bool m_customRecording{false};
    std::atomic_bool m_badFrameWarningLogged{false};
    std::chrono::steady_clock::time_point m_nextDeliveredLevelLogTime{};
};

SharedAudioDeviceModule *asSharedAudioDeviceModule(const scoped_refptr<webrtc::AudioDeviceModule> &module)
{
    return dynamic_cast<SharedAudioDeviceModule *>(module.get());
}
} // namespace

scoped_refptr<webrtc::AudioDeviceModule> createSharedAudioDeviceModule(scoped_refptr<webrtc::AudioDeviceModule> platform)
{
    if (!platform)
        return nullptr;
#if AIRAN_WEBRTC_MILESTONE >= 144
    return webrtc::make_ref_counted<SharedAudioDeviceModule>(std::move(platform));
#else
    return ::rtc::make_ref_counted<SharedAudioDeviceModule>(std::move(platform));
#endif
}

void setSharedAudioRecordingMode(const scoped_refptr<webrtc::AudioDeviceModule> &module, bool mixedSystemAndMicrophone)
{
    if (auto *shared = asSharedAudioDeviceModule(module))
        shared->setMixedSystemAndMicrophone(mixedSystemAndMicrophone);
}

} // namespace rtc
