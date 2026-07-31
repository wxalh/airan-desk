#include "media/codec/airan_video_codec_backend.h"

#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_types.h"
#include "media/codec/backends/ffmpeg/factory/ffmpeg_video_factory.h"
#include "common/logger_manager.h"

#include <api/video_codecs/video_decoder_factory_template.h>
#include <api/video_codecs/video_decoder_factory_template_dav1d_adapter.h>
#include <api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h>
#include <api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h>
#include <api/video_codecs/video_encoder_factory_template.h>
#include <api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h>
#include <api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h>
#include <api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h>
#include <modules/video_coding/codecs/h264/include/h264.h>

#include <algorithm>
#include <mutex>
#include <unordered_set>
#include <utility>

namespace airan::media
{
namespace
{
using NonH264WebRtcEncoderFactory = webrtc::VideoEncoderFactoryTemplate<
    webrtc::LibvpxVp8EncoderTemplateAdapter,
    webrtc::LibvpxVp9EncoderTemplateAdapter,
    webrtc::LibaomAv1EncoderTemplateAdapter>;
using NonH264WebRtcDecoderFactory = webrtc::VideoDecoderFactoryTemplate<
    webrtc::LibvpxVp8DecoderTemplateAdapter,
    webrtc::LibvpxVp9DecoderTemplateAdapter,
    webrtc::Dav1dDecoderTemplateAdapter>;

std::once_flag g_capabilities_once;
std::vector<rtc::VideoCodecCapability> g_video_codec_capabilities;

std::string capabilityKey(const rtc::VideoCodecCapability &capability)
{
    return capability.codec + "|" + capability.backend + "|" +
           (capability.canEncode ? "e" : "-") +
           (capability.canDecode ? "d" : "-") + "|" +
           (capability.hardware ? "h" : "s") + "|" +
           capability.zeroCopyPath + "|" +
           std::to_string(capability.maxSpatialLayers) + "|" +
           std::to_string(capability.maxTemporalLayers) + "|" +
           (capability.simulcast ? "simulcast" : "-") + "|" +
           (capability.svc ? "svc" : "-");
}

void appendCapabilities(const std::vector<rtc::VideoCodecCapability> &capabilities,
                        std::vector<rtc::VideoCodecCapability> &out,
                        std::unordered_set<std::string> &seen)
{
    for (const auto &capability : capabilities)
    {
        if (capability.codec.empty() || (!capability.canEncode && !capability.canDecode))
            continue;
        rtc::VideoCodecCapability productCapability = capability;
        if (productCapability.canEncode)
        {
            productCapability.maxSpatialLayers = (std::max)(productCapability.maxSpatialLayers, 3);
            productCapability.maxTemporalLayers = 1;
            productCapability.simulcast = true;
            productCapability.svc = false;
            productCapability.scalabilityModes = {"L1T1"};
            productCapability.notes =
                productCapability.notes.empty()
                    ? "Airan WebRTC adapter composes multiple L1T1 encoder instances for desktop simulcast"
                    : productCapability.notes + "; Airan WebRTC adapter composes multiple L1T1 encoder instances for desktop simulcast";
        }
        else if (productCapability.canDecode)
        {
            productCapability.maxSpatialLayers = (std::max)(1, productCapability.maxSpatialLayers);
            productCapability.maxTemporalLayers = (std::max)(1, productCapability.maxTemporalLayers);
            productCapability.simulcast = false;
            productCapability.svc = false;
            if (productCapability.scalabilityModes.empty())
                productCapability.scalabilityModes = {"L1T1"};
        }
        if (seen.insert(capabilityKey(productCapability)).second)
            out.push_back(std::move(productCapability));
    }
}

void appendInternalFactoryFormats(const std::vector<webrtc::SdpVideoFormat> &formats,
                                  bool encoder,
                                  std::vector<rtc::VideoCodecCapability> &out,
                                  std::unordered_set<std::string> &seen)
{
    std::vector<rtc::VideoCodecCapability> capabilities;
    capabilities.reserve(formats.size());
    for (const auto &format : formats)
    {
        rtc::VideoCodecCapability capability;
        capability.codec = format.name;
        capability.backend = "webrtc_internal";
        capability.canEncode = encoder;
        capability.canDecode = !encoder;
        capability.hardware = false;
        capability.maxSpatialLayers = encoder ? 3 : 1;
        capability.maxTemporalLayers = 1;
        capability.simulcast = encoder;
        capability.svc = false;
        capability.scalabilityModes = {"L1T1"};
        capability.notes = "WebRTC internal software codec fallback";
        capabilities.push_back(std::move(capability));
    }
    appendCapabilities(capabilities, out, seen);
}

std::vector<rtc::VideoCodecCapability> internalSoftwareCodecCapabilities()
{
    std::vector<rtc::VideoCodecCapability> capabilities;
    std::unordered_set<std::string> seen;
    NonH264WebRtcEncoderFactory encoderFactory;
    NonH264WebRtcDecoderFactory decoderFactory;
    appendInternalFactoryFormats(encoderFactory.GetSupportedFormats(), true, capabilities, seen);
    appendInternalFactoryFormats(decoderFactory.GetSupportedFormats(), false, capabilities, seen);
    return capabilities;
}

std::vector<rtc::VideoCodecCapability> detectLocalVideoCodecCapabilities()
{
    std::vector<rtc::VideoCodecCapability> capabilities;
    std::unordered_set<std::string> seen;
    const auto ffmpegCapabilities = ffmpeg::codecCapabilities();
    appendCapabilities(ffmpegCapabilities, capabilities, seen);
    if (ffmpegCapabilities.empty())
    {
        const char *diagnostics = ffmpeg::probeDiagnostics();
        LOG_WARN("No FFmpeg video codec capabilities detected; diagnostics={}",
                 diagnostics ? diagnostics : "none");
    }
    appendCapabilities(internalSoftwareCodecCapabilities(), capabilities, seen);
    return capabilities;
}
} // namespace

std::vector<webrtc::SdpVideoFormat> supportedH264Formats()
{
    constexpr webrtc::H264Level kLevels[] = {
        webrtc::H264Level::kLevel4,
        webrtc::H264Level::kLevel4_2,
        webrtc::H264Level::kLevel5_1,
        webrtc::H264Level::kLevel5_2,
        webrtc::H264Level::kLevel3_1,
    };

    std::vector<webrtc::SdpVideoFormat> formats;
    formats.reserve(sizeof(kLevels) / sizeof(kLevels[0]));
    for (const auto level : kLevels)
    {
        formats.push_back(webrtc::CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline,
                                                   level,
                                                   "1",
                                                   true));
    }
    return formats;
}

