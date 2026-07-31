#pragma once

#include "rtc/core/rtc_media_types.h"

#include <api/video_codecs/sdp_video_format.h>
#include <api/video_codecs/video_decoder_factory.h>
#include <api/video_codecs/video_encoder_factory.h>

#include <memory>
#include <vector>

namespace airan::media
{

std::vector<webrtc::SdpVideoFormat> supportedH264Formats();

std::vector<std::unique_ptr<webrtc::VideoEncoderFactory>> createBuiltinVideoEncoderBackends();
std::vector<std::unique_ptr<webrtc::VideoDecoderFactory>> createBuiltinVideoDecoderBackends();
void warmLocalVideoCodecCapabilities();
std::vector<rtc::VideoCodecCapability> localVideoCodecCapabilities();

} // namespace airan::media
