#include "ui/settings/audio/settings_audio_backend.h"
#include "ui/settings/audio/settings_audio_backend_internal.h"

#if defined(Q_OS_MACOS)

#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>

#include <QApplication>
#include <QObject>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace
{
QString cfStringToQString(CFStringRef value)
{
    if (!value)
        return {};

    char buffer[1024] = {};
    if (CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8))
        return QString::fromUtf8(buffer);
    return {};
}

QString deviceStringProperty(AudioDeviceID device, AudioObjectPropertySelector selector)
{
    CFStringRef value = nullptr;
    UInt32 size = sizeof(value);
    AudioObjectPropertyAddress address{selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &value) != noErr || !value)
        return {};

    const QString out = cfStringToQString(value);
    CFRelease(value);
    return out;
}

bool hasInputStreams(AudioDeviceID device)
{
    UInt32 size = 0;
    AudioObjectPropertyAddress address{kAudioDevicePropertyStreams, kAudioDevicePropertyScopeInput, kAudioObjectPropertyElementMain};
    return AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) == noErr && size > 0;
}

CFStringRef qStringToCfString(const QString &value)
{
    if (value.isEmpty())
        return nullptr;
    return CFStringCreateWithCharacters(kCFAllocatorDefault,
                                        reinterpret_cast<const UniChar *>(value.utf16()),
                                        value.size());
}

struct MicLevelContext
{
    std::atomic_bool *running = nullptr;
    QObject *receiver = nullptr;
    AudioQueueRef queue = nullptr;
};

void micLevelInputCallback(void *userData,
                           AudioQueueRef queue,
                           AudioQueueBufferRef buffer,
                           const AudioTimeStamp *,
                           UInt32,
                           const AudioStreamPacketDescription *)
{
    auto *context = static_cast<MicLevelContext *>(userData);
    if (!context || !buffer)
        return;

    const auto *samples = static_cast<const int16_t *>(buffer->mAudioData);
    const size_t sampleCount = buffer->mAudioDataByteSize / sizeof(int16_t);
    double sumSquares = 0.0;
    for (size_t i = 0; samples && i < sampleCount; ++i)
    {
        const double normalized = static_cast<double>(samples[i]) / 32768.0;
        sumSquares += normalized * normalized;
    }
    const double rms = sampleCount > 0 ? std::sqrt(sumSquares / sampleCount) : 0.0;
    SettingsAudioBackend::Internal::postMicLevel(
        context->receiver,
        static_cast<float>(std::clamp(rms * 3.0, 0.0, 1.0)));

    if (context->running && context->running->load())
        AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}
} // namespace

namespace SettingsAudioBackend
{

QList<AudioDeviceItem> enumerateAudioDevices()
{
    QList<AudioDeviceItem> out;

    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr || size == 0)
        return out;

    std::vector<AudioDeviceID> devices(size / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.data()) != noErr)
        return out;

    for (AudioDeviceID device : devices)
    {
        if (!hasInputStreams(device))
            continue;

        AudioDeviceItem item;
        item.id = deviceStringProperty(device, kAudioDevicePropertyDeviceUID);
        item.displayName = deviceStringProperty(device, kAudioObjectPropertyName);
        if (item.displayName.isEmpty())
            item.displayName = item.id;
        item.loopback = false;
        if (!item.id.isEmpty())
            out.push_back(item);
    }
    return out;
}

bool playToneOnOutput(const QString &)
{
    QApplication::beep();
    return true;
}

void runMicLevelTest(const QString &configuredInput, std::atomic_bool *running, QObject *receiver)
{
    if (!running || !running->load())
    {
        Internal::postMicStopped(receiver);
        return;
    }

    AudioStreamBasicDescription format{};
    format.mSampleRate = 48000;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mBitsPerChannel = 16;
    format.mChannelsPerFrame = 2;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(int16_t) * format.mChannelsPerFrame;
    format.mBytesPerPacket = format.mBytesPerFrame;

    MicLevelContext context;
    context.running = running;
    context.receiver = receiver;
    OSStatus status = AudioQueueNewInput(&format,
                                         micLevelInputCallback,
                                         &context,
                                         nullptr,
                                         kCFRunLoopCommonModes,
                                         0,
                                         &context.queue);
    if (status != noErr || !context.queue)
    {
        Internal::postMicStopped(receiver);
        return;
    }

    CFStringRef configuredUid = qStringToCfString(configuredInput.trimmed());
    if (configuredUid)
    {
        status = AudioQueueSetProperty(context.queue,
                                       kAudioQueueProperty_CurrentDevice,
                                       &configuredUid,
                                       sizeof(configuredUid));
        CFRelease(configuredUid);
        if (status != noErr)
        {
            AudioQueueDispose(context.queue, true);
            Internal::postMicStopped(receiver);
            return;
        }
    }

    constexpr int kBufferCount = 3;
    constexpr UInt32 kBufferBytes = 48000 / 50 * 2 * sizeof(int16_t);
    bool buffersReady = true;
    for (int i = 0; i < kBufferCount; ++i)
    {
        AudioQueueBufferRef buffer = nullptr;
        if (AudioQueueAllocateBuffer(context.queue, kBufferBytes, &buffer) != noErr ||
            !buffer || AudioQueueEnqueueBuffer(context.queue, buffer, 0, nullptr) != noErr)
        {
            buffersReady = false;
            break;
        }
    }

    if (buffersReady)
        status = AudioQueueStart(context.queue, nullptr);
    if (!buffersReady || status != noErr)
    {
        AudioQueueDispose(context.queue, true);
        Internal::postMicStopped(receiver);
        return;
    }

    while (running->load())
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, false);

    AudioQueueStop(context.queue, true);
    AudioQueueDispose(context.queue, true);
    Internal::postMicStopped(receiver);
}

} // namespace SettingsAudioBackend
#endif
