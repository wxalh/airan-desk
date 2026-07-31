#include "rtc/core/rtc_internal.h"

#include "media/codec/airan_video_codec_backend.h"
#include "media/codec/airan_video_bitrate_profile.h"
#include "common/logger_manager.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#if AIRAN_WEBRTC_MILESTONE < 144
#include <absl/types/optional.h>
#endif

namespace rtc
{
namespace
{
#if AIRAN_WEBRTC_MILESTONE >= 144
template <typename Optional>
void clearNativeOptional(Optional &value)
{
    value = std::nullopt;
}
#else
template <typename Optional>
void clearNativeOptional(Optional &value)
{
    value = absl::nullopt;
}
#endif

struct DesktopEncodingProfile
{
    const char *rid;
    double scaleDownBy;
    int minBitrateBps;
    int maxBitrateBps;
    int maxFps;
};

int normalizedDesktopFps(int fps)
{
    return (std::max)(1, fps);
}

bool hasValidDesktopSize(int width, int height)
{
    return width > 0 && height > 0;
}

int fullLayerMaxFps(int fps, const std::string &qualityProfile)
{
    const int normalizedFps = normalizedDesktopFps(fps);
    (void)qualityProfile;
    return normalizedFps;
}

airan::media::DesktopVideoBitrateLimits desktopLayerBitrateLimits(int width,
                                                                  int height,
                                                                  int fps,
                                                                  double scaleDownBy,
                                                                  const std::string &qualityProfile)
{
    const double safeScale = scaleDownBy > 0.0 ? scaleDownBy : 1.0;
    const int layerWidth = (std::max)(2, static_cast<int>(static_cast<double>((std::max)(2, width)) / safeScale));
    const int layerHeight = (std::max)(2, static_cast<int>(static_cast<double>((std::max)(2, height)) / safeScale));
    return airan::media::desktopVideoBitrateLimits(
        layerWidth,
        layerHeight,
        fps,
        airan::media::desktopVideoQualityProfileFromName(qualityProfile));
}

std::array<DesktopEncodingProfile, 3> desktopEncodingProfiles(int fps,
                                                              int width,
                                                              int height,
                                                              const std::string &qualityProfile)
{
    const int normalizedFps = normalizedDesktopFps(fps);
    const int fullFps = fullLayerMaxFps(normalizedFps, qualityProfile);
    const auto full = desktopLayerBitrateLimits(width, height, fullFps, 1.0, qualityProfile);
    const int halfFps = (std::min)(20, normalizedFps);
    const int quarterFps = (std::min)(10, normalizedFps);
    const auto half = desktopLayerBitrateLimits(width, height, halfFps, 2.0, "balanced");
    const auto quarter = desktopLayerBitrateLimits(width, height, quarterFps, 4.0, "balanced");
    return {{
        {"f", 1.0, full.min_bps, full.max_bps, fullFps},
        {"h", 2.0, half.min_bps, half.max_bps, halfFps},
        {"q", 4.0, quarter.min_bps, quarter.max_bps, quarterFps},
    }};
}

DesktopEncodingProfile defaultDesktopEncodingProfile(int fps,
                                                     int width,
                                                     int height,
                                                     const std::string &qualityProfile)
{
    return desktopEncodingProfiles(fps, width, height, qualityProfile)[0];
}

int desktopTransportMinBitrateBps(const DesktopEncodingProfile &profile)
{
    const int startupFloor = profile.scaleDownBy <= 1.0 ? 300000 : 150000;
    const int startupCeiling = profile.scaleDownBy <= 1.0 ? 800000 : 400000;
    return (std::min)(profile.minBitrateBps,
                      (std::max)(startupFloor, (std::min)(profile.maxBitrateBps / 12, startupCeiling)));
}

bool hasHardwareDesktopEncoder()
{
    const auto capabilities = airan::media::localVideoCodecCapabilities();
    return std::any_of(capabilities.begin(), capabilities.end(), [](const VideoCodecCapability &capability) {
        return capability.canEncode && capability.hardware;
    });
}

bool isAbove720p(int width, int height)
{
    return width > 1280 || height > 720;
}

bool shouldUseDesktopSimulcast(int width, int height, bool simulcastRequested)
{
    return simulcastRequested && hasValidDesktopSize(width, height) && isAbove720p(width, height) && hasHardwareDesktopEncoder();
}

DesktopEncodingProfile profileForRid(const std::string &rid,
                                     int fps,
                                     int width,
                                     int height,
                                     const std::string &qualityProfile,
                                     size_t fallbackIndex,
                                     size_t encodingCount)
{
    const auto profiles = desktopEncodingProfiles(fps, width, height, qualityProfile);
    for (const auto &profile : profiles)
    {
        if (rid == profile.rid)
            return profile;
    }

    if (encodingCount >= profiles.size())
        return profiles[(std::min)(fallbackIndex, profiles.size() - 1)];
    return defaultDesktopEncodingProfile(fps, width, height, qualityProfile);
}

void applyDesktopEncodingProfile(webrtc::RtpEncodingParameters &encoding,
                                 const DesktopEncodingProfile &profile,
                                 bool applySenderScopedPriority)
{
    encoding.active = true;
    if (applySenderScopedPriority)
    {
        encoding.network_priority = webrtc::Priority::kHigh;
        encoding.bitrate_priority = 2.0;
    }
    else
    {
        encoding.network_priority = webrtc::Priority::kLow;
        encoding.bitrate_priority = webrtc::kDefaultBitratePriority;
    }
    encoding.scale_resolution_down_by = profile.scaleDownBy;
    encoding.max_framerate = static_cast<double>(profile.maxFps);
    encoding.min_bitrate_bps = desktopTransportMinBitrateBps(profile);
    encoding.max_bitrate_bps = profile.maxBitrateBps;
    clearNativeOptional(encoding.num_temporal_layers);
    clearNativeOptional(encoding.scalability_mode);
}

void applyDesktopEncodingFpsOnly(webrtc::RtpEncodingParameters &encoding,
                                 int fps,
                                 bool applySenderScopedPriority)
{
    encoding.active = true;
    if (applySenderScopedPriority)
    {
        encoding.network_priority = webrtc::Priority::kHigh;
        encoding.bitrate_priority = 2.0;
    }
    else
    {
        encoding.network_priority = webrtc::Priority::kLow;
        encoding.bitrate_priority = webrtc::kDefaultBitratePriority;
    }
    clearNativeOptional(encoding.scale_resolution_down_by);
    encoding.max_framerate = static_cast<double>(normalizedDesktopFps(fps));
    clearNativeOptional(encoding.min_bitrate_bps);
    clearNativeOptional(encoding.max_bitrate_bps);
    clearNativeOptional(encoding.num_temporal_layers);
    clearNativeOptional(encoding.scalability_mode);
}

std::string describeDesktopEncodings(const std::vector<webrtc::RtpEncodingParameters> &encodings)
{
    std::ostringstream out;
    for (size_t index = 0; index < encodings.size(); ++index)
    {
        const auto &encoding = encodings[index];
        if (index > 0)
            out << "; ";
        out << "#" << index
            << "{rid=" << (encoding.rid.empty() ? "-" : encoding.rid)
            << ", active=" << (encoding.active ? "true" : "false")
            << ", scale=";
        if (encoding.scale_resolution_down_by)
            out << *encoding.scale_resolution_down_by;
        else
            out << "-";
        out << ", fps=";
        if (encoding.max_framerate)
            out << *encoding.max_framerate;
        else
            out << "-";
        out << ", min=";
        if (encoding.min_bitrate_bps)
            out << *encoding.min_bitrate_bps;
        else
            out << "-";
        out << ", max=";
        if (encoding.max_bitrate_bps)
            out << *encoding.max_bitrate_bps;
        else
            out << "-";
        out << ", scalability=";
        if (encoding.scalability_mode)
            out << *encoding.scalability_mode;
        else
            out << "-";
        out << "}";
    }
    return out.str();
}
} // namespace

std::vector<webrtc::RtpEncodingParameters> createDesktopVideoSendEncodings(int fps,
                                                                           int width,
                                                                           int height,
                                                                           bool simulcastRequested,
                                                                           const std::string &qualityProfile)
{
    const std::string normalizedQualityProfile =
        airan::media::normalizeDesktopVideoQualityProfile(qualityProfile);
    std::vector<webrtc::RtpEncodingParameters> encodings;
    if (!hasValidDesktopSize(width, height))
    {
        webrtc::RtpEncodingParameters encoding;
        applyDesktopEncodingFpsOnly(encoding, fps, true);
        encodings.push_back(std::move(encoding));
        LOG_WARN("Desktop video initial encodings deferred resolution tuning because target size is invalid: target={}x{}, fps={}, qualityProfile={}",
                 width, height, normalizedDesktopFps(fps), normalizedQualityProfile);
        return encodings;
    }

    if (shouldUseDesktopSimulcast(width, height, simulcastRequested))
    {
        for (const auto &profile : desktopEncodingProfiles(fps, width, height, normalizedQualityProfile))
        {
            webrtc::RtpEncodingParameters encoding;
            encoding.rid = profile.rid;
            applyDesktopEncodingProfile(encoding, profile, std::string(profile.rid) == "f");
            encodings.push_back(std::move(encoding));
        }
        LOG_DEBUG("Desktop video initial encodings request simulcast: target={}x{}, fps={}, qualityProfile={}, encodings=[{}]",
                  width, height, normalizedDesktopFps(fps), normalizedQualityProfile, describeDesktopEncodings(encodings));
        return encodings;
    }

    webrtc::RtpEncodingParameters encoding;
    applyDesktopEncodingProfile(encoding, defaultDesktopEncodingProfile(fps, width, height, normalizedQualityProfile), true);
    encodings.push_back(std::move(encoding));
    LOG_DEBUG("Desktop video initial encodings request single stream: target={}x{}, fps={}, qualityProfile={}, simulcastRequested={}, hardwareEncoder={}",
              width, height, normalizedDesktopFps(fps), normalizedQualityProfile, simulcastRequested, hasHardwareDesktopEncoder());
    return encodings;
}

bool setDesktopSenderParameters(const scoped_refptr<webrtc::RtpSenderInterface> &sender,
                                webrtc::RtpParameters parameters,
                                const char *label)
{
    const auto error = sender->SetParameters(parameters);
    if (error.ok())
    {
        const auto applied = sender->GetParameters();
        LOG_DEBUG("Desktop video sender tuned with profile: {}, encodings=[{}]",
                  label, describeDesktopEncodings(applied.encodings));
        return true;
    }

    LOG_WARN("Desktop video sender tuning profile '{}' failed: {}", label, error.message());
    return false;
}

void applyDesktopSenderProfile(webrtc::RtpParameters &parameters,
                               int fps,
                               int width,
                               int height,
                               bool simulcastRequested,
                               const std::string &qualityProfile,
                               bool applySenderScopedPriority,
                               bool applyBitrateLimits)
{
    const std::string normalizedQualityProfile =
        airan::media::normalizeDesktopVideoQualityProfile(qualityProfile);
    const bool requestSimulcast = shouldUseDesktopSimulcast(width, height, simulcastRequested);
    if (parameters.encodings.empty())
        return;

    for (size_t index = 0; index < parameters.encodings.size(); ++index)
    {
        auto &encoding = parameters.encodings[index];
        if (!hasValidDesktopSize(width, height))
        {
            applyDesktopEncodingFpsOnly(encoding, fps, applySenderScopedPriority && index == 0);
            continue;
        }

        if (requestSimulcast && parameters.encodings.size() >= desktopEncodingProfiles(fps, width, height, normalizedQualityProfile).size())
        {
            const auto profile = profileForRid(encoding.rid, fps, width, height, normalizedQualityProfile, index, parameters.encodings.size());
            applyDesktopEncodingProfile(encoding,
                                        profile,
                                        applySenderScopedPriority &&
                                            (encoding.rid == "f" || (encoding.rid.empty() && index == 0)));
        }
        else
        {
            applyDesktopEncodingProfile(encoding,
                                        defaultDesktopEncodingProfile(fps, width, height, normalizedQualityProfile),
                                        applySenderScopedPriority && index == 0);
        }

        if (!applyBitrateLimits)
        {
            clearNativeOptional(encoding.min_bitrate_bps);
            clearNativeOptional(encoding.max_bitrate_bps);
        }
    }
}

void configureDesktopVideoSender(const scoped_refptr<webrtc::RtpSenderInterface> &sender,
                                 int fps,
                                 int width,
                                 int height,
                                 bool simulcastRequested,
                                 const std::string &qualityProfile)
{
    if (!sender)
        return;

    const std::string normalizedQualityProfile =
        airan::media::normalizeDesktopVideoQualityProfile(qualityProfile);
    auto parameters = sender->GetParameters();
    parameters.degradation_preference = webrtc::DegradationPreference::MAINTAIN_RESOLUTION;
    const bool requestSimulcast = shouldUseDesktopSimulcast(width, height, simulcastRequested);
    applyDesktopSenderProfile(parameters, fps, width, height, simulcastRequested, normalizedQualityProfile, true, true);

    const bool simulcast = parameters.encodings.size() > 1;
    if (setDesktopSenderParameters(sender, parameters, simulcast ? "desktop-sfu-simulcast" : "desktop-default-single-stream"))
    {
        LOG_DEBUG("Desktop video sender active: target={}x{}, fps={}, qualityProfile={}, encodings={}, mode={}, degradation=maintain-resolution",
                  width,
                  height,
                  normalizedDesktopFps(fps),
                  normalizedQualityProfile,
                  parameters.encodings.size(),
                  simulcast ? "webrtc-sfu-simulcast-rid" :
                              (requestSimulcast ? "webrtc-negotiated-single-stream" : "webrtc-default-single-stream"));
        return;
    }

    auto noPriority = sender->GetParameters();
    noPriority.degradation_preference = webrtc::DegradationPreference::MAINTAIN_RESOLUTION;
    applyDesktopSenderProfile(noPriority, fps, width, height, simulcastRequested, normalizedQualityProfile, false, true);
    if (setDesktopSenderParameters(sender, noPriority, "desktop-default-no-priority"))
        return;

    auto noSvc = sender->GetParameters();
    noSvc.degradation_preference = webrtc::DegradationPreference::MAINTAIN_RESOLUTION;
    applyDesktopSenderProfile(noSvc, fps, width, height, simulcastRequested, normalizedQualityProfile, false, true);
    if (setDesktopSenderParameters(sender, noSvc, "desktop-default-no-scalability-mode"))
        return;

    auto conservative = sender->GetParameters();
    conservative.degradation_preference = webrtc::DegradationPreference::MAINTAIN_RESOLUTION;
    applyDesktopSenderProfile(conservative, fps, width, height, false, normalizedQualityProfile, false, false);
    if (!setDesktopSenderParameters(sender, conservative, "desktop-default-framerate-only"))
        LOG_WARN("Desktop video sender kept WebRTC defaults after all tuning profiles failed");
}
} // namespace rtc
