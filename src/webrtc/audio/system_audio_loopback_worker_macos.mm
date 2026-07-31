#include "system_audio_loopback_worker.h"

#include "common/logger_manager.h"
#include "util/config/config_util.h"

#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace
{
constexpr int kSampleRate = 48000;
constexpr size_t kChannels = 2;
constexpr size_t kFrameSamples = kSampleRate / 100;
constexpr const char *kAudioDeviceNoneValue = "__none__";
constexpr int64_t kPermissionTimeoutSeconds = 30;
constexpr int64_t kCaptureOperationTimeoutSeconds = 15;


bool isNoneDeviceValue(const QString &device)
{
    return device.trimmed().compare(QString::fromLatin1(kAudioDeviceNoneValue), Qt::CaseInsensitive) == 0;
}

QString cfStringToQString(CFStringRef value)
{
    if (!value)
        return {};

    char buffer[1024] = {};
    if (CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8))
        return QString::fromUtf8(buffer);
    return {};
}

CFStringRef configuredMacInputDeviceUid()
{
    const QString configured = ConfigUtil->audio_mic_device.trimmed();
    if (configured.isEmpty() || isNoneDeviceValue(configured))
        return nullptr;

    AudioObjectPropertyAddress devicesAddress{
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain};
    UInt32 dataSize = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devicesAddress, 0, nullptr, &dataSize) != noErr || dataSize == 0)
        return nullptr;

    std::vector<AudioDeviceID> devices(dataSize / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &devicesAddress, 0, nullptr, &dataSize, devices.data()) != noErr)
        return nullptr;

    for (AudioDeviceID device : devices)
    {
        UInt32 streamSize = 0;
        AudioObjectPropertyAddress streamAddress{
            kAudioDevicePropertyStreams,
            kAudioDevicePropertyScopeInput,
            kAudioObjectPropertyElementMain};
        if (AudioObjectGetPropertyDataSize(device, &streamAddress, 0, nullptr, &streamSize) != noErr || streamSize == 0)
            continue;

        CFStringRef uid = nullptr;
        UInt32 uidSize = sizeof(uid);
        AudioObjectPropertyAddress uidAddress{
            kAudioDevicePropertyDeviceUID,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain};
        if (AudioObjectGetPropertyData(device, &uidAddress, 0, nullptr, &uidSize, &uid) != noErr || !uid)
            continue;

        const QString deviceUid = cfStringToQString(uid);
        if (deviceUid.compare(configured, Qt::CaseInsensitive) == 0)
            return uid;
        CFRelease(uid);
    }

    LOG_WARN("Configured macOS microphone device was not found, falling back to default: device={}", configured);
    return nullptr;
}

int16_t clampFloatToS16(float sample)
{
    sample = std::max(-1.0f, std::min(1.0f, sample));
    return static_cast<int16_t>(std::lrint(sample * 32767.0f));
}

bool waitForDispatchSemaphore(dispatch_semaphore_t semaphore, int64_t timeoutSeconds, const char *operation)
{
    const dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, timeoutSeconds * NSEC_PER_SEC);
    if (dispatch_semaphore_wait(semaphore, timeout) == 0)
        return true;
    LOG_WARN("macOS {} timed out after {} seconds", operation ? operation : "operation", timeoutSeconds);
    return false;
}

void releaseDispatchSemaphore(dispatch_semaphore_t semaphore)
{
    if (!semaphore)
        return;
#if OS_OBJECT_USE_OBJC
    [semaphore release];
#else
    dispatch_release(semaphore);
#endif
}

void releaseDispatchQueue(dispatch_queue_t queue)
{
    if (!queue)
        return;
#if OS_OBJECT_USE_OBJC
    [queue release];
#else
    dispatch_release(queue);
#endif
}

bool ensureMacMicrophoneAccess()
{
    const AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (status == AVAuthorizationStatusAuthorized)
        return true;
    if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted)
    {
        LOG_WARN("macOS microphone permission is denied");
        return false;
    }

    __block BOOL granted = NO;
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL allowed) {
        granted = allowed;
        dispatch_semaphore_signal(semaphore);
    }];
    if (!waitForDispatchSemaphore(semaphore, kPermissionTimeoutSeconds, "microphone permission request"))
        return false;
    releaseDispatchSemaphore(semaphore);
    return granted == YES;
}

struct MacMicContext
{
    SystemAudioLoopbackWorker *owner{nullptr};
    const std::atomic_bool *stopRequested{nullptr};
    AudioQueueRef queue{nullptr};
};

