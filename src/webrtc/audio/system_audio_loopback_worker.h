#ifndef SYSTEM_AUDIO_LOOPBACK_WORKER_H
#define SYSTEM_AUDIO_LOOPBACK_WORKER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class SystemAudioLoopbackWorker
{
public:
    using FrameCallback = std::function<void(const int16_t *samples, size_t frames, int sampleRate, size_t channels)>;

    enum class Source
    {
        System,
        Microphone
    };

    explicit SystemAudioLoopbackWorker(FrameCallback frameCallback,
                                       bool includeSystem = true,
                                       bool includeMicrophone = true);
    ~SystemAudioLoopbackWorker();

    void setIncludeSystem(bool includeSystem);
    void setIncludeMicrophone(bool includeMicrophone);
    void start();
    void stop();
    bool isRunning() const;
    void appendSamples(Source source, const int16_t *samples, size_t frames, int sampleRate, size_t channels);

private:
    void runMixer();
    void captureSystemAudio();
    void captureMicrophoneAudio();

    FrameCallback m_frameCallback;
    std::atomic_bool m_stopRequested{false};
    std::atomic_bool m_running{false};
    std::atomic_bool m_includeSystem{true};
    std::atomic_bool m_includeMicrophone{true};
    std::thread m_mixerThread;
    std::vector<std::thread> m_captureThreads;
    std::mutex m_audioMutex;
    std::deque<int16_t> m_systemSamples;
    std::deque<int16_t> m_microphoneSamples;
};

#endif /* SYSTEM_AUDIO_LOOPBACK_WORKER_H */
