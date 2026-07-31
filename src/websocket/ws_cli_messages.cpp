#include "ws_cli.h"


void WsCli::reConnect()
{
    if (!isSupportedSignalingUrl(m_url))
        return;
    emit wsOpen(m_url);
}


void WsCli::sendWsCliTextMsg(const QString &msg)
{
    if (m_shutdownDone || !m_connected || !m_ws || m_ws->state() != QAbstractSocket::ConnectedState)
    {
        LOG_WARN("Cannot send WebSocket text message because socket is not connected");
        return;
    }
    m_ws->sendTextMessage(msg);
    m_ws->flush();
}


void WsCli::sendWsCliBinaryMsg(const QByteArray &msg)
{
    if (m_shutdownDone || !m_connected || !m_ws || m_ws->state() != QAbstractSocket::ConnectedState)
    {
        LOG_WARN("Cannot send WebSocket binary message because socket is not connected");
        return;
    }
    m_ws->sendBinaryMessage(msg);
    m_ws->flush();
}


void WsCli::sendHeartMsg()
{
    if (m_connected && m_ws && m_ws->state() == QAbstractSocket::ConnectedState)
    {
        m_ws->sendTextMessage("@heart");
        m_ws->flush();
    }
}
