#pragma once

#include <api/video_codecs/video_decoder_factory.h>
#include <api/video_codecs/video_encoder_factory.h>

#include <memory>

namespace airan::media
{

std::unique_ptr<webrtc::VideoEncoderFactory> createAiranVideoEncoderFactory();
std::unique_ptr<webrtc::VideoDecoderFactory> createAiranVideoDecoderFactory();

} // namespace airan::media
