#include "media/codec/webrtc/airan_codec_adapter.h"

#include "media/codec/airan_video_codec_backend.h"
#include "common/logger_manager.h"
#include "rtc/peer/factory/rtc_peer_connection_factory_helpers.h"

#include <api/video_codecs/sdp_video_format.h>
#if AIRAN_WEBRTC_MILESTONE >= 144
#include <api/environment/environment.h>
#include <api/video_codecs/scalability_mode_helper.h>
#include <media/engine/simulcast_encoder_adapter.h>
#endif
#include <modules/video_coding/codecs/h264/include/h264.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace airan::media
{
namespace
{

std::string lowerAscii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool isPowerEfficient(const webrtc::SdpVideoFormat &format, bool encoder);

webrtc::SdpVideoFormat sdpFormatForCapability(const rtc::VideoCodecCapability &capability)
{
    const std::string codec = lowerAscii(capability.codec);
    if (codec == "h264")
    {
        return webrtc::CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline,
                                        webrtc::H264Level::kLevel3_1,
                                        "1",
                                        true);
    }
    if (codec == "vp8")
        return webrtc::SdpVideoFormat("VP8");
    if (codec == "vp9")
        return webrtc::SdpVideoFormat("VP9");
    if (codec == "av1")
        return webrtc::SdpVideoFormat("AV1");
    return webrtc::SdpVideoFormat(capability.codec);
}

std::vector<webrtc::SdpVideoFormat> formatsFromCapabilities(bool encoder)
{
    std::vector<webrtc::SdpVideoFormat> formats;
    for (const auto &capability : localVideoCodecCapabilities())
    {
        if ((encoder && !capability.canEncode) || (!encoder && !capability.canDecode))
            continue;
        const auto format = sdpFormatForCapability(capability);
        if (!rtc::factory_internal::containsFormat(formats, format))
            formats.push_back(format);
    }
    std::stable_sort(formats.begin(), formats.end(), [encoder](const auto &left, const auto &right) {
        const bool left_power = isPowerEfficient(left, encoder);
        const bool right_power = isPowerEfficient(right, encoder);
        if (left_power != right_power)
            return left_power;
        return rtc::factory_internal::videoCodecRank(left.name, left_power) <
               rtc::factory_internal::videoCodecRank(right.name, right_power);
    });
    return formats;
}

bool capabilityMatchesFormat(const rtc::VideoCodecCapability &capability,
                             const webrtc::SdpVideoFormat &format,
                             bool encoder)
{
    if ((encoder && !capability.canEncode) || (!encoder && !capability.canDecode))
        return false;
    return sdpFormatForCapability(capability).IsSameCodec(format);
}

bool isPowerEfficient(const webrtc::SdpVideoFormat &format, bool encoder)
{
    for (const auto &capability : localVideoCodecCapabilities())
    {
        if (capability.hardware && capabilityMatchesFormat(capability, format, encoder))
            return true;
    }
    return false;
}

bool isSupported(const webrtc::SdpVideoFormat &format, bool encoder)
{
    for (const auto &capability : localVideoCodecCapabilities())
    {
        if (capabilityMatchesFormat(capability, format, encoder))
            return true;
    }
    return false;
}

#if AIRAN_WEBRTC_MILESTONE >= 144
bool airanAdapterScalabilityModeSupported(const std::optional<std::string> &scalabilityMode)
{
    if (!scalabilityMode)
        return true;
    const auto parsed = webrtc::ScalabilityModeStringToEnum(*scalabilityMode);
    if (!parsed)
        return false;

    // The bottom Airan FFmpeg encoder is intentionally L1T1-only. WebRTC's
    // SimulcastEncoderAdapter may compose multiple L1T1 encoder instances for
    // simulcast, but Airan must not advertise temporal or SVC modes that the
    // bottom encoder cannot produce with compatible metadata.
    return *parsed == webrtc::ScalabilityMode::kL1T1;
}
#endif

class AiranVideoEncoderBackendFactory final : public webrtc::VideoEncoderFactory
{
public:
    AiranVideoEncoderBackendFactory()
        : m_backends(createBuiltinVideoEncoderBackends())
    {
    }

    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override
    {
        std::vector<webrtc::SdpVideoFormat> formats;
        for (const auto &backend : m_backends)
        {
            if (!backend)
                continue;
            for (const auto &format : backend->GetSupportedFormats())
            {
                if (!rtc::factory_internal::containsFormat(formats, format))
                    formats.push_back(format);
            }
        }
        sortFormats(formats);
        return formats;
    }

    std::vector<webrtc::SdpVideoFormat> GetImplementations() const override
    {
        std::vector<webrtc::SdpVideoFormat> formats;
        for (const auto &backend : m_backends)
        {
            if (!backend)
                continue;
            for (const auto &format : backend->GetImplementations())
            {
                if (!rtc::factory_internal::containsFormat(formats, format))
                    formats.push_back(format);
            }
        }
        sortFormats(formats);
        return formats;
    }

    CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat &format,
#if AIRAN_WEBRTC_MILESTONE >= 144
                                   std::optional<std::string> scalability_mode) const override
#else
                                   absl::optional<std::string> scalability_mode) const override