std::vector<std::unique_ptr<webrtc::VideoEncoderFactory>> createBuiltinVideoEncoderBackends()
{
    std::vector<std::unique_ptr<webrtc::VideoEncoderFactory>> factories;
    if (auto factory = ffmpeg::createEncoderFactory())
        factories.emplace_back(factory);
    factories.emplace_back(std::make_unique<NonH264WebRtcEncoderFactory>());
    return factories;
}

std::vector<std::unique_ptr<webrtc::VideoDecoderFactory>> createBuiltinVideoDecoderBackends()
{
    std::vector<std::unique_ptr<webrtc::VideoDecoderFactory>> factories;
    if (auto factory = ffmpeg::createDecoderFactory())
        factories.emplace_back(factory);
    factories.emplace_back(std::make_unique<NonH264WebRtcDecoderFactory>());
    return factories;
}

void warmLocalVideoCodecCapabilities()
{
    std::call_once(g_capabilities_once, []() {
        g_video_codec_capabilities = detectLocalVideoCodecCapabilities();
        LOG_DEBUG("local video codec capabilities cached: {}", g_video_codec_capabilities.size());
    });
}

std::vector<rtc::VideoCodecCapability> localVideoCodecCapabilities()
{
    warmLocalVideoCodecCapabilities();
    return g_video_codec_capabilities;
}

} // namespace airan::media
