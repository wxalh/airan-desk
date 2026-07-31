#include "media/codec/backends/ffmpeg/codec/ffmpeg_codec_probe.h"

#include "media/codec/airan_video_codec_backend.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>

#if defined(AIRAN_HAVE_FFMPEG)
#define AIRAN_FFMPEG_RUNTIME_ENABLED 1
#endif

namespace airan::media::ffmpeg
{
namespace
{
struct CachedCapability
{
    const CodecProbe *probe = nullptr;
    bool encoderRuntime = false;
    bool decoderRuntime = false;
};

bool isHardwareProbe(const CodecProbe &probe)
{
#if defined(AIRAN_FFMPEG_RUNTIME_ENABLED)
    const bool platform_hardware = probe.backend &&
                                   (std::strstr(probe.backend, "_mf") ||
                                    std::strstr(probe.backend, "_v4l2m2m") ||
                                    std::strstr(probe.backend, "_rkmpp"));
    return probe.deviceType != AV_HWDEVICE_TYPE_NONE ||
           probe.hardwarePixelFormat != AV_PIX_FMT_NONE ||
           platform_hardware ||
           (probe.zeroCopy && probe.zeroCopy[0] != '\0');
#else
    (void)probe;
    return false;
#endif
}

std::vector<CachedCapability> &capabilityCache()
{
    static std::vector<CachedCapability> cache;
    return cache;
}

std::string &probeDiagnosticText()
{
    static std::string text;
    return text;
}

void probeCapabilities()
{
    static std::once_flag flag;
    std::call_once(flag, []() {
        auto &cache = capabilityCache();
        cache.clear();
#if defined(AIRAN_FFMPEG_RUNTIME_ENABLED)
        if (!loaded())
        {
            probeDiagnosticText() = diagnostics();
            return;
        }

        size_t probe_count = 0;
        const CodecProbe *probes = codecProbes(&probe_count);
        for (size_t i = 0; probes && i < probe_count; ++i)
        {
            const auto &probe = probes[i];
            if (!probeRuntimeDependenciesAvailable(probe))
                continue;

            const bool encoder_runtime = probe.encoder && openEncoderProbe(probe);
            const bool decoder_runtime = probe.decoder && openDecoderProbe(probe);
            if (encoder_runtime || decoder_runtime)
                cache.push_back({&probe, encoder_runtime, decoder_runtime});
        }
#else
        probeDiagnosticText() = "FFmpeg runtime disabled";
#endif
        if (cache.empty() && probeDiagnosticText().empty())
            probeDiagnosticText() = "FFmpeg runtime loaded, but every codec open probe failed";
    });
}
} // namespace

std::vector<const CodecProbe *> selectProbes(CodecKind codec, bool encoder)
{
    probeCapabilities();
    std::vector<const CodecProbe *> probes;
    const auto &cache = capabilityCache();
    for (const auto &entry : cache)
    {
        if (!entry.probe || entry.probe->codec != codec)
            continue;
        if ((encoder && entry.encoderRuntime) || (!encoder && entry.decoderRuntime))
            probes.push_back(entry.probe);
    }
    if (!encoder)
    {
        std::stable_sort(probes.begin(), probes.end(), [](const CodecProbe *lhs, const CodecProbe *rhs) {
            const bool lhs_hardware = lhs && isHardwareProbe(*lhs);
            const bool rhs_hardware = rhs && isHardwareProbe(*rhs);
            return lhs_hardware && !rhs_hardware;
        });
    }
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10) && defined(AIRAN_FFMPEG_RUNTIME_ENABLED)
    if (!encoder)
    {
        std::stable_sort(probes.begin(), probes.end(), [](const CodecProbe *lhs, const CodecProbe *rhs) {
            const bool lhs_d3d11 = lhs && lhs->hardwarePixelFormat == AV_PIX_FMT_D3D11;
            const bool rhs_d3d11 = rhs && rhs->hardwarePixelFormat == AV_PIX_FMT_D3D11;
            return lhs_d3d11 && !rhs_d3d11;
        });
    }
#endif
    return probes;
}

const CodecProbe *selectProbe(CodecKind codec, bool encoder)
{
    const auto probes = selectProbes(codec, encoder);
    return probes.empty() ? nullptr : probes.front();
}

bool codecAvailable(CodecKind codec, bool encoder)
{
    if (codec == CodecKind::Unknown)
        return false;
    return selectProbe(codec, encoder) != nullptr;
}

bool codecPowerEfficient(CodecKind codec, bool encoder)
{
    if (codec == CodecKind::Unknown)
        return false;
    const auto probes = selectProbes(codec, encoder);
    return std::any_of(probes.begin(), probes.end(), [](const CodecProbe *probe) {
        return probe && isHardwareProbe(*probe);
    });
}

bool encoderAvailable()
{
    probeCapabilities();
    const auto &cache = capabilityCache();
    return std::any_of(cache.begin(), cache.end(), [](const auto &entry) {
        return entry.encoderRuntime;
    });
}

bool decoderAvailable()
{
    probeCapabilities();
    const auto &cache = capabilityCache();
    return std::any_of(cache.begin(), cache.end(), [](const auto &entry) {
        return entry.decoderRuntime;
    });
}

std::vector<rtc::VideoCodecCapability> codecCapabilities()
{
    probeCapabilities();
    std::vector<rtc::VideoCodecCapability> capabilities;
    for (const auto &entry : capabilityCache())
    {
        if (!entry.probe)
            continue;
        rtc::VideoCodecCapability capability;
        capability.codec = codecName(entry.probe->codec);
        capability.backend = entry.probe->backend ? entry.probe->backend : "";
        capability.canEncode = entry.encoderRuntime;
        capability.canDecode = entry.decoderRuntime;
        capability.hardware = isHardwareProbe(*entry.probe);
        capability.zeroCopyPath = entry.probe->zeroCopy ? entry.probe->zeroCopy : "";
        capability.maxSpatialLayers = 1;
        capability.maxTemporalLayers = 1;
        capability.simulcast = false;
        capability.svc = false;
        capability.scalabilityModes = {"L1T1"};
        capability.notes = "ffmpeg runtime open succeeded; bottom encoder supports L1T1, WebRTC SimulcastEncoderAdapter composes multiple Airan instances for simulcast";
        capabilities.push_back(std::move(capability));
    }
    return capabilities;
}

std::vector<webrtc::SdpVideoFormat> supportedFormats(bool encoder)
{
    std::vector<webrtc::SdpVideoFormat> formats;
    if (codecAvailable(CodecKind::H264, encoder))
    {
        for (const auto &format : airan::media::supportedH264Formats())
            formats.push_back(format);
    }
    if (codecAvailable(CodecKind::VP8, encoder))
        formats.emplace_back("VP8");
    if (codecAvailable(CodecKind::VP9, encoder))
        formats.emplace_back("VP9");
    if (codecAvailable(CodecKind::AV1, encoder))
        formats.emplace_back("AV1");
    return formats;
}

const char *probeDiagnostics()
{
    probeCapabilities();
    if (!probeDiagnosticText().empty())
        return probeDiagnosticText().c_str();
#if defined(AIRAN_FFMPEG_RUNTIME_ENABLED)
    return diagnostics();
#else
    return "FFmpeg runtime disabled";
#endif
}

} // namespace airan::media::ffmpeg
