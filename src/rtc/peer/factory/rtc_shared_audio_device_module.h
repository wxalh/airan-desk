#ifndef AIRAN_RTC_SHARED_AUDIO_DEVICE_MODULE_H
#define AIRAN_RTC_SHARED_AUDIO_DEVICE_MODULE_H

#include "rtc/core/rtc_base_types.h"

namespace webrtc
{
class AudioDeviceModule;
}

namespace rtc
{

scoped_refptr<webrtc::AudioDeviceModule> createSharedAudioDeviceModule(scoped_refptr<webrtc::AudioDeviceModule> platform);
void setSharedAudioRecordingMode(const scoped_refptr<webrtc::AudioDeviceModule> &module, bool mixedSystemAndMicrophone);

} // namespace rtc

#endif /* AIRAN_RTC_SHARED_AUDIO_DEVICE_MODULE_H */
