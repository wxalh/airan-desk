#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_types.h"

#include <modules/video_coding/codecs/h264/include/h264.h>

namespace airan::media::ffmpeg
{


const char *codecName(CodecKind codec)
{
    switch (codec)
    {
    case CodecKind::Unknown:
        return "";
    case CodecKind::H264:
        return "H264";
    case CodecKind::VP8:
        return "VP8";
    case CodecKind::VP9:
        return "VP9";
    case CodecKind::AV1:
        return "AV1";
    }
    return "";
}


webrtc::VideoCodecType webrtcCodecType(CodecKind codec)
{
    switch (codec)
    {
    case CodecKind::Unknown:
        return webrtc::kVideoCodecGeneric;
    case CodecKind::H264:
        return webrtc::kVideoCodecH264;
    case CodecKind::VP8:
        return webrtc::kVideoCodecVP8;
    case CodecKind::VP9:
        return webrtc::kVideoCodecVP9;
    case CodecKind::AV1:
        return webrtc::kVideoCodecAV1;
    }
    return webrtc::kVideoCodecGeneric;
}


CodecKind codecKindFromFormat(const webrtc::SdpVideoFormat &format)
{
    switch (webrtc::PayloadStringToCodecType(format.name))
    {
    case webrtc::kVideoCodecH264:
        return CodecKind::H264;
    case webrtc::kVideoCodecVP8:
        return CodecKind::VP8;
    case webrtc::kVideoCodecVP9:
        return CodecKind::VP9;
    case webrtc::kVideoCodecAV1:
        return CodecKind::AV1;
    default:
        return CodecKind::Unknown;
    }
}

} // namespace airan::media::ffmpeg
