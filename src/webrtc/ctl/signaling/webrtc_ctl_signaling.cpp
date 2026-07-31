#include "webrtc/ctl/webrtc_ctl.h"
#include "common/constant.h"
#include "util/json/json_util.h"

void WebRtcCtl::sendDisconnectSignal(const QString &reason)
{
    if (m_disconnectSent || m_remoteDisconnectReceived)
        return;

    m_disconnectSent = true;
    const QJsonObject message = JsonUtil::createObject()
                                    .add(Constant::KEY_ROLE, Constant::ROLE_CTL)
                                    .add(Constant::KEY_TYPE, Constant::TYPE_SESSION_DISCONNECT)
                                    .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                    .add(Constant::KEY_RECEIVER, m_remoteId)
                                    .add(Constant::KEY_SESSION_ID, m_sessionId)
                                    .add(Constant::KEY_REASON, reason)
                                    .build();
    emit sendWsCliTextMsg(JsonUtil::toCompactString(message));
}

void WebRtcCtl::notifyLocalDisconnect()
{
    sendDisconnectSignal(QStringLiteral("controller_user"));
}


void WebRtcCtl::parseWsMsg(const QJsonObject &object)
{
    
    if (!object.contains(Constant::KEY_ROLE) || !object.contains(Constant::KEY_TYPE))
        return;

    QString role = JsonUtil::getString(object, Constant::KEY_ROLE);
    QString type = JsonUtil::getString(object, Constant::KEY_TYPE);

    
    if (role != Constant::ROLE_CLI)
    {
        return;
    }

    const QString sender = JsonUtil::getString(object, Constant::KEY_SENDER);
    const QString receiver = JsonUtil::getString(object, Constant::KEY_RECEIVER);
    if (sender != m_remoteId || receiver != ConfigUtil->local_id)
    {
        LOG_TRACE("Ignore signaling message {} for unrelated session sender={}, receiver={}",
                  type, sender, receiver);
        return;
    }
    const QString sessionId = JsonUtil::getString(object, Constant::KEY_SESSION_ID);

    if (type == Constant::TYPE_SESSION_DISCONNECT)
    {
        if (sessionId.isEmpty() || sessionId != m_sessionId)
        {
            LOG_TRACE("Ignore session disconnect for unrelated WebRTC session sessionId={}, currentSessionId={}, label={}",
                      sessionId, m_sessionId, m_sessionLabel);
            return;
        }
        m_remoteDisconnectReceived = true;
        disableReconnect();
        const QString reason = JsonUtil::getString(
            object, Constant::KEY_REASON, QStringLiteral("controlled_user"));
        emit remoteDisconnectRequested(reason, false);
        return;
    }

    if (type == Constant::TYPE_PEER_DISCONNECT)
    {
        m_remoteDisconnectReceived = true;
        disableReconnect();
        const QString reason = JsonUtil::getString(
            object, Constant::KEY_REASON, QStringLiteral("controlled_user"));
        emit remoteDisconnectRequested(reason, true);
        return;
    }

    if (!sessionId.isEmpty() && sessionId != m_sessionId)
    {
        LOG_TRACE("Ignore signaling message {} for unrelated WebRTC session sessionId={}, currentSessionId={}, label={}",
                  type, sessionId, m_sessionId, m_sessionLabel);
        return;
    }

    if (type == Constant::TYPE_ERROR)
    {
        handleSignalingError(object);
        return;
    }

    if (!m_peerConnection)
    {
        LOG_WARN("Ignore signaling message {} because peer connection is not ready", type);
        return;
    }

    
    if (type == Constant::TYPE_OFFER || type == Constant::TYPE_ANSWER)
    {
        handleRemoteDescriptionMessage(object, type);
    }
    
    else if (type == Constant::TYPE_CANDIDATE)
    {
        handleRemoteCandidateMessage(object);
    }
}


void WebRtcCtl::onWsCliRecvBinaryMsg(const QByteArray &message)
{
    parseWsMsg(JsonUtil::safeParseObject(message));
}


void WebRtcCtl::onWsCliRecvTextMsg(const QString &message)
{
    parseWsMsg(JsonUtil::safeParseObject(message.toUtf8()));
}
