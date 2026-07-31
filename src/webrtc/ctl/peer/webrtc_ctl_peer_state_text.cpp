#include "webrtc/ctl/peer/webrtc_ctl_peer_state_text.h"

#include <QCoreApplication>

namespace webrtc_ctl_peer_state
{
QString peerConnectionStateText(rtc::PeerConnection::State state, QObject *context)
{
    Q_UNUSED(context);
    switch (state)
    {
    case rtc::PeerConnection::State::Connected:
        return QCoreApplication::translate("WebRtcCtl", "Connected");
    case rtc::PeerConnection::State::Connecting:
        return QCoreApplication::translate("WebRtcCtl", "Checking");
    case rtc::PeerConnection::State::New:
        return QCoreApplication::translate("WebRtcCtl", "New");
    case rtc::PeerConnection::State::Failed:
        return QCoreApplication::translate("WebRtcCtl", "Failed");
    case rtc::PeerConnection::State::Disconnected:
        return QCoreApplication::translate("WebRtcCtl", "Disconnected");
    case rtc::PeerConnection::State::Closed:
        return QCoreApplication::translate("WebRtcCtl", "Closed");
    default:
        return QCoreApplication::translate("WebRtcCtl", "Unknown");
    }
}

QString iceStateText(rtc::PeerConnection::IceState state, QObject *context)
{
    Q_UNUSED(context);
    switch (state)
    {
    case rtc::PeerConnection::IceState::Connected:
        return QCoreApplication::translate("WebRtcCtl", "Connected");
    case rtc::PeerConnection::IceState::Checking:
        return QCoreApplication::translate("WebRtcCtl", "Checking");
    case rtc::PeerConnection::IceState::New:
        return QCoreApplication::translate("WebRtcCtl", "New");
    case rtc::PeerConnection::IceState::Failed:
        return QCoreApplication::translate("WebRtcCtl", "Failed");
    case rtc::PeerConnection::IceState::Disconnected:
        return QCoreApplication::translate("WebRtcCtl", "Disconnected");
    case rtc::PeerConnection::IceState::Closed:
        return QCoreApplication::translate("WebRtcCtl", "Closed");
    case rtc::PeerConnection::IceState::Completed:
        return QCoreApplication::translate("WebRtcCtl", "Completed");
    default:
        return QCoreApplication::translate("WebRtcCtl", "Unknown");
    }
}

QString gatheringStateText(rtc::PeerConnection::GatheringState state, QObject *context)
{
    Q_UNUSED(context);
    switch (state)
    {
    case rtc::PeerConnection::GatheringState::InProgress:
        return QCoreApplication::translate("WebRtcCtl", "In progress");
    case rtc::PeerConnection::GatheringState::Complete:
        return QCoreApplication::translate("WebRtcCtl", "Complete");
    case rtc::PeerConnection::GatheringState::New:
        return QCoreApplication::translate("WebRtcCtl", "New");
    default:
        return QCoreApplication::translate("WebRtcCtl", "Unknown");
    }
}
} /* namespace webrtc_ctl_peer_state */
