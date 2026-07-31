#pragma once

#include <api/video_codecs/video_decoder_factory.h>
#include <api/video_codecs/video_encoder_factory.h>

namespace airan::media::ffmpeg
{

webrtc::VideoEncoderFactory *createEncoderFactory();
webrtc::VideoDecoderFactory *createDecoderFactory();

} // namespace airan::media::ffmpeg
