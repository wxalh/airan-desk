#include "rtc/media/codec/preferences/rtc_codec_preference_ranking.h"
#include "rtc/media/codec/preferences/rtc_codec_preference_ranking_internal.h"

#include <algorithm>

namespace rtc
{
namespace
{
} // namespace

std::vector<webrtc::RtpCodecCapability> hardwareFirstVideoCodecs(
    const scoped_refptr<webrtc::PeerConnectionFactoryInterface> &factory,
    bool senderCapabilities,
    const std::vector<VideoCodecCapability> *remoteCapabilities,
    int targetWidth,
    int targetHeight,
    int targetFps)
{
    if (!factory)
        return {};

    auto capabilities = senderCapabilities
#if AIRAN_WEBRTC_MILESTONE >= 144
        ? factory->GetRtpSenderCapabilities(webrtc::MediaType::VIDEO)
        : factory->GetRtpReceiverCapabilities(webrtc::MediaType::VIDEO);
#else
        ? factory->GetRtpSenderCapabilities(cricket::MEDIA_TYPE_VIDEO)
        : factory->GetRtpReceiverCapabilities(cricket::MEDIA_TYPE_VIDEO);
#endif
    std::vector<webrtc::RtpCodecCapability> codecs = capabilities.codecs;
    std::stable_sort(codecs.begin(), codecs.end(), [senderCapabilities, remoteCapabilities, targetWidth, targetHeight, targetFps](const auto &left, const auto &right) {
        const int leftRemoteScore = remoteVideoCodecScore(left, senderCapabilities, remoteCapabilities, targetWidth, targetHeight, targetFps);
        const int rightRemoteScore = remoteVideoCodecScore(right, senderCapabilities, remoteCapabilities, targetWidth, targetHeight, targetFps);
        if (leftRemoteScore != rightRemoteScore)
            return leftRemoteScore > rightRemoteScore;
        return hardwareFirstVideoCodecRank(left, senderCapabilities, targetWidth, targetHeight, targetFps) <
               hardwareFirstVideoCodecRank(right, senderCapabilities, targetWidth, targetHeight, targetFps);
    });
    return codecs;
}
} // namespace rtc
