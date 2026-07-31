#ifndef WEBRTC_CTL_PEER_STATE_TEXT_H
#define WEBRTC_CTL_PEER_STATE_TEXT_H

#include "rtc/core/rtc.hpp"

#include <QString>

class QObject;

namespace webrtc_ctl_peer_state
{

QString peerConnectionStateText(rtc::PeerConnection::State state, QObject *context);


QString iceStateText(rtc::PeerConnection::IceState state, QObject *context);


QString gatheringStateText(rtc::PeerConnection::GatheringState state, QObject *context);
} /* namespace webrtc_ctl_peer_state */

#endif /* WEBRTC_CTL_PEER_STATE_TEXT_H */
