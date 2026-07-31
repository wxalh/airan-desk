#include "rtc/peer/factory/rtc_peer_connection_factory_helpers.h"

#include <algorithm>
#include <cctype>

namespace rtc::factory_internal
{

int videoCodecRank(const std::string &name, bool powerEfficient)
{
    std::string codec = name;
    std::transform(codec.begin(), codec.end(), codec.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (powerEfficient)
    {
        if (codec == "vp9")
            return 0;
        if (codec == "vp8")
            return 10;
        if (codec == "h264")
            return 20;
        if (codec == "h265" || codec == "hevc")
            return 30;
        if (codec == "av1")
            return 40;
        return 100;
    }
    if (codec == "vp8")
        return 0;
    if (codec == "vp9")
        return 10;
    if (codec == "h264")
        return 20;
    if (codec == "h265" || codec == "hevc")
        return 30;
    if (codec == "av1")
        return 40;
    return 100;
}

bool containsFormat(const std::vector<webrtc::SdpVideoFormat> &formats, const webrtc::SdpVideoFormat &format)
{
    return std::any_of(formats.begin(), formats.end(), [&format](const auto &existing) {
        return existing.IsSameCodec(format);
    });
}

} // namespace rtc::factory_internal
