#include "webrtc/ctl/webrtc_ctl.h"
#include "util/file/file_packet_util.h"

#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <algorithm>

void WebRtcCtl::disableReconnect()
{
    m_allowReconnect = false;
    m_reconnectPending.store(false);
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
}

void WebRtcCtl::scheduleReconnect()
{
    if (!m_allowReconnect || m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, "scheduleReconnect", Qt::QueuedConnection);
        return;
    }

    if (!m_allowReconnect || m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    
    bool expected = false;
    if (!m_reconnectPending.compare_exchange_strong(expected, true))
    {
        LOG_DEBUG("Reconnect already pending");
        return;
    }

    if (m_reconnectTimer && m_reconnectTimer->isActive())
    {
        LOG_DEBUG("Reconnect already scheduled");
        return;
    }

    m_reconnectAttempts++;

    
    if (m_reconnectAttempts == 1)
    {
        m_reconnectBackoffMs = 3000;
    }
    else
    {
        m_reconnectBackoffMs = (std::min)(m_reconnectBackoffMs * 2, 15000);
    }

    LOG_INFO("Scheduling reconnect attempt {}, backoff {} ms", m_reconnectAttempts, m_reconnectBackoffMs);
    
    if (m_reconnectTimer)
    {
        m_reconnectTimer->setSingleShot(true);
        m_reconnectTimer->setInterval(m_reconnectBackoffMs);
        m_reconnectTimer->start();
    }
}

void WebRtcCtl::stopReconnect()
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, "stopReconnect", Qt::QueuedConnection);
        return;
    }

    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    m_reconnectAttempts = 0;
    m_reconnectBackoffMs = 3000; 
    m_reconnectPending.store(false);
}

void WebRtcCtl::setNetworkPath(const QString &networkPath)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, "setNetworkPath", Qt::QueuedConnection,
                                  Q_ARG(QString, networkPath));
        return;
    }

    const QString normalized = networkPath.toLower();
    if (normalized != QStringLiteral("auto") &&
        normalized != QStringLiteral("direct") &&
        normalized != QStringLiteral("turn_udp") &&
        normalized != QStringLiteral("turn_tcp"))
    {
        LOG_WARN("Ignored unknown network path: {}", networkPath);
        return;
    }

    if (m_networkPath == normalized)
    {
        LOG_INFO("Network path unchanged: {}", normalized);
        return;
    }

    m_networkPath = normalized;
    LOG_INFO("Network path switched to {}, rebuilding PeerConnection after remote session drain", m_networkPath);
    publishNetworkPathState();

    m_connected = false;
    m_reconnectPending.store(false);
    if (m_reconnectTimer)
        m_reconnectTimer->stop();

    // Drain the controlled-side peer before rebuilding with the new path;
    // otherwise the next CONNECT can overlap the old active WebRtcCli.
    sendDisconnectSignal(QStringLiteral("network_path_change"));
    destroy();
    m_pendingNetworkPathReconnect = m_networkPath;
    const quint64 reconnectGeneration = ++m_networkPathReconnectGeneration;
    QTimer::singleShot(1200, this, [this, reconnectGeneration]() {
        if (reconnectGeneration != m_networkPathReconnectGeneration)
            return;
        restartAfterNetworkPathChange();
    });
}

void WebRtcCtl::restartAfterNetworkPathChange()
{
    const QString requestedPath = m_pendingNetworkPathReconnect;
    if (m_shutdownDone || m_networkPath != requestedPath)
        return;

    if (!m_filePacketUtil)
    {
        m_filePacketUtil = std::make_unique<FilePacketUtil>(this);
        connectFilePacketUtilSignals();
    }

    try
    {
        init();
        LOG_INFO("Network path reconnect started with path={}", m_networkPath);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Network path reconnect failed to start: {}", e.what());
        scheduleReconnect();
    }
    catch (...)
    {
        LOG_ERROR("Network path reconnect failed to start: unknown error");
        scheduleReconnect();
    }
}


void WebRtcCtl::doReconnect()
{
    m_reconnectPending.store(false);

    if (!m_allowReconnect || m_shutdownStarted.load())
    {
        LOG_DEBUG("Skipping reconnect because the session is closing or reconnect is disabled");
        return;
    }

    LOG_INFO("Reconnect attempt {} starting", m_reconnectAttempts);

    
    m_connected = false;
    setSessionHealth(2, tr("Connection lost, reconnecting..."));
    destroy();

    
    if (!m_filePacketUtil)
    {
        m_filePacketUtil = std::make_unique<FilePacketUtil>(this);
        connectFilePacketUtilSignals();
    }

    
    try
    {
        init();
        LOG_INFO("Reconnect attempt {}: init() invoked", m_reconnectAttempts);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception during reconnect init: {}", e.what());
        scheduleReconnect();
    }
    catch (...)
    {
        LOG_ERROR("Unknown exception during reconnect init");
        scheduleReconnect();
    }
}
