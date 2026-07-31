#pragma once

#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_probe.h"

namespace airan::media::ffmpeg
{


bool openCodecProbe(const CodecProbe &probe, bool encoder, bool hardwareFrames);

} // namespace airan::media::ffmpeg
