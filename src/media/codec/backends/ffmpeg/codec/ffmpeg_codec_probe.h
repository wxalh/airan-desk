#pragma once

#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_types.h"

#include <cstddef>

namespace airan::media::ffmpeg
{


const CodecProbe *codecProbes(size_t *count);


bool openEncoderProbe(const CodecProbe &probe);


bool openDecoderProbe(const CodecProbe &probe);


#if defined(AIRAN_HAVE_FFMPEG)
bool encoderProbeAcceptsPixelFormat(const CodecProbe &probe, AVPixelFormat pixelFormat);
bool probeRuntimeDependenciesAvailable(const CodecProbe &probe);
#endif

} // namespace airan::media::ffmpeg
