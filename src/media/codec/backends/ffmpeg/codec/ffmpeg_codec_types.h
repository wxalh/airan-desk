#pragma once

#include "media/codec/backends/ffmpeg/runtime/ffmpeg_runtime.h"
#include "rtc/core/rtc_media_types.h"

#include <api/video_codecs/sdp_video_format.h>
#include <modules/video_coding/include/video_codec_interface.h>

#include <cstddef>
#include <vector>

namespace airan::media::ffmpeg
{

constexpr int32_t kAiranCodecTryNextBackend = -30001;

enum class CodecKind
{
    Unknown,
    H264,
    VP8,
    VP9,
    AV1,
};

struct CodecProbe
{
    CodecKind codec;
    const char *backend;
    const char *ffmpegCodec;
    const char *zeroCopy;
    bool encoder;
    bool decoder;
#if defined(AIRAN_HAVE_FFMPEG)
    AVHWDeviceType deviceType;
    AVPixelFormat hardwarePixelFormat;
    AVPixelFormat softwarePixelFormat;
#endif
};

const char *codecName(CodecKind codec);
webrtc::VideoCodecType webrtcCodecType(CodecKind codec);
CodecKind codecKindFromFormat(const webrtc::SdpVideoFormat &format);

std::vector<const CodecProbe *> selectProbes(CodecKind codec, bool encoder);
const CodecProbe *selectProbe(CodecKind codec, bool encoder);
bool codecAvailable(CodecKind codec, bool encoder);
bool codecPowerEfficient(CodecKind codec, bool encoder);
bool encoderAvailable();
bool decoderAvailable();

std::vector<rtc::VideoCodecCapability> codecCapabilities();
std::vector<webrtc::SdpVideoFormat> supportedFormats(bool encoder);
const char *probeDiagnostics();

} // namespace airan::media::ffmpeg
