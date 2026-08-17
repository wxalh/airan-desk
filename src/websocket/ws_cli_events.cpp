#include "ws_cli.h"

#include <QMetaEnum>


void WsCli::onWsAboutToClose()
{
    LOG_DEBUG("WebSocket about to close");
    m_connected = false;
}


void WsCli::onWsBinaryMessageReceived(const QByteArray &message)
{
    if (message.size() > kMaxInboundMessageBytes)
    {
        LOG_ERROR("Rejected oversized inbound binary signaling message: size={}", message.size());
        m_connected = false;
        if (m_ws)
            m_ws->abort();
        return;
    }
    LOG_TRACE("size:{}", message.size());
    emit onWsCliRecvBinaryMsg(message);
}


void WsCli::onWsTextMessageReceived(const QString &message)
{
    if (message.size() > kMaxInboundMessageBytes)
    {
        LOG_ERROR("Rejected oversized inbound text signaling message: characters={}", message.size());
        m_connected = false;
        if (m_ws)
            m_ws->abort();
        return;
    }
    const qint64 utf8Bytes = message.toUtf8().size();
    if (utf8Bytes > kMaxInboundMessageBytes)
    {
        LOG_ERROR("Rejected oversized inbound text signaling message: bytes={}", utf8Bytes);
        m_connected = false;
        if (m_ws)
            m_ws->abort();
        return;
    }
    LOG_TRACE("text size:{}", utf8Bytes);
    emit onWsCliRecvTextMsg(message);
}


void WsCli::onWsConnected()
{
    LOG_INFO("WebSocket connected successfully");
    m_connected = true;
    m_socketConnectTimer.invalidate();

    m_reconnect_phase = 0;
    m_reconnect_count = 0;
    emit stopReconnectTimer();
    emit onReconnectStatusUpdate(tr("Connection restored"), 0, 0, 0);

    if (!flushPendingMessages())
        return;

    emit startHeartTimer(m_heart_interval_ms);
    emit onWsCliConnected();
}


void WsCli::onWsDisconnected()
{
    LOG_WARN("WebSocket disconnected, starting intelligent reconnect");
    m_connected = false;
    ++m_messageGeneration;
    m_socketConnectTimer.invalidate();
    emit stopHeartTimer();
    emit onWsCliDisconnected();

    if (m_shutdownDone)
    {
        LOG_DEBUG("WebSocket disconnected during shutdown; not scheduling reconnect");
        return;
    }
    if (!isSupportedSignalingUrl(m_url))
    {
        LOG_INFO("WebSocket remains offline because signaling is not configured");
        return;
    }
    scheduleReconnect();
}


void WsCli::onWsError(QAbstractSocket::SocketError error)
{
    LOG_ERROR("WebSocket error: {} ({})",
              static_cast<int>(error),
              QMetaEnum::fromType<QAbstractSocket::SocketError>().valueToKey(error));
    if (!m_connected && error != QAbstractSocket::OperationError)
        LOG_INFO("Error occurred, may need to reconnect");
}


void WsCli::onWsPong(quint64 elapsedTime, const QByteArray &payload)
{
    LOG_TRACE("pong elapsedTime: {} payloadSize: {}", elapsedTime, payload.size());
}

void WsCli::onWsPreSharedKeyAuthenticationRequired(QSslPreSharedKeyAuthenticator *authenticator)
{
    Q_UNUSED(authenticator);
    LOG_ERROR("onWsPreSharedKeyAuthenticationRequired");
}

void WsCli::onWsProxyAuthenticationRequired(const QNetworkProxy &proxy, QAuthenticator *authenticator)
{
    Q_UNUSED(proxy);
    Q_UNUSED(authenticator);
    LOG_ERROR("onWsProxyAuthenticationRequired");
}

void WsCli::onWsSslErrors(const QList<QSslError> &errors)
{
    Q_UNUSED(errors);
    LOG_ERROR("onWsSslErrors");
}
