#ifndef AIRAN_RTC_PEER_CONNECTION_FACTORIES_H
#define AIRAN_RTC_PEER_CONNECTION_FACTORIES_H

#include "rtc/core/rtc.hpp"

#include <api/video_codecs/video_decoder_factory.h>
#include <api/video_codecs/video_encoder_factory.h>

#include <memory>

namespace webrtc
{
class AudioDeviceModule;
}

namespace rtc
{


std::unique_ptr<webrtc::VideoEncoderFactory> createAiranVideoEncoderFactory();


std::unique_ptr<webrtc::VideoDecoderFactory> createAiranVideoDecoderFactory();


scoped_refptr<webrtc::AudioDeviceModule> createAudioDeviceModule(bool enabled);

} // namespace rtc

#endif /* AIRAN_RTC_PEER_CONNECTION_FACTORIES_H */
