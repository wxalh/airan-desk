#include "webrtc/ctl/webrtc_ctl.h"

#include "common/protocol_constants.h"
#include "util/json/json_util.h"
#include "util/qt/qt_callback_util.h"

#include <QDateTime>
#include <QPointer>
#include <QThread>

namespace
{
constexpr qint64 kHeartbeatIntervalMs = 10000;
constexpr qint64 kUnstableThresholdMs = 60000;
constexpr qint64 kReconnectThresholdMs = 120000;
}

void WebRtcCtl::setupHeartbeatChannelCallbacks()
{
    if (!m_heartbeatChannel)
        return;
    m_heartbeatChannel->onOpen(makeWeakCallback(this, &WebRtcCtl::onHeartbeatChannelOpen, m_callbackLifetime));
    m_heartbeatChannel->onMessage(makeWeakCallback(this, &WebRtcCtl::onHeartbeatChannelMessage, m_callbackLifetime));
    m_heartbeatChannel->onError(makeWeakCallback(this, &WebRtcCtl::onHeartbeatChannelError, m_callbackLifetime));
    m_heartbeatChannel->onClosed(makeWeakCallback(this, &WebRtcCtl::onHeartbeatChannelClosed, m_callbackLifetime));
}

void WebRtcCtl::onHeartbeatChannelOpen()
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard]() {
            if (guard)
                guard->onHeartbeatChannelOpen();
        });
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastSessionInboundMs.store(now);
    m_lastSessionOutboundMs.store(now);
    m_lastSessionProgressMs.store(now);
    m_lastBufferedAmount = sampleSessionBufferedAmount();
    m_heartbeatNegotiated.store(false);
    if (m_sessionHeartbeatTimer)
        m_sessionHeartbeatTimer->start(1000);
    sendSessionHeartbeat(QStringLiteral("ping"));
}

void WebRtcCtl::onHeartbeatChannelMessage(rtc::message_variant message)
{
    if (m_shutdownStarted.load())
        return;
    noteSessionInboundActivity();
    if (!std::holds_alternative<std::string>(message))
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard, message = std::move(message)]() mutable {
            if (guard)
                guard->onHeartbeatChannelMessage(std::move(message));
        });
        return;
    }
    const std::string &text = std::get<std::string>(message);
    const QJsonObject object = JsonUtil::safeParseObject(QByteArray::fromRawData(text.data(), static_cast<int>(text.size())));
    if (JsonUtil::getString(object, Constant::KEY_MSGTYPE) != Constant::TYPE_SESSION_HEARTBEAT_MESSAGE)
        return;
    m_heartbeatNegotiated.store(true);
    if (JsonUtil::getString(object, Constant::KEY_ACTION) == QStringLiteral("ping"))
        sendSessionHeartbeat(QStringLiteral("pong"));
}

void WebRtcCtl::onHeartbeatChannelError(std::string error)
{
    if (m_shutdownStarted.load())
        return;
    LOG_WARN("Session heartbeat channel error: {}", error);
}

void WebRtcCtl::onHeartbeatChannelClosed()
{
    if (m_shutdownStarted.load())
        return;
    LOG_WARN("Session heartbeat channel closed; waiting for session timeout");
}

void WebRtcCtl::sendSessionHeartbeat(const QString &action)
{
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
            noteSessionOutboundActivity();
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

void WebRtcCtl::noteSessionInboundActivity()
{
    m_lastSessionInboundMs.store(QDateTime::currentMSecsSinceEpoch());
}

void WebRtcCtl::noteSessionOutboundActivity()
{
    m_lastSessionOutboundMs.store(QDateTime::currentMSecsSinceEpoch());
}

void WebRtcCtl::noteSessionTransportProgress()
{
    m_lastSessionProgressMs.store(QDateTime::currentMSecsSinceEpoch());
}

quint64 WebRtcCtl::sampleSessionBufferedAmount() const
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

void WebRtcCtl::setSessionHealth(int state, const QString &message)
{
    if (m_sessionHealthState == state)
        return;
    m_sessionHealthState = state;
    emit sessionHealthChanged(state, message);
}

void WebRtcCtl::requestSessionReconnect(const QString &message)
{
    if (!m_allowReconnect || m_shutdownStarted.load())
        return;
    setSessionHealth(2, message);
    scheduleReconnect();
}

void WebRtcCtl::pollSessionHeartbeat()
{
    if (m_shutdownStarted.load())
        return;
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
        now - m_lastSessionOutboundMs.load() >= kHeartbeatIntervalMs)
        sendSessionHeartbeat(QStringLiteral("ping"));

    if (!m_heartbeatNegotiated.load())
        return;
    const qint64 inboundSilence = now - m_lastSessionInboundMs.load();
    if (inboundSilence < kUnstableThresholdMs)
    {
        if (m_sessionHealthState != 0)
        {
            setSessionHealth(0, tr("Connection restored"));
        }
        return;
    }
    if (m_sessionHealthState == 0)
        setSessionHealth(1, tr("Connection is unstable"));
    if (inboundSilence >= kReconnectThresholdMs && now - m_lastSessionProgressMs.load() >= kReconnectThresholdMs)
        requestSessionReconnect(tr("Connection lost, reconnecting..."));
}
