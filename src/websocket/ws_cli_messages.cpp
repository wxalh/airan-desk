#include "ws_cli.h"


#include "util/json/json_util.h"

#include <QJsonDocument>
#include <QSysInfo>
#include <QUrlQuery>
#include <QUuid>

#include <utility>

namespace
{
constexpr int kMaxPendingMessages = 1024;
constexpr qint64 kMaxPendingMessageBytes = 64LL * 1024 * 1024;
constexpr int kMaxPresenceDevices = 1000;
constexpr int kMaxPresenceMessageBytes = 2048;
constexpr int kPresenceTimeoutMs = 8000;
const QString kPresenceRequestType = QStringLiteral("device_info_request");
const QString kPresenceResponseType = QStringLiteral("device_info_response");
const QString kPresenceRolePrefix = QStringLiteral("device_presence:");
}

QString WsCli::presenceLocalId() const
{
    return QUrlQuery(m_url).queryItemValue(QStringLiteral("sessionId"));
}

bool WsCli::sendPresenceMessage(const QJsonObject &object)
{
    if (m_shutdownDone || !m_connected || !m_ws ||
        m_ws->state() != QAbstractSocket::ConnectedState)
        return false;

    const QString text = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
    if (text.toUtf8().size() > kMaxPresenceMessageBytes)
        return false;
    // Presence is short lived: it must never enter the reconnect retry queue.
    if (m_ws->sendTextMessage(text) < 0)
        return false;
    return true;
}

void WsCli::clearPresenceState()
{
    m_presence_timer->stop();
    m_presence_query_timer->stop();
    m_presenceRequests.clear();
    const QSet<QString> devices = m_presenceDevices;
    m_presenceDevices.clear();
    m_presenceQueryWindow.invalidate();
    m_presenceReplyWindow.invalidate();
    m_presenceRepliesInWindow = 0;
    for (const QString &id : devices)
        emit devicePresenceChanged(id, QStringLiteral("unknown"), QString());
}

void WsCli::expirePresenceRequests()
{
    const qint64 now = m_presenceClock.elapsed();
    QStringList expired;
    for (auto it = m_presenceRequests.begin(); it != m_presenceRequests.end();)
    {
        if (now - it->sentAt >= kPresenceTimeoutMs)
        {
            expired.append(it->deviceId);
            it = m_presenceRequests.erase(it);
        }
        else
            ++it;
    }
    if (m_presenceRequests.isEmpty())
        m_presence_timer->stop();
    for (const QString &id : expired)
        emit devicePresenceChanged(id, QStringLiteral("unknown"), QString());
}

void WsCli::queryDevicePresence(const QStringList &ids)
{
    // Starting a refresh invalidates all outstanding requests from the old one.
    m_presenceRequests.clear();
    m_presence_timer->stop();
    m_presence_query_timer->stop();
    m_presenceDevices.clear();
    for (const QString &value : ids)
    {
        const QString id = value.trimmed();
        if (!id.isEmpty())
            emit devicePresenceChanged(id, QStringLiteral("unknown"), QString());
        if (m_presenceDevices.size() < kMaxPresenceDevices &&
            !id.isEmpty() && id.size() <= 128 && id != Constant::ROLE_SERVER)
            m_presenceDevices.insert(id);
    }

    if (m_shutdownDone || !m_connected || !m_ws ||
        m_ws->state() != QAbstractSocket::ConnectedState || presenceLocalId().isEmpty())
        return;
    // Bound repeated manual refreshes as well as the size of each batch.
    if (m_presenceQueryWindow.isValid() && m_presenceQueryWindow.elapsed() < 1000)
    {
        m_presence_query_timer->start(qMax(1, 1000 - static_cast<int>(m_presenceQueryWindow.elapsed())));
        return;
    }
    m_presenceQueryWindow.start();

    const QString localId = presenceLocalId();
    for (const QString &id : m_presenceDevices)
    {
        if (id == localId)
        {
            emit devicePresenceChanged(id, QStringLiteral("online"), QSysInfo::prettyProductName());
            continue;
        }
        const QString requestId = QUuid::createUuid().toString().remove(QLatin1Char('{')).remove(QLatin1Char('}'));
        const QJsonObject object{
            {Constant::KEY_SENDER, localId},
            {Constant::KEY_RECEIVER, id},
            {Constant::KEY_TYPE, kPresenceRequestType},
            {Constant::KEY_ROLE, kPresenceRolePrefix + requestId},
            {Constant::KEY_REQUEST_ID, requestId}};
        m_presenceRequests.insert(requestId, PresenceRequest{id, m_presenceClock.elapsed()});
        if (!sendPresenceMessage(object))
            m_presenceRequests.remove(requestId);
    }
    if (!m_presenceRequests.isEmpty())
        m_presence_timer->start();
    m_ws->flush();
}

