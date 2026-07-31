#include "system_audio_loopback_worker.h"

#include "common/logger_manager.h"
#include "util/config/config_util.h"

#include <QtGlobal>

#if defined(Q_OS_LINUX)

#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr int kOutputSampleRate = 48000;
constexpr size_t kOutputChannels = 2;
constexpr size_t kFrameSamples = kOutputSampleRate / 100;
constexpr size_t kFrameInterleavedSamples = kFrameSamples * kOutputChannels;
constexpr const char *kAudioDeviceNoneValue = "__none__";


std::string runCommandFirstLine(const char *command)
{
    FILE *pipe = popen(command, "r");
    if (!pipe)
        return {};
    char buffer[512] = {};
    std::string line;
    if (fgets(buffer, sizeof(buffer), pipe))
        line = buffer;
    pclose(pipe);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        line.pop_back();
    return line;
}


std::string pulseInfoValue(const char *key)
{
    std::string command = "pactl info 2>/dev/null | awk -F': ' '/^";
    command += key;
    command += "/ {print $2; exit}'";
    return runCommandFirstLine(command.c_str());
}


std::string defaultPulseSinkName()
{
    std::string sink = runCommandFirstLine("pactl get-default-sink 2>/dev/null");
    if (!sink.empty())
        return sink;
    return pulseInfoValue("Default Sink");
}


std::string defaultPulseSourceName()
{
    std::string source = runCommandFirstLine("pactl get-default-source 2>/dev/null");
    if (!source.empty())
        return source;
    return pulseInfoValue("Default Source");
}


std::string defaultPulseMonitorName()
{
    const std::string defaultSink = defaultPulseSinkName();
    if (!defaultSink.empty())
    {
        LOG_INFO("PulseAudio default sink resolved for monitor capture: sink={}", defaultSink);
        return defaultSink + ".monitor";
    }
    return runCommandFirstLine("pactl list short sources 2>/dev/null | awk '/\\.monitor/ {print $2; exit}'");
}


bool isNoneDeviceValue(const QString &device)
{
    return device.trimmed().compare(QString::fromLatin1(kAudioDeviceNoneValue), Qt::CaseInsensitive) == 0;
}


std::string configuredPulseMonitorName(bool *disabledBySettings)
{
    if (disabledBySettings)
        *disabledBySettings = false;

    const QString configured = ConfigUtil->audio_loopback_device.trimmed();
    if (isNoneDeviceValue(configured))
    {
        if (disabledBySettings)
            *disabledBySettings = true;
        return {};
    }

    if (configured.isEmpty() || configured == QStringLiteral("@DEFAULT_MONITOR@"))
        return defaultPulseMonitorName();

    std::string device = configured.toStdString();
    const std::string monitorSuffix = ".monitor";
    if (device.size() >= monitorSuffix.size() &&
        device.compare(device.size() - monitorSuffix.size(), monitorSuffix.size(), monitorSuffix) == 0)
        return device;
    return device + monitorSuffix;
}


std::string defaultPulseMicrophoneName()
{
    std::string source = defaultPulseSourceName();
    if (!source.empty() && source.find(".monitor") == std::string::npos)
    {
        LOG_INFO("PulseAudio default microphone source resolved: source={}", source);
        return source;
    }
    return runCommandFirstLine("pactl list short sources 2>/dev/null | awk '$2 !~ /\\.monitor$/ {print $2; exit}'");
}


std::string configuredPulseMicrophoneName(bool *disabledBySettings)
{
    if (disabledBySettings)
        *disabledBySettings = false;

    const QString configured = ConfigUtil->audio_mic_device.trimmed();
    if (isNoneDeviceValue(configured))
    {
        if (disabledBySettings)
            *disabledBySettings = true;
        return {};
    }

    if (configured.isEmpty())
        return defaultPulseMicrophoneName();

    if (configured.endsWith(QStringLiteral(".monitor")))
    {
        LOG_WARN("Configured microphone device is a monitor source and will be ignored: {}", configured);
        return {};
    }
    return configured.toStdString();
}


struct PulseSimpleApi
{
    using PaSimple = struct pa_simple;
    using PaStreamDirection = int;
    struct PaSampleSpec
    {
        int format;
        uint32_t rate;
        uint8_t channels;
    };

    void *handle{nullptr};
    PaSimple *(*simpleNew)(const char *, const char *, PaStreamDirection, const char *, const char *,
                           const PaSampleSpec *, const void *, const void *, int *){nullptr};
    int (*simpleRead)(PaSimple *, void *, size_t, int *){nullptr};
    void (*simpleFree)(PaSimple *){nullptr};

    bool load()
    {
        handle = dlopen("libpulse-simple.so.0", RTLD_LAZY);
        if (!handle)
            return false;
        simpleNew = reinterpret_cast<decltype(simpleNew)>(dlsym(handle, "pa_simple_new"));
        simpleRead = reinterpret_cast<decltype(simpleRead)>(dlsym(handle, "pa_simple_read"));
        simpleFree = reinterpret_cast<decltype(simpleFree)>(dlsym(handle, "pa_simple_free"));
        return simpleNew && simpleRead && simpleFree;
    }