void macMicInputCallback(void *userData,
                         AudioQueueRef queue,
                         AudioQueueBufferRef buffer,
                         const AudioTimeStamp *,
                         UInt32,
                         const AudioStreamPacketDescription *)
{
    auto *context = static_cast<MacMicContext *>(userData);
    if (!context || !context->owner || !buffer)
        return;

    const size_t bytes = buffer->mAudioDataByteSize;
    const size_t frames = bytes / (sizeof(int16_t) * kChannels);
    if (frames > 0)
        context->owner->appendSamples(SystemAudioLoopbackWorker::Source::Microphone,
                                      static_cast<const int16_t *>(buffer->mAudioData),
                                      frames,
                                      kSampleRate,
                                      kChannels);

    if (!context->stopRequested || !context->stopRequested->load())
        AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}

std::vector<int16_t> sampleBufferToS16(CMSampleBufferRef sampleBuffer, int *sampleRate, size_t *channels)
{
    if (!sampleBuffer)
        return {};

    CMAudioFormatDescriptionRef formatDescription = CMSampleBufferGetFormatDescription(sampleBuffer);
    const AudioStreamBasicDescription *asbd = formatDescription ? CMAudioFormatDescriptionGetStreamBasicDescription(formatDescription) : nullptr;
    if (!asbd)
        return {};

    *sampleRate = static_cast<int>(asbd->mSampleRate);
    *channels = std::max<size_t>(1, asbd->mChannelsPerFrame);

    const bool nonInterleaved = (asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
    const size_t audioBufferCount = nonInterleaved ? std::max<size_t>(1, asbd->mChannelsPerFrame) : 1;
    const size_t audioBufferListBytes = offsetof(AudioBufferList, mBuffers) + audioBufferCount * sizeof(AudioBuffer);
    std::vector<uint8_t> audioBufferListStorage(audioBufferListBytes, 0);
    auto *audioBufferList = reinterpret_cast<AudioBufferList *>(audioBufferListStorage.data());
    audioBufferList->mNumberBuffers = static_cast<UInt32>(audioBufferCount);

    CMBlockBufferRef blockBuffer = nullptr;
    size_t bufferListSizeNeeded = 0;
    OSStatus status = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sampleBuffer,
                                                                              &bufferListSizeNeeded,
                                                                              audioBufferList,
                                                                              audioBufferListBytes,
                                                                              nullptr,
                                                                              nullptr,
                                                                              kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
                                                                              &blockBuffer);
    if (status != noErr)
        return {};

    std::vector<int16_t> out;
    const bool floatFormat = (asbd->mFormatFlags & kAudioFormatFlagIsFloat) != 0;
    const bool signedInteger = (asbd->mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0;
    const UInt32 bits = asbd->mBitsPerChannel;
    const size_t frames = static_cast<size_t>(CMSampleBufferGetNumSamples(sampleBuffer));
    out.resize(frames * (*channels));

    if (audioBufferList->mNumberBuffers == 1)
    {
        const auto *bytes = static_cast<const uint8_t *>(audioBufferList->mBuffers[0].mData);
        if (!bytes)
        {
            if (blockBuffer)
                CFRelease(blockBuffer);
            return {};
        }
        if (floatFormat && bits == 32)
        {
            const auto *samples = reinterpret_cast<const float *>(bytes);
            const size_t sampleCount = std::min(out.size(),
                                                static_cast<size_t>(audioBufferList->mBuffers[0].mDataByteSize) / sizeof(float));
            for (size_t i = 0; i < sampleCount; ++i)
                out[i] = clampFloatToS16(samples[i]);
        }
        else if (signedInteger && bits == 16)
        {
            const size_t bytesToCopy = std::min(out.size() * sizeof(int16_t),
                                                static_cast<size_t>(audioBufferList->mBuffers[0].mDataByteSize));
            std::memcpy(out.data(), bytes, bytesToCopy);
        }
    }
    else
    {
        for (UInt32 channel = 0; channel < audioBufferList->mNumberBuffers && channel < *channels; ++channel)
        {
            const auto *bytes = static_cast<const uint8_t *>(audioBufferList->mBuffers[channel].mData);
            if (!bytes)
                continue;
            if (floatFormat && bits == 32)
            {
                const auto *samples = reinterpret_cast<const float *>(bytes);
                const size_t sampleCount = std::min(frames,
                                                    static_cast<size_t>(audioBufferList->mBuffers[channel].mDataByteSize) / sizeof(float));
                for (size_t frame = 0; frame < sampleCount; ++frame)
                    out[frame * (*channels) + channel] = clampFloatToS16(samples[frame]);
            }
            else if (signedInteger && bits == 16)
            {
                const auto *samples = reinterpret_cast<const int16_t *>(bytes);
                const size_t sampleCount = std::min(frames,
                                                    static_cast<size_t>(audioBufferList->mBuffers[channel].mDataByteSize) / sizeof(int16_t));
                for (size_t frame = 0; frame < sampleCount; ++frame)
                    out[frame * (*channels) + channel] = samples[frame];
            }
        }
    }

    if (blockBuffer)
        CFRelease(blockBuffer);
    return out;
}
} // namespace

