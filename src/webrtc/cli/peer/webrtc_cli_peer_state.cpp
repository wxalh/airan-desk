#include "webrtc/cli/webrtc_cli.h"
#include "common/constant.h"
#include "security/audit_session.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"


void WebRtcCli::onPeerConnectionStateChanged(rtc::PeerConnection::State state)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, "onPeerConnectionStateChanged", Qt::QueuedConnection,
                                  Q_ARG(rtc::PeerConnection::State, state));
        return;
    }

    if (m_destroying)
    {
        LOG_DEBUG("Ignoring state change callback during destruction");
        return;
    }

    const bool wasConnected = m_connected;
    m_connected = (state == rtc::PeerConnection::State::Connected);
    m_peerConnected.store(m_connected);
    if (m_connected && m_peerStartupTimer)
        m_peerStartupTimer->stop();
    std::string stateStr;
    if (m_connected)
    {
        stateStr = "Connected";
        if (m_peerConnection)
        {
            const QPointer<WebRtcCli> guard(this);
            const auto callbackLifetime = m_callbackLifetime;
            m_peerConnection->querySelectedCandidatePair(
                [guard, callbackLifetime](bool found, rtc::SelectedCandidatePair pair) {
                    auto permit = callbackLifetime->tryEnter();
                    if (!permit || !guard || !found)
                        return;
                    guard->m_callbackDispatcher->post([guard, pair]() {
                        if (!guard || guard->m_destroying)
                            return;
                        LOG_DEBUG("Selected candidate pair from stats: localType={}, localProtocol={}, localRelayProtocol={}, remoteType={}, remoteProtocol={}, remoteRelayProtocol={}",
                                  pair.local.candidateType,
                                  pair.local.protocol,
                                  pair.local.relayProtocol,
                                  pair.remote.candidateType,
                                  pair.remote.protocol,
                                  pair.remote.relayProtocol);
                    });
                });
        }
    }
    else if (state == rtc::PeerConnection::State::Connecting)
        stateStr = "Checking";
    else if (state == rtc::PeerConnection::State::New)
        stateStr = "New";
    else if (state == rtc::PeerConnection::State::Failed)
        stateStr = "Failed";
    else if (state == rtc::PeerConnection::State::Disconnected)
        stateStr = "Disconnected";
    else if (state == rtc::PeerConnection::State::Closed)
        stateStr = "Closed";
    else
        stateStr = "Unknown";

    LOG_INFO("Client side connection state: {}", stateStr);
    if (m_connected && !wasConnected && !m_connectionAudited)
    {
        m_connectionAudited = true;
        if (m_auditSession)
            m_auditSession->recordConnected(m_visible_width, m_visible_height);
        emit controlledSessionConnected(m_sessionId,
                                        m_remoteId,
                                        m_controlledSessionMode,
                                        m_auditSession ? m_auditSession->sourceIp() : QString());
    }
    if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed)
    {
        m_disconnectReason = state == rtc::PeerConnection::State::Failed
                                 ? QStringLiteral("network_error")
                                 : QStringLiteral("remote_or_network");
        if (m_sessionHeartbeatTimer)
            m_sessionHeartbeatTimer->stop();
        emit destroyCli();
    }

    if (m_isOnlyFile)
        return;

    if (m_connected)
    {
        LOG_INFO("WebRTC connection established, enabling libwebrtc media tracks");
        if (m_disconnectGraceTimer)
            QMetaObject::invokeMethod(m_disconnectGraceTimer, "stop", Qt::QueuedConnection);
        m_lastControlAliveMs = QDateTime::currentMSecsSinceEpoch();
        if (m_controlWatchdogTimer)
            QMetaObject::invokeMethod(m_controlWatchdogTimer, "start", Qt::QueuedConnection, Q_ARG(int, 2000));
        if (m_mediaStatsTimer)
            QMetaObject::invokeMethod(m_mediaStatsTimer, "start", Qt::QueuedConnection, Q_ARG(int, 2000));
        startMediaCapture();
    }
    else if (state == rtc::PeerConnection::State::Disconnected)
    {
        LOG_INFO("WebRTC disconnected, starting grace timer before stopping media");
        if (m_controlWatchdogTimer)
            QMetaObject::invokeMethod(m_controlWatchdogTimer, "stop", Qt::QueuedConnection);
        if (m_mediaStatsTimer)
            QMetaObject::invokeMethod(m_mediaStatsTimer, "stop", Qt::QueuedConnection);
        if (m_disconnectGraceTimer)
            QMetaObject::invokeMethod(m_disconnectGraceTimer, "start", Qt::QueuedConnection, Q_ARG(int, 6000));
    }
    else if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed)
    {
        LOG_INFO("WebRTC connection failed/closed, stopping media");
        if (m_controlWatchdogTimer)
            QMetaObject::invokeMethod(m_controlWatchdogTimer, "stop", Qt::QueuedConnection);
        if (m_mediaStatsTimer)
            QMetaObject::invokeMethod(m_mediaStatsTimer, "stop", Qt::QueuedConnection);
        if (m_disconnectGraceTimer)
            QMetaObject::invokeMethod(m_disconnectGraceTimer, "stop", Qt::QueuedConnection);
        QMetaObject::invokeMethod(this, "stopMediaCapture", Qt::QueuedConnection);
    }
}

