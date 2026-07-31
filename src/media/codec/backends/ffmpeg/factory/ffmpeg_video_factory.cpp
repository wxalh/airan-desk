#include "media/codec/backends/ffmpeg/factory/ffmpeg_video_factory.h"

#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_types.h"
#include "media/codec/backends/ffmpeg/decoder/ffmpeg_video_decoder.h"
#include "media/codec/backends/ffmpeg/encoder/core/ffmpeg_video_encoder.h"

#if AIRAN_WEBRTC_MILESTONE >= 144
#include <api/environment/environment.h>
#include <api/video_codecs/scalability_mode_helper.h>
#endif
#include <modules/video_coding/codecs/h264/include/h264.h>

#if AIRAN_WEBRTC_MILESTONE < 144
#include <absl/types/optional.h>
#endif
#include <memory>
#include <optional>

namespace airan::media::ffmpeg
{

namespace
{

#if AIRAN_WEBRTC_MILESTONE >= 144
bool scalabilityModeSupported(const std::optional<std::string> &scalabilityMode)
#else
bool scalabilityModeSupported(const absl::optional<std::string> &scalabilityMode)
#endif
{
    if (!scalabilityMode)
        return true;
#if AIRAN_WEBRTC_MILESTONE >= 144
    const auto parsed = webrtc::ScalabilityModeStringToEnum(*scalabilityMode);
    return parsed && *parsed == webrtc::ScalabilityMode::kL1T1;
#else
    return *scalabilityMode == "L1T1";
#endif
}

webrtc::H264PacketizationMode h264PacketizationMode(const webrtc::SdpVideoFormat &format)
{
    const auto it = format.parameters.find("packetization-mode");
    if (it != format.parameters.end() && it->second == "0")
        return webrtc::H264PacketizationMode::SingleNalUnit;
    return webrtc::H264PacketizationMode::NonInterleaved;
}

} // namespace

class FfmpegVideoEncoderFactory final : public webrtc::VideoEncoderFactory
{
public:
    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override
    {
        return supportedFormats(true);
    }

    std::vector<webrtc::SdpVideoFormat> GetImplementations() const override
    {
        return GetSupportedFormats();
    }

    CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat &format,
#if AIRAN_WEBRTC_MILESTONE >= 144
                                   std::optional<std::string> scalabilityMode) const override
#else
                                   absl::optional<std::string> scalabilityMode) const override
#endif
    {
        const CodecKind codec = codecKindFromFormat(format);
        const bool h264PacketizationSupported =
            codec != CodecKind::H264 ||
            h264PacketizationMode(format) == webrtc::H264PacketizationMode::NonInterleaved;
        CodecSupport support;
        support.is_supported = scalabilityModeSupported(scalabilityMode) && codecAvailable(codec, true) && h264PacketizationSupported;
        support.is_power_efficient = support.is_supported && codecPowerEfficient(codec, true);
        return support;
    }

#if AIRAN_WEBRTC_MILESTONE >= 144
    std::unique_ptr<webrtc::VideoEncoder> Create(const webrtc::Environment &,
                                                 const webrtc::SdpVideoFormat &format) override
#else
    std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat &format) override
#endif
    {
#if defined(AIRAN_HAVE_FFMPEG)
        const CodecKind codec = codecKindFromFormat(format);
        if (!codecAvailable(codec, true))
            return nullptr;
        const auto packetizationMode = h264PacketizationMode(format);
        if (codec == CodecKind::H264 &&
            packetizationMode != webrtc::H264PacketizationMode::NonInterleaved)
            return nullptr;
        return std::make_unique<FfmpegVideoEncoder>(codec, packetizationMode);
#else
        (void)format;
        return nullptr;
#endif
    }
};

class FfmpegVideoDecoderFactory final : public webrtc::VideoDecoderFactory
{
public:
    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override
    {
        return supportedFormats(false);
    }

    CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat &format, bool referenceScaling) const override
    {
        const CodecKind codec = codecKindFromFormat(format);
        CodecSupport support;
        support.is_supported = !referenceScaling && codecAvailable(codec, false);
        support.is_power_efficient = support.is_supported && codecPowerEfficient(codec, false);
        return support;
    }

#if AIRAN_WEBRTC_MILESTONE >= 144
    std::unique_ptr<webrtc::VideoDecoder> Create(const webrtc::Environment &,
                                                 const webrtc::SdpVideoFormat &format) override
#else
    std::unique_ptr<webrtc::VideoDecoder> CreateVideoDecoder(const webrtc::SdpVideoFormat &format) override
#endif
    {
#if defined(AIRAN_HAVE_FFMPEG)
        const CodecKind codec = codecKindFromFormat(format);
        if (!codecAvailable(codec, false))
            return nullptr;
        return std::make_unique<FfmpegVideoDecoder>(codec);
#else
        (void)format;
        return nullptr;
#endif
    }
};

webrtc::VideoEncoderFactory *createEncoderFactory()
{
    return encoderAvailable() ? new FfmpegVideoEncoderFactory() : nullptr;
}

webrtc::VideoDecoderFactory *createDecoderFactory()
{
    return decoderAvailable() ? new FfmpegVideoDecoderFactory() : nullptr;
}

} // namespace airan::media::ffmpeg