    ~PulseSimpleApi()
    {
        if (handle)
            dlclose(handle);
    }
};

} // namespace


void SystemAudioLoopbackWorker::captureSystemAudio()
{
    PulseSimpleApi pulse;
    if (!pulse.load())
    {
        LOG_WARN("PulseAudio simple API is unavailable; system audio monitor capture is disabled");
        return;
    }

    while (!m_stopRequested.load())
    {
        bool disabledBySettings = false;
        const std::string monitor = configuredPulseMonitorName(&disabledBySettings);
        if (disabledBySettings)
        {
            LOG_INFO("PulseAudio monitor capture is disabled by settings");
            return;
        }
        if (monitor.empty())
        {
            LOG_WARN("PulseAudio/PipeWire monitor source was not found; retrying monitor capture");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        PulseSimpleApi::PaSampleSpec spec{3, kOutputSampleRate, static_cast<uint8_t>(kOutputChannels)};
        int error = 0;
        PulseSimpleApi::PaSimple *stream = nullptr;
        try
        {
            stream = pulse.simpleNew(nullptr, "airan-desk", 2, monitor.c_str(), "remote system audio", &spec, nullptr, nullptr, &error);
        }
        catch (...)
        {
            LOG_WARN("PulseAudio monitor capture open threw an exception: device={}", monitor);
        }
        if (!stream)
        {
            LOG_WARN("PulseAudio monitor capture open failed: device={}, error={}; retrying", monitor, error);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        LOG_INFO("PulseAudio monitor capture started: device={}", monitor);
        std::vector<int16_t> buffer(kFrameInterleavedSamples);
        while (!m_stopRequested.load())
        {
            error = 0;
            int readResult = -1;
            try
            {
                readResult = pulse.simpleRead(stream, buffer.data(), buffer.size() * sizeof(int16_t), &error);
            }
            catch (...)
            {
                LOG_WARN("PulseAudio monitor read threw an exception: device={}", monitor);
                break;
            }
            if (readResult < 0)
            {
                LOG_WARN("PulseAudio monitor read failed: device={}, error={}; reconnecting", monitor, error);
                break;
            }
            appendSamples(Source::System, buffer.data(), kFrameSamples, kOutputSampleRate, kOutputChannels);
        }
        pulse.simpleFree(stream);
        LOG_INFO("PulseAudio monitor capture stopped: device={}", monitor);
        if (!m_stopRequested.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}


void SystemAudioLoopbackWorker::captureMicrophoneAudio()
{
    PulseSimpleApi pulse;
    if (!pulse.load())
    {
        LOG_WARN("PulseAudio simple API is unavailable; microphone capture is disabled to avoid exclusive ALSA access");
        return;
    }

    while (!m_stopRequested.load())
    {
        bool disabledBySettings = false;
        const std::string microphone = configuredPulseMicrophoneName(&disabledBySettings);
        if (disabledBySettings)
        {
            LOG_INFO("PulseAudio microphone capture is disabled by settings");
            return;
        }
        if (microphone.empty())
        {
            LOG_WARN("PulseAudio microphone source was not found; retrying microphone capture");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        PulseSimpleApi::PaSampleSpec spec{3, kOutputSampleRate, static_cast<uint8_t>(kOutputChannels)};
        int error = 0;
        PulseSimpleApi::PaSimple *stream = nullptr;
        try
        {
            stream = pulse.simpleNew(nullptr, "airan-desk", 2, microphone.c_str(), "remote microphone audio", &spec, nullptr, nullptr, &error);
        }
        catch (...)
        {
            LOG_WARN("PulseAudio microphone open threw an exception: device={}", microphone);
        }
        if (!stream)
        {
            LOG_WARN("PulseAudio microphone open failed: device={}, error={}; retrying", microphone, error);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        LOG_INFO("PulseAudio microphone capture started: device={}", microphone);
        std::vector<int16_t> buffer(kFrameInterleavedSamples);
        while (!m_stopRequested.load())
        {
            error = 0;
            int readResult = -1;
            try
            {
                readResult = pulse.simpleRead(stream, buffer.data(), buffer.size() * sizeof(int16_t), &error);
            }
            catch (...)
            {
                LOG_WARN("PulseAudio microphone read threw an exception: device={}", microphone);
                break;
            }
            if (readResult < 0)
            {
                LOG_WARN("PulseAudio microphone read failed: device={}, error={}; reconnecting", microphone, error);
                break;
            }
            appendSamples(Source::Microphone, buffer.data(), kFrameSamples, kOutputSampleRate, kOutputChannels);
        }
        pulse.simpleFree(stream);
        LOG_INFO("PulseAudio microphone capture stopped: device={}", microphone);
        if (!m_stopRequested.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

#endif // defined(Q_OS_LINUX)
