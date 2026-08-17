#include "ws_cli.h"


#include "util/json/json_util.h"

#include <utility>

namespace
{
constexpr int kMaxPendingMessages = 1024;
constexpr qint64 kMaxPendingMessageBytes = 64LL * 1024 * 1024;
}


void WsCli::clearPendingMessages()
{
    m_pendingMessages.clear();
    m_pendingMessageBytes = 0;
}


void WsCli::queuePendingMessage(PendingMessage message)
{
    if (message.size <= 0 || message.size > kMaxPendingMessageBytes ||
        m_pendingMessages.size() >= kMaxPendingMessages ||
        message.size > kMaxPendingMessageBytes - m_pendingMessageBytes)
    {
        LOG_ERROR("WebSocket signaling retry queue overflow: size={}, queuedBytes={}, queuedMessages={}",
                  message.size, m_pendingMessageBytes, m_pendingMessages.size());
        clearPendingMessages();
        if (m_ws)
            m_ws->abort();
        return;
    }
    m_pendingMessageBytes += message.size;
    m_pendingMessages.enqueue(std::move(message));
    LOG_DEBUG("Queued WebSocket signaling message: queuedBytes={}, queuedMessages={}",
              m_pendingMessageBytes, m_pendingMessages.size());
}


bool WsCli::sendPendingMessageNow(const PendingMessage &message)
{
    if (message.size <= 0 || message.size > kMaxPendingMessageBytes)
    {
        LOG_ERROR("Rejected oversized outbound WebSocket signaling message: size={}", message.size);
        m_connected = false;
        if (m_ws)
            m_ws->abort();
        return false;
    }
    if (m_shutdownDone || !m_connected || !m_ws ||
        m_ws->state() != QAbstractSocket::ConnectedState)
        return false;

    const qint64 accepted = message.binary
                                ? m_ws->sendBinaryMessage(message.bytes)
                                : m_ws->sendTextMessage(message.text);
    if (accepted < 0)
    {
        LOG_ERROR("WebSocket rejected an outbound signaling message; reconnecting");
        m_connected = false;
        m_ws->abort();
        return false;
    }
    return true;
}


bool WsCli::isWebRtcSessionMessage(const PendingMessage &message) const
{
    const QByteArray payload = message.binary ? message.bytes : message.text.toUtf8();
    const QJsonObject object = JsonUtil::safeParseObject(payload);
    const QString type = JsonUtil::getString(object, Constant::KEY_TYPE);
    return type == Constant::TYPE_CONNECT ||
           type == Constant::TYPE_OFFER ||
           type == Constant::TYPE_ANSWER ||
           type == Constant::TYPE_CANDIDATE ||
           type == Constant::TYPE_SESSION_DISCONNECT ||
           type == Constant::TYPE_PEER_DISCONNECT ||
           type == Constant::TYPE_ERROR;
}


bool WsCli::flushPendingMessages()
{
    while (!m_pendingMessages.isEmpty())
    {
        const PendingMessage &message = m_pendingMessages.head();
        if (message.generation != 0 && message.generation != m_messageGeneration &&
            isWebRtcSessionMessage(message))
        {
            LOG_DEBUG("Dropping stale WebRTC signaling message from generation {} (current={})",
                      message.generation,
                      m_messageGeneration);
            m_pendingMessageBytes -= message.size;
            m_pendingMessages.dequeue();
            continue;
        }
        if (!sendPendingMessageNow(message))
            return false;
        m_pendingMessageBytes -= message.size;
        m_pendingMessages.dequeue();
    }
    if (m_ws)
        m_ws->flush();
    return true;
}


void WsCli::reConnect()
{
    if (!isSupportedSignalingUrl(m_url))
        return;
    emit wsOpen(m_url);
}


void WsCli::sendWsCliTextMsg(const QString &msg)
{
    if (m_shutdownDone || msg.isEmpty())
        return;
    PendingMessage message;
    message.text = msg;
    message.size = msg.toUtf8().size();
    message.generation = m_messageGeneration;
    if (!flushPendingMessages() || !sendPendingMessageNow(message))
        queuePendingMessage(std::move(message));
    else
        m_ws->flush();
}


void WsCli::sendWsCliBinaryMsg(const QByteArray &msg)
{
    if (m_shutdownDone || msg.isEmpty())
        return;
    PendingMessage message;
    message.binary = true;
    message.bytes = msg;
    message.size = msg.size();
    message.generation = m_messageGeneration;
    if (!flushPendingMessages() || !sendPendingMessageNow(message))
        queuePendingMessage(std::move(message));
    else
        m_ws->flush();
}


void WsCli::sendHeartMsg()
{
    if (m_connected && m_ws && m_ws->state() == QAbstractSocket::ConnectedState)
    {
        PendingMessage heartbeat;
        heartbeat.text = QStringLiteral("@heart");
        heartbeat.size = heartbeat.text.size();
        if (sendPendingMessageNow(heartbeat))
            m_ws->flush();
    }
}
