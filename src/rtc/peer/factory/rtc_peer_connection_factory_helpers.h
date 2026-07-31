#ifndef AIRAN_RTC_PEER_CONNECTION_FACTORY_HELPERS_H
#define AIRAN_RTC_PEER_CONNECTION_FACTORY_HELPERS_H

#include "rtc/core/rtc.hpp"

#include <api/video_codecs/sdp_video_format.h>

#include <optional>
#include <string>
#include <vector>

namespace rtc::factory_internal
{

#if AIRAN_WEBRTC_MILESTONE >= 144
inline constexpr auto kNoScalabilityMode = std::nullopt;
#else
inline const absl::optional<std::string> kNoScalabilityMode;
#endif


int videoCodecRank(const std::string &name, bool powerEfficient);


bool containsFormat(const std::vector<webrtc::SdpVideoFormat> &formats, const webrtc::SdpVideoFormat &format);


template <typename Factory, typename Query>
bool isPowerEfficient(const Factory &factory, const webrtc::SdpVideoFormat &format, Query query)
{
    if (!factory)
        return false;
    return query(*factory, format).is_power_efficient;
}

} // namespace rtc::factory_internal

#endif /* AIRAN_RTC_PEER_CONNECTION_FACTORY_HELPERS_H */