#endif
    {
        CodecSupport support;
#if AIRAN_WEBRTC_MILESTONE >= 144
        if (!airanAdapterScalabilityModeSupported(scalability_mode))
            return support;
#endif

        for (const auto &backend : m_backends)
        {
            if (!backend)
                continue;
            const auto backend_support = backend->QueryCodecSupport(format, scalability_mode);
            support.is_supported = support.is_supported || backend_support.is_supported;
            support.is_power_efficient = support.is_power_efficient || backend_support.is_power_efficient;
        }
        return support;
    }

#if AIRAN_WEBRTC_MILESTONE >= 144
    std::unique_ptr<webrtc::VideoEncoder> Create(const webrtc::Environment &env,
                                                 const webrtc::SdpVideoFormat &format) override
#else
    std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat &format) override
#endif
    {
        for (auto *backend : orderedBackends(format))
        {
#if AIRAN_WEBRTC_MILESTONE >= 144
            auto encoder = backend->Create(env, format);
#else
            auto encoder = backend->CreateVideoEncoder(format);
#endif
            if (encoder)
            {
                const auto support = backend->QueryCodecSupport(format, rtc::factory_internal::kNoScalabilityMode);
                LOG_INFO("Airan Codec Adapter created {} encoder: powerEfficient={}",
                         format.name,
                         support.is_power_efficient);
                return encoder;
            }
            LOG_WARN("Airan encoder backend returned null: {}", format.ToString());
        }
        LOG_ERROR("Airan Codec Adapter could not create encoder for {}", format.ToString());
        return nullptr;
    }

private:
    std::vector<webrtc::VideoEncoderFactory *> orderedBackends(const webrtc::SdpVideoFormat &format) const
    {
        std::vector<webrtc::VideoEncoderFactory *> backends;
        for (const auto &backend : m_backends)
        {
            if (!backend)
                continue;
            const auto support = backend->QueryCodecSupport(format, rtc::factory_internal::kNoScalabilityMode);
            if (support.is_supported)
                backends.push_back(backend.get());
        }
        std::stable_sort(backends.begin(), backends.end(), [&format](const auto *left, const auto *right) {
            const bool left_power =
                left->QueryCodecSupport(format, rtc::factory_internal::kNoScalabilityMode).is_power_efficient;
            const bool right_power =
                right->QueryCodecSupport(format, rtc::factory_internal::kNoScalabilityMode).is_power_efficient;
            return left_power && !right_power;
        });
        return backends;
    }

    void sortFormats(std::vector<webrtc::SdpVideoFormat> &formats) const
    {
        std::stable_sort(formats.begin(), formats.end(), [this](const auto &left, const auto &right) {
            const auto left_support = QueryCodecSupport(left, rtc::factory_internal::kNoScalabilityMode);
            const auto right_support = QueryCodecSupport(right, rtc::factory_internal::kNoScalabilityMode);
            if (left_support.is_power_efficient != right_support.is_power_efficient)
                return left_support.is_power_efficient;
            return rtc::factory_internal::videoCodecRank(left.name, left_support.is_power_efficient) <
                   rtc::factory_internal::videoCodecRank(right.name, right_support.is_power_efficient);
        });
    }

    std::vector<std::unique_ptr<webrtc::VideoEncoderFactory>> m_backends;
};

class AiranVideoEncoderFactory final : public webrtc::VideoEncoderFactory
{
public:
    AiranVideoEncoderFactory()
        : m_backendFactory(std::make_unique<AiranVideoEncoderBackendFactory>())
    {
    }

    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override
    {
        return m_backendFactory->GetSupportedFormats();
    }

    std::vector<webrtc::SdpVideoFormat> GetImplementations() const override
    {
        return m_backendFactory->GetImplementations();
    }

    CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat &format,
#if AIRAN_WEBRTC_MILESTONE >= 144
                                   std::optional<std::string> scalability_mode) const override
#else
                                   absl::optional<std::string> scalability_mode) const override
#endif
    {
        if (!format.IsCodecInList(GetSupportedFormats()))
            return {};
#if AIRAN_WEBRTC_MILESTONE >= 144
        if (!airanAdapterScalabilityModeSupported(scalability_mode))
            return {};
#endif
        return m_backendFactory->QueryCodecSupport(format, scalability_mode);
    }

