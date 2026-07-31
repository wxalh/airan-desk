#include "ui/main/main_window.h"

#include "util/config/config_util.h"
#include "websocket/signaling_url_resolver.h"

#include <QHostInfo>
#include <QLabel>
#include <QThread>


void MainWindow::initCli()
{
    m_ws = new WsCli();
    m_ws_thread = new QThread();

    connect(m_ws, &WsCli::onWsCliConnected, this, &MainWindow::onWsCliConnected);
    connect(m_ws, &WsCli::onWsCliDisconnected, this, &MainWindow::onWsCliDisconnected);
    connect(m_ws, &WsCli::onReconnectStatusUpdate, this, &MainWindow::onWsCliReconnectStatus);
    connect(m_ws, &WsCli::onWsCliRecvBinaryMsg, this, &MainWindow::onWsCliRecvBinaryMsg);
    connect(m_ws, &WsCli::onWsCliRecvTextMsg, this, &MainWindow::onWsCliRecvTextMsg);
    connect(this, &MainWindow::initWsCli, m_ws, &WsCli::init);
    connect(this, &MainWindow::resetWsCliUrl, m_ws, &WsCli::resetUrlAndReconnect);
    connect(this, &MainWindow::sendWsCliBinaryMsg, m_ws, &WsCli::sendWsCliBinaryMsg);
    connect(this, &MainWindow::sendWsCliTextMsg, m_ws, &WsCli::sendWsCliTextMsg);

    connect(m_ws, &WsCli::shutdownFinished, m_ws_thread, &QThread::quit, Qt::DirectConnection);

    m_ws_thread->setObjectName("WsCliThread");
    m_ws->moveToThread(m_ws_thread);
    m_ws_thread->start();

    refreshSignalingStatus();
    emit initWsCli(buildWsUrl(), 30 * 1000);
}


QString MainWindow::buildWsUrl() const
{
    return SignalingUrlResolver::resolve(
               ConfigUtil->wsUrl,
               ConfigUtil->local_id,
               QHostInfo::localHostName(),
               ConfigUtil->install_id)
        .url;
}


void MainWindow::refreshSignalingStatus()
{
    if (!m_wsConnectStatus)
        return;
    const SignalingUrlResolver::Result result = SignalingUrlResolver::resolve(
        ConfigUtil->wsUrl,
        ConfigUtil->local_id,
        QHostInfo::localHostName(),
        ConfigUtil->install_id);
    if (result.state == SignalingUrlResolver::State::NotConfigured)
        m_wsConnectStatus->setText(tr("Server not configured"));
    else if (result.state == SignalingUrlResolver::State::Invalid)
        m_wsConnectStatus->setText(tr("Server configuration is invalid"));
    else
        m_wsConnectStatus->setText(tr("Connecting to server..."));
}


void MainWindow::applySignalingConfiguration(bool reconnect)
{
    refreshSignalingStatus();
    if (reconnect)
        emit resetWsCliUrl(buildWsUrl());
}


void MainWindow::onWsCliConnected()
{
    LOG_INFO("websocket connected");
    if (m_wsConnectStatus)
        m_wsConnectStatus->setText(tr("Server connected"));
}


void MainWindow::onWsCliDisconnected()
{
    LOG_WARN("WebSocket disconnected, auto-reconnect will be handled by WsCli");
    const SignalingUrlResolver::Result configured = SignalingUrlResolver::resolve(
        ConfigUtil->wsUrl,
        ConfigUtil->local_id,
        QHostInfo::localHostName(),
        ConfigUtil->install_id);
    if (configured.state != SignalingUrlResolver::State::Ready)
    {
        refreshSignalingStatus();
        return;
    }
    if (m_wsConnectStatus)
        m_wsConnectStatus->setText(tr("Server disconnected, trying to reconnect..."));
}


void MainWindow::onWsCliReconnectStatus(const QString &status,
                                        int phase,
                                        int attempt,
                                        int nextDelaySeconds)
{
    const SignalingUrlResolver::Result configured = SignalingUrlResolver::resolve(
        ConfigUtil->wsUrl,
        ConfigUtil->local_id,
        QHostInfo::localHostName(),
        ConfigUtil->install_id);
    if (configured.state != SignalingUrlResolver::State::Ready)
    {
        refreshSignalingStatus();
        return;
    }
    QString displayStatus;
    if (phase == 0 && attempt == 0)
    {
        displayStatus = tr("Server connected");
    }
    else if (nextDelaySeconds > 0)
    {
        displayStatus = tr("Server disconnected, reconnecting... (%1)").arg(status);
    }
    else
    {
        displayStatus = tr("Server disconnected, reconnect failed: %1").arg(status);
    }

    if (m_wsConnectStatus)
        m_wsConnectStatus->setText(displayStatus);

    LOG_INFO("Reconnect status update - Phase: {}, Attempt: {}, Status: {}",
             phase, attempt, status);
}