API_AVAILABLE(macos(13.0))
@interface AiranScreenAudioDelegate : NSObject <SCStreamOutput, SCStreamDelegate>
{
@public
    std::atomic<SystemAudioLoopbackWorker *> owner;
    std::atomic_bool stopped;
}
@end

@implementation AiranScreenAudioDelegate
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type
{
    (void)stream;
    SystemAudioLoopbackWorker *currentOwner = owner.load();
    if (type != SCStreamOutputTypeAudio || !currentOwner)
        return;

    int sampleRate = kSampleRate;
    size_t channels = kChannels;
    std::vector<int16_t> samples = sampleBufferToS16(sampleBuffer, &sampleRate, &channels);
    if (!samples.empty())
        currentOwner->appendSamples(SystemAudioLoopbackWorker::Source::System,
                                    samples.data(),
                                    samples.size() / channels,
                                    sampleRate,
                                    channels);
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
    (void)stream;
    stopped.store(true);
    if (error)
        LOG_WARN("ScreenCaptureKit audio stream stopped unexpectedly: {}", [[error localizedDescription] UTF8String]);
}
@end

void captureMacSystemAudio(SystemAudioLoopbackWorker *owner, const std::atomic_bool &stopRequested)
{
    if (!owner)
        return;

    if (@available(macOS 13.0, *))
    {
        @autoreleasepool
        {
            if (@available(macOS 10.15, *))
            {
                if (!CGPreflightScreenCaptureAccess() && !CGRequestScreenCaptureAccess())
                {
                    LOG_WARN("macOS screen recording permission is denied; system audio capture is disabled");
                    return;
                }
            }

            __block SCShareableContent *content = nil;
            dispatch_semaphore_t contentSemaphore = dispatch_semaphore_create(0);
            [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *shareableContent, NSError *error) {
                if (error)
                    LOG_WARN("ScreenCaptureKit shareable content query failed: {}", [[error localizedDescription] UTF8String]);
                content = [shareableContent retain];
                dispatch_semaphore_signal(contentSemaphore);
            }];
            if (!waitForDispatchSemaphore(contentSemaphore, kCaptureOperationTimeoutSeconds, "ScreenCaptureKit content query"))
                return;
            releaseDispatchSemaphore(contentSemaphore);

            SCDisplay *display = content.displays.firstObject;
            if (!display)
            {
                [content release];
                LOG_WARN("ScreenCaptureKit display was not found; system audio capture is disabled");
                return;
            }

            SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
            [content release];
            SCStreamConfiguration *configuration = [[SCStreamConfiguration alloc] init];
            configuration.capturesAudio = YES;
            configuration.excludesCurrentProcessAudio = YES;
            configuration.width = 2;
            configuration.height = 2;
            configuration.minimumFrameInterval = CMTimeMake(1, 5);

            AiranScreenAudioDelegate *delegate = [[AiranScreenAudioDelegate alloc] init];
            delegate->owner.store(owner);
            delegate->stopped.store(false);
            SCStream *stream = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:delegate];
            dispatch_queue_t queue = dispatch_queue_create("airan-desk.screen-audio", DISPATCH_QUEUE_SERIAL);
            NSError *outputError = nil;
            if (![stream addStreamOutput:delegate type:SCStreamOutputTypeAudio sampleHandlerQueue:queue error:&outputError])
            {
                LOG_WARN("ScreenCaptureKit audio output registration failed: {}", outputError ? [[outputError localizedDescription] UTF8String] : "unknown");
                delegate->owner.store(nullptr);
                [stream release];
                [delegate release];
                [configuration release];
                [filter release];
                releaseDispatchQueue(queue);
                return;
            }

            __block BOOL captureStarted = NO;
            dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);
            [stream startCaptureWithCompletionHandler:^(NSError *error) {
                captureStarted = error == nil;
                if (error)
                    LOG_WARN("ScreenCaptureKit audio capture start failed: {}", [[error localizedDescription] UTF8String]);
                dispatch_semaphore_signal(startSemaphore);
            }];
            const bool startCompleted = waitForDispatchSemaphore(startSemaphore,
                                                                  kCaptureOperationTimeoutSeconds,
                                                                  "ScreenCaptureKit start");
            if (startCompleted)
                releaseDispatchSemaphore(startSemaphore);

            if (startCompleted && captureStarted)
            {
                LOG_INFO("ScreenCaptureKit system audio capture started");
                while (!stopRequested.load() && !delegate->stopped.load())
                    [NSThread sleepForTimeInterval:0.02];
            }

            dispatch_semaphore_t stopSemaphore = dispatch_semaphore_create(0);
            [stream stopCaptureWithCompletionHandler:^(NSError *error) {
                if (error)
                    LOG_WARN("ScreenCaptureKit audio capture stop failed: {}", [[error localizedDescription] UTF8String]);
                dispatch_semaphore_signal(stopSemaphore);
            }];
            const bool stopCompleted = waitForDispatchSemaphore(stopSemaphore,
                                                                 kCaptureOperationTimeoutSeconds,
                                                                 "ScreenCaptureKit stop");
            if (stopCompleted)
                releaseDispatchSemaphore(stopSemaphore);

            delegate->owner.store(nullptr);
            NSError *removeError = nil;
            if (![stream removeStreamOutput:delegate type:SCStreamOutputTypeAudio error:&removeError] && removeError)
                LOG_WARN("ScreenCaptureKit audio output removal failed: {}", [[removeError localizedDescription] UTF8String]);
            [stream release];
            [delegate release];
            [configuration release];
            [filter release];
            releaseDispatchQueue(queue);
            LOG_INFO("ScreenCaptureKit system audio capture stopped");
            return;
        }
    }

    LOG_WARN("ScreenCaptureKit system audio requires macOS 13.0 or later");
}