#if AIRAN_WEBRTC_MILESTONE >= 144
    std::unique_ptr<webrtc::VideoEncoder> Create(const webrtc::Environment &env,
                                                 const webrtc::SdpVideoFormat &format) override
    {
        const auto support = m_backendFactory->QueryCodecSupport(format, rtc::factory_internal::kNoScalabilityMode);
        if (!support.is_supported)
            return nullptr;

        LOG_INFO("Airan Codec Adapter created {} encoder through WebRTC m144 simulcast adapter: powerEfficient={}",
                 format.name,
                 support.is_power_efficient);
        return std::make_unique<webrtc::SimulcastEncoderAdapter>(env,
                                                                 m_backendFactory.get(),
                                                                 nullptr,
                                                                 format);
    }
#else
    std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat &format) override
    {
        return m_backendFactory->CreateVideoEncoder(format);
    }
#endif

private:
    std::unique_ptr<webrtc::VideoEncoderFactory> m_backendFactory;
};

class AiranVideoDecoderFactory final : public webrtc::VideoDecoderFactory
{
public:
    AiranVideoDecoderFactory()
        : m_backends(createBuiltinVideoDecoderBackends())
    {
    }

    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override
    {
        std::vector<webrtc::SdpVideoFormat> formats;
        for (const auto &backend : m_backends)
        {
            if (!backend)
                continue;
            for (const auto &format : backend->GetSupportedFormats())
            {
                if (!rtc::factory_internal::containsFormat(formats, format))
                    formats.push_back(format);
            }
        }
        sortFormats(formats);
        return formats;
    }

    CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat &format, bool reference_scaling) const override
    {
        CodecSupport support;
        if (reference_scaling)
            return support;

        for (const auto &backend : m_backends)
        {
            if (!backend)
                continue;
            const auto backend_support = backend->QueryCodecSupport(format, reference_scaling);
            support.is_supported = support.is_supported || backend_support.is_supported;
            support.is_power_efficient = support.is_power_efficient || backend_support.is_power_efficient;
        }
        return support;
    }

#if AIRAN_WEBRTC_MILESTONE >= 144
    std::unique_ptr<webrtc::VideoDecoder> Create(const webrtc::Environment &env,
                                                 const webrtc::SdpVideoFormat &format) override
#else
    std::unique_ptr<webrtc::VideoDecoder> CreateVideoDecoder(const webrtc::SdpVideoFormat &format) override
#endif
    {
        for (auto *backend : orderedBackends(format))
        {
#if AIRAN_WEBRTC_MILESTONE >= 144
            auto decoder = backend->Create(env, format);
#else
            auto decoder = backend->CreateVideoDecoder(format);
#endif
            if (decoder)
            {
                const auto support = backend->QueryCodecSupport(format, false);
                LOG_INFO("Airan Codec Adapter created {} decoder: powerEfficient={}",
                         format.name,
                         support.is_power_efficient);
                return decoder;
            }
            LOG_WARN("Airan decoder backend returned null: {}", format.ToString());
        }
        LOG_ERROR("Airan Codec Adapter could not create decoder for {}", format.ToString());
        return nullptr;
    }

private:
    std::vector<webrtc::VideoDecoderFactory *> orderedBackends(const webrtc::SdpVideoFormat &format) const
    {
        std::vector<webrtc::VideoDecoderFactory *> backends;
        for (const auto &backend : m_backends)
        {
            if (!backend)
                continue;
            const auto support = backend->QueryCodecSupport(format, false);
            if (support.is_supported)
                backends.push_back(backend.get());
        }
        std::stable_sort(backends.begin(), backends.end(), [&format](const auto *left, const auto *right) {
            const bool left_power = left->QueryCodecSupport(format, false).is_power_efficient;
            const bool right_power = right->QueryCodecSupport(format, false).is_power_efficient;
            return left_power && !right_power;
        });
        return backends;
    }

    void sortFormats(std::vector<webrtc::SdpVideoFormat> &formats) const
    {
        std::stable_sort(formats.begin(), formats.end(), [this](const auto &left, const auto &right) {
            const auto left_support = QueryCodecSupport(left, false);
            const auto right_support = QueryCodecSupport(right, false);
            if (left_support.is_power_efficient != right_support.is_power_efficient)
                return left_support.is_power_efficient;
            return rtc::factory_internal::videoCodecRank(left.name, left_support.is_power_efficient) <
                   rtc::factory_internal::videoCodecRank(right.name, right_support.is_power_efficient);
        });
    }

    std::vector<std::unique_ptr<webrtc::VideoDecoderFactory>> m_backends;
};

} // namespace

std::unique_ptr<webrtc::VideoEncoderFactory> createAiranVideoEncoderFactory()
{
    return std::make_unique<AiranVideoEncoderFactory>();
}

std::unique_ptr<webrtc::VideoDecoderFactory> createAiranVideoDecoderFactory()
{
    return std::make_unique<AiranVideoDecoderFactory>();
}

} // namespace airan::media