void WebRtcCli::requestLocalDisconnect()
{
    sendSessionDisconnect(QStringLiteral("controlled_user"));
    requestDisconnect(QStringLiteral("controlled_user"));
}


void WebRtcCli::sendSessionDisconnect(const QString &reason)
{
    if (m_disconnectSent)
        return;

    m_disconnectSent = true;
    const QJsonObject message = JsonUtil::createObject()
                                    .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                    .add(Constant::KEY_TYPE, Constant::TYPE_SESSION_DISCONNECT)
                                    .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                    .add(Constant::KEY_RECEIVER, m_remoteId)
                                    .add(Constant::KEY_SESSION_ID, m_sessionId)
                                    .add(Constant::KEY_REASON,
                                         reason.trimmed().isEmpty()
                                             ? QStringLiteral("remote_or_network")
                                             : reason.trimmed())
                                    .build();
    emit sendWsCliTextMsg(JsonUtil::toCompactString(message));
}

void WebRtcCli::requestDisconnect(const QString &reason)
{
    m_disconnectReason = reason.isEmpty() ? QStringLiteral("remote_or_network") : reason;
    emit destroyCli();
}


void WebRtcCli::onPeerIceStateChanged(rtc::PeerConnection::IceState state)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, "onPeerIceStateChanged", Qt::QueuedConnection,
                                  Q_ARG(rtc::PeerConnection::IceState, state));
        return;
    }

    std::string stateStr;
    if (state == rtc::PeerConnection::IceState::Connected)
        stateStr = "Connected";
    else if (state == rtc::PeerConnection::IceState::Checking)
        stateStr = "Checking";
    else if (state == rtc::PeerConnection::IceState::New)
        stateStr = "New";
    else if (state == rtc::PeerConnection::IceState::Failed)
        stateStr = "Failed";
    else if (state == rtc::PeerConnection::IceState::Disconnected)
        stateStr = "Disconnected";
    else if (state == rtc::PeerConnection::IceState::Closed)
        stateStr = "Closed";
    else if (state == rtc::PeerConnection::IceState::Completed)
        stateStr = "Completed";
    else
        stateStr = "Unknown";
    LOG_INFO("Client side ICE state: {}", stateStr);
}


void WebRtcCli::onPeerGatheringStateChanged(rtc::PeerConnection::GatheringState state)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, "onPeerGatheringStateChanged", Qt::QueuedConnection,
                                  Q_ARG(rtc::PeerConnection::GatheringState, state));
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
    LOG_DEBUG("Client side gathering state: {}", stateStr);
}