bool WsCli::handlePresenceMessage(const QByteArray &message)
{
    const QJsonDocument document = QJsonDocument::fromJson(message);
    if (!document.isObject())
        return false;
    const QJsonObject object = document.object();
    const QString type = object.value(Constant::KEY_TYPE).toString();
    const QString role = object.value(Constant::KEY_ROLE).toString();
    const bool presenceError = type == Constant::TYPE_ERROR && role.startsWith(kPresenceRolePrefix);
    if (type != kPresenceRequestType && type != kPresenceResponseType && !presenceError)
        return false;
    // Consume invalid and stale presence messages too, so they cannot affect RTC sessions.
    if (message.size() > kMaxPresenceMessageBytes || m_shutdownDone || !m_connected)
        return true;

    const QString localId = presenceLocalId();
    const QString sender = object.value(Constant::KEY_SENDER).toString();
    if (localId.isEmpty() || sender.isEmpty() || sender.size() > 128 ||
        object.value(Constant::KEY_RECEIVER).toString() != localId)
        return true;

    if (presenceError)
    {
        if (sender != Constant::ROLE_SERVER)
            return true;
        const QString requestId = role.mid(kPresenceRolePrefix.size());
        const auto it = m_presenceRequests.constFind(requestId);
        if (it == m_presenceRequests.constEnd())
            return true;
        const QString id = it->deviceId;
        const bool expired = m_presenceClock.elapsed() - it->sentAt >= kPresenceTimeoutMs;
        m_presenceRequests.remove(requestId);
        const QString error = object.value(Constant::KEY_DATA).toString();
        const bool offline = error == QStringLiteral("The controlled end may not be online") ||
                             error == QStringLiteral("controlled_offline") ||
                             error == QStringLiteral("not found recv id");
        emit devicePresenceChanged(id, !expired && offline ? QStringLiteral("offline")
                                                         : QStringLiteral("unknown"), QString());
        return true;
    }

    const QString requestId = object.value(Constant::KEY_REQUEST_ID).toString();
    if (requestId.isEmpty() || requestId.size() > 64 || QUuid(requestId).isNull() ||
        role != kPresenceRolePrefix + requestId || sender == Constant::ROLE_SERVER)
        return true;

    if (type == kPresenceRequestType)
    {
        if (!m_presenceReplyWindow.isValid() || m_presenceReplyWindow.elapsed() >= 1000)
        {
            m_presenceReplyWindow.start();
            m_presenceRepliesInWindow = 0;
        }
        if (m_presenceRepliesInWindow >= 32)
            return true;
        ++m_presenceRepliesInWindow;
        const QJsonObject response{
            {Constant::KEY_SENDER, localId},
            {Constant::KEY_RECEIVER, sender},
            {Constant::KEY_TYPE, kPresenceResponseType},
            {Constant::KEY_ROLE, role},
            {Constant::KEY_REQUEST_ID, requestId},
            {Constant::KEY_OS, QSysInfo::prettyProductName().left(128)}};
        if (sendPresenceMessage(response))
            m_ws->flush();
        return true;
    }

    const auto it = m_presenceRequests.constFind(requestId);
    if (it == m_presenceRequests.constEnd() || it->deviceId != sender)
        return true;
    const bool expired = m_presenceClock.elapsed() - it->sentAt >= kPresenceTimeoutMs;
    m_presenceRequests.remove(requestId);
    QString os = object.value(Constant::KEY_OS).toString().left(128);
    for (int index = 0; index < os.size(); ++index)
    {
        if (os.at(index).category() == QChar::Other_Control)
            os[index] = QLatin1Char(' ');
    }
    emit devicePresenceChanged(sender, expired ? QStringLiteral("unknown") : QStringLiteral("online"),
                               expired ? QString() : os.trimmed());
    return true;
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
