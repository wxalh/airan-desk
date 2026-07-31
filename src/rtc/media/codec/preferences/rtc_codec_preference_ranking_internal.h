#ifndef AIRAN_RTC_CODEC_PREFERENCE_RANKING_INTERNAL_H
#define AIRAN_RTC_CODEC_PREFERENCE_RANKING_INTERNAL_H

#include "rtc/media/codec/preferences/rtc_codec_preference_ranking.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace rtc
{


inline std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}


inline std::string codecNameFromMime(const std::string &mimeType)
{
    std::string codec = lowerAscii(mimeType);
    const std::string prefix = "video/";
    if (codec.rfind(prefix, 0) == 0)
        codec = codec.substr(prefix.size());
    return codec;
}

int hardwareFirstVideoCodecRank(const webrtc::RtpCodecCapability &codec,
                                bool senderCapabilities,
                                int targetWidth = 0,
                                int targetHeight = 0,
                                int targetFps = 0);
bool localVideoCodecHasHardware(const std::string &codecName, bool senderCapabilities);
int remoteVideoCodecScore(const webrtc::RtpCodecCapability &codec,
                          bool senderCapabilities,
                          const std::vector<VideoCodecCapability> *remoteCapabilities,
                          int targetWidth = 0,
                          int targetHeight = 0,
                          int targetFps = 0);

} // namespace rtc

#endif /* AIRAN_RTC_CODEC_PREFERENCE_RANKING_INTERNAL_H */
