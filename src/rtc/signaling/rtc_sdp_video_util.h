#ifndef AIRAN_RTC_SDP_VIDEO_UTIL_H
#define AIRAN_RTC_SDP_VIDEO_UTIL_H

#include <string>

namespace rtc
{

void logSdpVideoCodecs(const char *label, const std::string &type, const std::string &sdp);
std::string normalizeVideoCodec(std::string mimeType);

} // namespace rtc

#endif // AIRAN_RTC_SDP_VIDEO_UTIL_H
