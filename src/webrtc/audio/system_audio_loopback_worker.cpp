#include "system_audio_loopback_worker.h"

#include "common/logger_manager.h"

#include <exception>
#include <utility>


SystemAudioLoopbackWorker::SystemAudioLoopbackWorker(FrameCallback frameCallback,
                                                     bool includeSystem,
                                                     bool includeMicrophone)
    : m_frameCallback(std::move(frameCallback)),
      m_includeSystem(includeSystem),
      m_includeMicrophone(includeMicrophone)
{
}


SystemAudioLoopbackWorker::~SystemAudioLoopbackWorker()
{
    stop();
}


void SystemAudioLoopbackWorker::setIncludeSystem(bool includeSystem)
{
    m_includeSystem.store(includeSystem);
}


void SystemAudioLoopbackWorker::setIncludeMicrophone(bool includeMicrophone)
{
    m_includeMicrophone.store(includeMicrophone);
}


void SystemAudioLoopbackWorker::start()
{
    if (m_running.exchange(true))
        return;

    m_stopRequested.store(false);
    m_mixerThread = std::thread([this]() {
        try
        {
            runMixer();
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Audio mixer thread crashed: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("Audio mixer thread crashed: unknown error");
        }
    });
    if (m_includeSystem.load())
    {
        m_captureThreads.emplace_back([this]() {
            try
            {
                captureSystemAudio();
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("System audio capture thread crashed: {}", e.what());
            }
            catch (...)
            {
                LOG_ERROR("System audio capture thread crashed: unknown error");
            }
        });
    }
    if (m_includeMicrophone.load())
    {
        m_captureThreads.emplace_back([this]() {
            try
            {
                captureMicrophoneAudio();
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Microphone capture thread crashed: {}", e.what());
            }
            catch (...)
            {
                LOG_ERROR("Microphone capture thread crashed: unknown error");
            }
        });
    }
}


void SystemAudioLoopbackWorker::stop()
{
    m_stopRequested.store(true);
    for (auto &thread : m_captureThreads)
    {
        if (thread.joinable())
            thread.join();
    }
    m_captureThreads.clear();
    if (m_mixerThread.joinable())
        m_mixerThread.join();
    m_running.store(false);
}


bool SystemAudioLoopbackWorker::isRunning() const
{
    return m_running.load();
}
