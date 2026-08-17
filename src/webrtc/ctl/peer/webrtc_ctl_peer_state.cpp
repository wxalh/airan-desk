#include "webrtc/ctl/webrtc_ctl.h"
#include "webrtc/ctl/network/webrtc_ctl_network_path_util.h"
#include "webrtc/ctl/peer/webrtc_ctl_peer_state_text.h"
#include "util/qt/qt_callback_util.h"

#include <QPointer>
#include <QThread>

using webrtc_ctl_network_path::selectedNetworkPathFromPair;
using webrtc_ctl_peer_state::gatheringStateText;
using webrtc_ctl_peer_state::iceStateText;
using webrtc_ctl_peer_state::peerConnectionStateText;


void WebRtcCtl::querySelectedNetworkPath()
{
    if (!m_peerConnection || m_shutdownStarted.load())
        return;

    const QPointer<WebRtcCtl> guard(this);
    const auto callbackLifetime = m_callbackLifetime;
    m_peerConnection->querySelectedCandidatePair(
        [guard, callbackLifetime](bool found, rtc::SelectedCandidatePair pair) {
            auto permit = callbackLifetime->tryEnter();
            if (!permit || !guard || !found)
                return;

            const QString selectedPath = selectedNetworkPathFromPair(pair);
            if (selectedPath.isEmpty())
                return;

            LOG_DEBUG("Selected candidate pair from stats: localType={}, localProtocol={}, localRelayProtocol={}, remoteType={}, remoteProtocol={}, remoteRelayProtocol={}",
                      pair.local.candidateType,
                      pair.local.protocol,
                      pair.local.relayProtocol,
                      pair.remote.candidateType,
                      pair.remote.protocol,
                      pair.remote.relayProtocol);
            guard->m_callbackDispatcher->post([guard, selectedPath]() {
                if (guard && !guard->m_shutdownStarted.load())
                    guard->publishNetworkPathState(selectedPath);
            });
        });
}

/*
 * Handles PeerConnection state changes, publishes the selected path, and schedules reconnects when needed.
 */
void WebRtcCtl::onPeerConnectionStateChanged(rtc::PeerConnection::State state)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard, state]() {
            if (guard)
                guard->onPeerConnectionStateChanged(state);
        });
        return;
    }
    m_connected = (state == rtc::PeerConnection::State::Connected);
    if (m_connected)
    {
        if (m_peerStartupTimer)
            m_peerStartupTimer->stop();
        flushPendingFileTextMessages();
    }

    std::string stateStr;
    if (state == rtc::PeerConnection::State::Connected)
    {
        stateStr = "Connected";
        querySelectedNetworkPath();
        stopReconnect();
    }
    else if (state == rtc::PeerConnection::State::Connecting)
        stateStr = "Checking";
    else if (state == rtc::PeerConnection::State::New)
        stateStr = "New";
    else if (state == rtc::PeerConnection::State::Failed)
    {
        stateStr = "Failed";
        requestSessionReconnect(tr("WebRTC connection failed, reconnecting..."));
    }
    else if (state == rtc::PeerConnection::State::Disconnected)
        stateStr = "Disconnected";
    else if (state == rtc::PeerConnection::State::Closed)
    {
        stateStr = "Closed";
        requestSessionReconnect(tr("WebRTC connection closed, reconnecting..."));
    }
    else
        stateStr = "Unknown";
    LOG_DEBUG("Control side connection state: {}", stateStr);
    emit connectionStatusChanged(tr("WebRTC connection state: %1").arg(peerConnectionStateText(state, this)));
}

/*
 * Handles ICE connection state changes.
 */
void WebRtcCtl::onPeerIceStateChanged(rtc::PeerConnection::IceState state)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard, state]() {
            if (guard)
                guard->onPeerIceStateChanged(state);
        });
        return;
    }
    std::string stateStr;
    if (state == rtc::PeerConnection::IceState::Connected)
    {
        stateStr = "Connected";
        stopReconnect();
    }
    else if (state == rtc::PeerConnection::IceState::Checking)
        stateStr = "Checking";
    else if (state == rtc::PeerConnection::IceState::New)
        stateStr = "New";
    else if (state == rtc::PeerConnection::IceState::Failed)
    {
        stateStr = "Failed";
        requestSessionReconnect(tr("ICE connection failed, reconnecting..."));
    }
    else if (state == rtc::PeerConnection::IceState::Disconnected)
        stateStr = "Disconnected";
    else if (state == rtc::PeerConnection::IceState::Closed)
    {
        stateStr = "Closed";
        requestSessionReconnect(tr("ICE connection closed, reconnecting..."));
    }
    else if (state == rtc::PeerConnection::IceState::Completed)
    {
        stateStr = "Completed";
        stopReconnect();
    }
    else
        stateStr = "Unknown";
    LOG_INFO("Control side ICE state: {}", stateStr);
    emit connectionStatusChanged(tr("ICE state: %1").arg(iceStateText(state, this)));
}

/*
 * Handles ICE gathering state changes for logs and progress text.
 */
void WebRtcCtl::onPeerGatheringStateChanged(rtc::PeerConnection::GatheringState state)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard, state]() {
            if (guard)
                guard->onPeerGatheringStateChanged(state);
        });
        return;
    }
    std::string stateStr;
    if (state == rtc::PeerConnection::GatheringState::InProgress)
        stateStr = "InProgress";
    else if (state == rtc::PeerConnection::GatheringState::Complete)
        stateStr = "Complete";
    else if (state == rtc::PeerConnection::GatheringState::New)
        stateStr = "New";
    else
        stateStr = "Unknown";
    LOG_INFO("Control side ICE gathering state: {}", stateStr);
    emit connectionStatusChanged(tr("ICE gathering: %1").arg(gatheringStateText(state, this)));
}
