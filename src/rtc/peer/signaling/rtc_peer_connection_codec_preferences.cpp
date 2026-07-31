#include "rtc/core/rtc_internal.h"

#include "common/logger_manager.h"

#include <api/peer_connection_interface.h>

#include <utility>
#include <vector>
#include <algorithm>

namespace rtc
{


void PeerConnection::setRemoteVideoCodecCapabilities(std::vector<VideoCodecCapability> capabilities)
{
    m_remoteVideoCodecCapabilities = std::move(capabilities);
    LOG_INFO("Remote video codec runtime capabilities updated: count={}, acceptsSimulcast={}",
             m_remoteVideoCodecCapabilities.size(), remoteAcceptsVideoSimulcast());
}

bool PeerConnection::remoteAcceptsVideoSimulcast() const
{
    if (m_mediaTopology != MediaTopology::Sfu)
        return false;
    return std::any_of(m_remoteVideoCodecCapabilities.begin(),
                       m_remoteVideoCodecCapabilities.end(),
                       [](const VideoCodecCapability &capability) {
                           return capability.canDecode && capability.simulcast;
                       });
}


void PeerConnection::reapplyVideoCodecPreferences()
{
    if (!m_pc || !m_factory)
        return;
    if (m_signalingThread && !m_signalingThread->IsQuitting() && !m_signalingThread->IsCurrent())
    {
        m_signalingThread->BlockingCall([this]() {
            reapplyVideoCodecPreferences();
        });
        return;
    }

    const auto signalingState = m_pc->signaling_state();
    if (signalingState == webrtc::PeerConnectionInterface::kHaveLocalOffer ||
        signalingState == webrtc::PeerConnectionInterface::kClosed)
    {
        LOG_INFO("Skip reapplying video codec preferences while signaling state={}",
                 static_cast<int>(signalingState));
        return;
    }

    for (const auto &transceiver : m_pc->GetTransceivers())
    {
        if (!transceiver)
            continue;
#if AIRAN_WEBRTC_MILESTONE >= 144
        if (transceiver->media_type() != webrtc::MediaType::VIDEO)
#else
        if (transceiver->media_type() != cricket::MEDIA_TYPE_VIDEO)
#endif
            continue;

        const auto direction = transceiver->direction();
        const bool hasSend =
            direction == webrtc::RtpTransceiverDirection::kSendOnly ||
            direction == webrtc::RtpTransceiverDirection::kSendRecv;
        applyAiranVideoCodecPreferences(transceiver, m_factory, hasSend, &m_remoteVideoCodecCapabilities);
    }
}

} // namespace rtc