void captureMacMicrophoneAudio(SystemAudioLoopbackWorker *owner, const std::atomic_bool &stopRequested)
{
    if (!owner)
        return;
    if (!ensureMacMicrophoneAccess())
        return;

    @autoreleasepool
    {
        AudioStreamBasicDescription format{};
        format.mSampleRate = kSampleRate;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
        format.mBitsPerChannel = 16;
        format.mChannelsPerFrame = kChannels;
        format.mFramesPerPacket = 1;
        format.mBytesPerFrame = sizeof(int16_t) * kChannels;
        format.mBytesPerPacket = format.mBytesPerFrame;

        MacMicContext context;
        context.owner = owner;
        context.stopRequested = &stopRequested;

        OSStatus status = AudioQueueNewInput(&format, macMicInputCallback, &context, nullptr, kCFRunLoopCommonModes, 0, &context.queue);
        if (status != noErr || !context.queue)
        {
            LOG_WARN("macOS microphone AudioQueue creation failed: status={}", status);
            return;
        }

        CFStringRef configuredDeviceUid = configuredMacInputDeviceUid();
        if (configuredDeviceUid)
        {
            status = AudioQueueSetProperty(context.queue, kAudioQueueProperty_CurrentDevice, &configuredDeviceUid, sizeof(configuredDeviceUid));
            if (status != noErr)
                LOG_WARN("macOS microphone AudioQueue device selection failed: status={}", status);
            CFRelease(configuredDeviceUid);
        }

        constexpr int kBufferCount = 3;
        constexpr UInt32 kBufferBytes = static_cast<UInt32>(kFrameSamples * kChannels * sizeof(int16_t));
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
            LOG_WARN("macOS microphone AudioQueue start failed: status={}", status);
            AudioQueueDispose(context.queue, true);
            return;
        }

        LOG_INFO("macOS microphone capture started");
        while (!stopRequested.load())
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, false);

        AudioQueueStop(context.queue, true);
        AudioQueueDispose(context.queue, true);
        LOG_INFO("macOS microphone capture stopped");
    }
}


void SystemAudioLoopbackWorker::captureSystemAudio()
{
    if (isNoneDeviceValue(ConfigUtil->audio_loopback_device))
    {
        LOG_INFO("macOS system audio capture is disabled by settings");
        return;
    }
    captureMacSystemAudio(this, m_stopRequested);
}


void SystemAudioLoopbackWorker::captureMicrophoneAudio()
{
    if (isNoneDeviceValue(ConfigUtil->audio_mic_device))
    {
        LOG_INFO("macOS microphone capture is disabled by settings");
        return;
    }
    captureMacMicrophoneAudio(this, m_stopRequested);
}
