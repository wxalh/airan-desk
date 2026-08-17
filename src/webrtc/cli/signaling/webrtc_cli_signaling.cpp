#include "webrtc/cli/webrtc_cli.h"
#include "webrtc/codec/video_codec_capability_signaling.h"
#include "common/constant.h"
#include "util/json/json_util.h"

#include <utility>

namespace
{
constexpr int kMaxSdpChars = 4 * 1024 * 1024;
}


void WebRtcCli::onWsCliRecvBinaryMsg(const QByteArray &message)
{
    parseWsMsg(JsonUtil::safeParseObject(message));
}


void WebRtcCli::onWsCliRecvTextMsg(const QString &message)
{
    parseWsMsg(JsonUtil::safeParseObject(message.toUtf8()));
}


void WebRtcCli::parseWsMsg(const QJsonObject &object)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    QString type = JsonUtil::getString(object, Constant::KEY_TYPE);
    if (type.isEmpty())
    {
        LOG_ERROR("parseWsMsg: Missing or empty message type");
        return;
    }

    const QString role = JsonUtil::getString(object, Constant::KEY_ROLE);
    if (role != Constant::ROLE_CTL)
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
            LOG_TRACE("Ignore session disconnect for unrelated WebRTC session sessionId={}, currentSessionId={}",
                      sessionId, m_sessionId);
            return;
        }
        const QString reason = JsonUtil::getString(
            object, Constant::KEY_REASON, QStringLiteral("controller_user"));
        m_remoteDisconnectReceived = true;
        requestDisconnect(reason);
        return;
    }

    const bool sessionScopedSignaling = type == Constant::TYPE_OFFER ||
                                        type == Constant::TYPE_ANSWER ||
                                        type == Constant::TYPE_CANDIDATE;
    if (sessionScopedSignaling && (sessionId.isEmpty() || sessionId != m_sessionId))
    {
        LOG_TRACE("Ignore signaling message {} for unrelated WebRTC session sessionId={}", type, sessionId);
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


void WebRtcCli::handleRemoteDescriptionMessage(const QJsonObject &object, const QString &type)
{
    QString data = JsonUtil::getString(object, Constant::KEY_DATA);
    if (data.isEmpty() || data.size() > kMaxSdpChars)
    {
        LOG_ERROR("parseWsMsg: Invalid SDP size for {} message: {}", type, data.size());
        return;
    }

    if (m_remoteDescriptionInFlight)
    {
        m_pendingRemoteDescriptionData = data;
        m_pendingRemoteDescriptionType = type;
        m_pendingRemoteDescriptionObject = object;
        LOG_WARN("Remote description already in flight; retaining latest {}", type);
        return;
    }

    auto remoteCapabilities = parseVideoCodecCapabilities(object);
    if (!remoteCapabilities.empty() && m_peerConnection)
    {
        logVideoCodecCapabilities("Remote", remoteCapabilities);
        m_peerConnection->setRemoteVideoCodecCapabilities(std::move(remoteCapabilities));
    }
    setRemoteDescription(data, type);
    LOG_TRACE("parseWsMsg: Processed {} message", type);
}


void WebRtcCli::handleRemoteCandidateMessage(const QJsonObject &object)
{
    QString data = JsonUtil::getString(object, Constant::KEY_DATA);
    QString mid = JsonUtil::getString(object, Constant::KEY_MID);
    if (!data.isEmpty())
    {
        addRemoteCandidateOrQueue(data, mid);
        LOG_TRACE("parseWsMsg: Processed candidate message");
    }
    else
    {
        LOG_ERROR("parseWsMsg: Empty data for candidate message");
    }
}
