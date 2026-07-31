#include "system_audio_loopback_worker.h"

#include "common/logger_manager.h"

#include <QtGlobal>

#if defined(Q_OS_WIN)
#include <Windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace
{
constexpr int kOutputSampleRate = 48000;
constexpr size_t kOutputChannels = 2;
constexpr size_t kTransportChannels = 1;
constexpr size_t kFrameSamples = kOutputSampleRate / 100;
constexpr size_t kFrameInterleavedSamples = kFrameSamples * kOutputChannels;
constexpr size_t kMaxQueuedSamples = kOutputSampleRate * kOutputChannels;


int16_t mixS16(int left, int right)
{
    return static_cast<int16_t>(std::max(-32768, std::min(32767, left + right)));
}


std::vector<int16_t> toStereo48k(const int16_t *samples, size_t frames, int sampleRate, size_t channels)
{
    if (!samples || frames == 0 || sampleRate <= 0 || channels == 0)
        return {};

    const size_t outFrames = static_cast<size_t>((static_cast<uint64_t>(frames) * kOutputSampleRate + sampleRate - 1) / sampleRate);
    std::vector<int16_t> out(outFrames * kOutputChannels);
    for (size_t frame = 0; frame < outFrames; ++frame)
    {
        size_t srcFrame = static_cast<size_t>((static_cast<uint64_t>(frame) * sampleRate) / kOutputSampleRate);
        if (srcFrame >= frames)
            srcFrame = frames - 1;
        const int16_t left = samples[srcFrame * channels];
        const int16_t right = channels > 1 ? samples[srcFrame * channels + 1] : left;
        out[frame * 2] = left;
        out[frame * 2 + 1] = right;
    }
    return out;
}
} /* namespace */


void SystemAudioLoopbackWorker::appendSamples(Source source, const int16_t *samples, size_t frames, int sampleRate, size_t channels)
{
    std::vector<int16_t> converted = toStereo48k(samples, frames, sampleRate, channels);
    if (converted.empty())
        return;

    std::lock_guard<std::mutex> lock(m_audioMutex);
    auto &queue = source == Source::System ? m_systemSamples : m_microphoneSamples;
    queue.insert(queue.end(), converted.begin(), converted.end());
    while (queue.size() > kMaxQueuedSamples)
        queue.pop_front();
}


void SystemAudioLoopbackWorker::runMixer()
{
    LOG_INFO("Remote mixed audio source started: system={}, microphone={}",
             m_includeSystem.load(), m_includeMicrophone.load());
    std::vector<int16_t> frame(kFrameInterleavedSamples);
    std::vector<int16_t> monoFrame(kFrameSamples);
    auto nextFrameTime = std::chrono::steady_clock::now();
    auto nextLevelLogTime = nextFrameTime + std::chrono::seconds(1);
    int peakLevel = 0;
    while (!m_stopRequested.load())
    {
        {
            std::lock_guard<std::mutex> lock(m_audioMutex);
            for (size_t i = 0; i < frame.size(); ++i)
            {
                int mixed = 0;
                if (m_includeSystem.load() && !m_systemSamples.empty())
                {
                    mixed += m_systemSamples.front();
                    m_systemSamples.pop_front();
                }
                if (m_includeMicrophone.load() && !m_microphoneSamples.empty())
                {
                    mixed += m_microphoneSamples.front();
                    m_microphoneSamples.pop_front();
                }
                frame[i] = mixS16(mixed, 0);
            }
        }

        if (m_frameCallback)
        {
            for (size_t i = 0; i < kFrameSamples; ++i)
            {
                const int left = frame[i * 2];
                const int right = frame[i * 2 + 1];
                monoFrame[i] = static_cast<int16_t>((left + right) / 2);
                peakLevel = std::max(peakLevel, std::abs(static_cast<int>(monoFrame[i])));
            }
            m_frameCallback(monoFrame.data(), kFrameSamples, kOutputSampleRate, kTransportChannels);
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= nextLevelLogTime)
        {
            size_t queuedSystemSamples = 0;
            size_t queuedMicrophoneSamples = 0;
            {
                std::lock_guard<std::mutex> lock(m_audioMutex);
                queuedSystemSamples = m_systemSamples.size();
                queuedMicrophoneSamples = m_microphoneSamples.size();
            }
            LOG_DEBUG("Remote mixed audio level: peak={}, queuedSystemSamples={}, queuedMicrophoneSamples={}",
                      peakLevel,
                      queuedSystemSamples,
                      queuedMicrophoneSamples);
            peakLevel = 0;
            nextLevelLogTime = now + std::chrono::seconds(1);
        }

#if defined(Q_OS_WIN)
        Sleep(10);
#else
        nextFrameTime += std::chrono::milliseconds(10);
        std::this_thread::sleep_until(nextFrameTime);
#endif
    }
    LOG_INFO("Remote mixed audio source stopped");
}
