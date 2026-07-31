#ifndef AIRAN_RTC_CODEC_PREFERENCE_RANKING_H
#define AIRAN_RTC_CODEC_PREFERENCE_RANKING_H

#include "rtc/core/rtc_internal.h"

#include <vector>

namespace rtc
{

std::vector<webrtc::RtpCodecCapability> hardwareFirstVideoCodecs(
    const scoped_refptr<webrtc::PeerConnectionFactoryInterface> &factory,
    bool senderCapabilities,
    const std::vector<VideoCodecCapability> *remoteCapabilities,
    int targetWidth = 0,
    int targetHeight = 0,
    int targetFps = 0);
}

#endif /* AIRAN_RTC_CODEC_PREFERENCE_RANKING_H */
