#include "webrtc/cli/webrtc_cli.h"

#include "common/protocol_constants.h"
#include "util/json/json_util.h"
#include "util/qt/qt_callback_util.h"

#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

namespace
{
constexpr qint64 kHeartbeatIntervalMs = 10000;
constexpr qint64 kReconnectThresholdMs = 120000;
}

void WebRtcCli::setupHeartbeatChannelCallbacks()
{
    if (!m_heartbeatChannel)
        return;
    m_heartbeatChannel->onOpen(makeWeakCallback(this, &WebRtcCli::onHeartbeatChannelOpen, m_callbackLifetime));
    m_heartbeatChannel->onMessage(makeWeakCallback(this, &WebRtcCli::onHeartbeatChannelMessage, m_callbackLifetime));
    m_heartbeatChannel->onError(makeWeakCallback(this, &WebRtcCli::onHeartbeatChannelError, m_callbackLifetime));
    m_heartbeatChannel->onClosed(makeWeakCallback(this, &WebRtcCli::onHeartbeatChannelClosed, m_callbackLifetime));
}

void WebRtcCli::onHeartbeatChannelOpen()
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, "onHeartbeatChannelOpen", Qt::QueuedConnection);
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastSessionInboundMs.store(now);
    m_lastSessionOutboundMs.store(now);
    m_lastSessionProgressMs.store(now);
    m_lastBufferedAmount = sampleSessionBufferedAmount();
    m_lastSessionHeartbeatSentMs = 0;
    m_heartbeatNegotiated.store(false);
    if (m_sessionHeartbeatTimer)
        m_sessionHeartbeatTimer->start(1000);
    sendSessionHeartbeat(QStringLiteral("ping"));
}

void WebRtcCli::onHeartbeatChannelMessage(rtc::message_variant data)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;
    noteSessionInboundActivity();
    if (!std::holds_alternative<std::string>(data))
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard, data = std::move(data)]() mutable {
            if (guard)
                guard->onHeartbeatChannelMessage(std::move(data));
        });
        return;
    }
    const std::string &text = std::get<std::string>(data);
    const QJsonObject object = JsonUtil::safeParseObject(QByteArray::fromRawData(text.data(), static_cast<int>(text.size())));
    if (JsonUtil::getString(object, Constant::KEY_MSGTYPE) != Constant::TYPE_SESSION_HEARTBEAT_MESSAGE)
        return;
    m_heartbeatNegotiated.store(true);
    if (JsonUtil::getString(object, Constant::KEY_ACTION) == QStringLiteral("ping"))
        sendSessionHeartbeat(QStringLiteral("pong"));
}

void WebRtcCli::onHeartbeatChannelError(std::string error)
{
    if (!m_shutdownRequested.load() && !m_shutdownStarted.load())
        LOG_WARN("Session heartbeat channel error: {}", error);
}

void WebRtcCli::onHeartbeatChannelClosed()
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load() || m_destroying)
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onHeartbeatChannelClosed();
        });
        return;
    }
    LOG_WARN("Session heartbeat channel closed; destroying stale controlled session");
    m_disconnectReason = QStringLiteral("heartbeat_channel_closed");
    emit destroyCli();
}

void WebRtcCli::sendSessionHeartbeat(const QString &action)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    if (!m_heartbeatChannel || !m_heartbeatChannel->isOpen())
        return;
    const QJsonObject object = JsonUtil::createObject()
                                   .add(Constant::KEY_MSGTYPE, Constant::TYPE_SESSION_HEARTBEAT_MESSAGE)
                                   .add(Constant::KEY_ACTION, action)
                                   .add(Constant::KEY_SEQUENCE, static_cast<double>(++m_heartbeatSequence))
                                   .build();
    try
    {
        if (m_heartbeatChannel->send(JsonUtil::toCompactString(object).toStdString()))
        {
            m_lastSessionHeartbeatSentMs = QDateTime::currentMSecsSinceEpoch();
            noteSessionOutboundActivity();
        }
    }
    catch (const std::exception &e)
    {
        LOG_WARN("Failed to send session heartbeat: {}", e.what());
    }
    catch (...)
    {
        LOG_WARN("Failed to send session heartbeat: unknown error");
    }
}

void WebRtcCli::noteSessionInboundActivity()
{
    m_lastSessionInboundMs.store(QDateTime::currentMSecsSinceEpoch());
}

void WebRtcCli::noteSessionOutboundActivity()
{
    m_lastSessionOutboundMs.store(QDateTime::currentMSecsSinceEpoch());
}

void WebRtcCli::noteSessionTransportProgress()
{
    m_lastSessionProgressMs.store(QDateTime::currentMSecsSinceEpoch());
}

quint64 WebRtcCli::sampleSessionBufferedAmount() const
{
    quint64 total = 0;
    const std::shared_ptr<rtc::DataChannel> channels[] = {m_fileChannel, m_fileTextChannel, m_inputChannel, m_inputMoveChannel, m_clipboardChannel};
    for (const auto &channel : channels)
    {
        if (channel)
            total += channel->bufferedAmount();
    }
    return total;
}

void WebRtcCli::pollSessionHeartbeat()
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;
    flushPendingFileTextMessages();
    flushPendingInputMessages();
    flushPendingClipboardControlMessages();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastSessionOutboundMs.load() == 0)
        m_lastSessionOutboundMs.store(now);
    if (m_lastSessionInboundMs.load() == 0)
        m_lastSessionInboundMs.store(now);
    if (m_lastSessionProgressMs.load() == 0)
        m_lastSessionProgressMs.store(now);
    const quint64 buffered = sampleSessionBufferedAmount();
    if (buffered < m_lastBufferedAmount)
        noteSessionTransportProgress();
    m_lastBufferedAmount = buffered;
    if (m_heartbeatChannel && m_heartbeatChannel->isOpen() &&
        (m_lastSessionHeartbeatSentMs == 0 || now - m_lastSessionHeartbeatSentMs >= kHeartbeatIntervalMs))
        sendSessionHeartbeat(QStringLiteral("ping"));
    if (!m_heartbeatNegotiated.load())
        return;
    if (now - m_lastSessionInboundMs.load() >= kReconnectThresholdMs &&
        now - m_lastSessionProgressMs.load() >= kReconnectThresholdMs &&
        !m_destroying)
    {
        LOG_WARN("Session heartbeat timed out without transport progress; destroying controlled session");
        emit destroyCli();
    }
}
