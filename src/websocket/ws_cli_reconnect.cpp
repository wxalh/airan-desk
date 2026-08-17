#include "ws_cli.h"


void WsCli::scheduleReconnect()
{
    if (m_shutdownDone)
    {
        LOG_DEBUG("Shutdown in progress, skipping reconnect");
        return;
    }
    if (m_connected)
    {
        LOG_DEBUG("Already connected, no need to reconnect");
        return;
    }
    if (!isSupportedSignalingUrl(m_url))
    {
        LOG_DEBUG("Signaling URL is unavailable, skipping reconnect");
        return;
    }

    int delay = 1000;
    QString phaseDesc;

    switch (m_reconnect_phase)
    {
    case 0:
        delay = 1000;
        phaseDesc = tr("Fast reconnect");
        break;
    case 1:
        delay = 10000;
        phaseDesc = tr("Medium reconnect");
        break;
    case 2:
        delay = 30000;
        phaseDesc = tr("Slow reconnect");
        break;
    case 3:
    default:
        delay = 60000;
        phaseDesc = tr("Long reconnect");
        break;
    }

    LOG_DEBUG("Scheduling reconnect in {}ms (phase: {}, attempt: {})",
              delay, m_reconnect_phase, m_reconnect_count + 1);

    const QString status = tr("%1 phase, reconnecting in %2 seconds...").arg(phaseDesc).arg(delay / 1000);
    emit onReconnectStatusUpdate(status, m_reconnect_phase, m_reconnect_count + 1, delay / 1000);

    emit startReconnectTimer(delay);
}


void WsCli::attemptReconnect()
{
    LOG_DEBUG("attemptReconnect() called");

    if (m_shutdownDone)
    {
        LOG_DEBUG("Shutdown in progress, aborting reconnect attempt");
        return;
    }
    if (m_connected)
    {
        LOG_DEBUG("Already connected, stopping reconnect attempts");
        return;
    }
    if (!isSupportedSignalingUrl(m_url))
    {
        LOG_DEBUG("Signaling URL is unavailable, aborting reconnect attempt");
        return;
    }

    if (m_ws)
    {
        const auto socketState = m_ws->state();
        if (socketState == QAbstractSocket::HostLookupState ||
            socketState == QAbstractSocket::ConnectingState)
        {
            LOG_DEBUG("Reconnect attempt skipped because the WebSocket handshake is already in flight");
            m_reconnect_followup_timer->start(1000);
            return;
        }
        if (socketState != QAbstractSocket::UnconnectedState)
        {
            LOG_DEBUG("Reconnect attempt skipped because the WebSocket is changing state: {}",
                      static_cast<int>(socketState));
            m_reconnect_followup_timer->start(1000);
            return;
        }
    }

    ++m_reconnect_count;

    LOG_INFO("Attempting reconnect (phase: {}, attempt: {})", m_reconnect_phase, m_reconnect_count);

    const QString status = tr("Trying to reconnect... (attempt %1)").arg(m_reconnect_count);
    emit onReconnectStatusUpdate(status, m_reconnect_phase, m_reconnect_count, 0);

    if (m_ws)
    {
        LOG_DEBUG("Calling m_ws->open() to reconnect");
        m_socketConnectTimer.start();
        m_ws->open(m_url);
    }
    else
    {
        LOG_ERROR("m_ws is null, cannot reconnect");
        return;
    }

    if (m_reconnect_count >= MAX_RETRY_PER_PHASE && m_reconnect_phase < 3)
    {
        ++m_reconnect_phase;
        m_reconnect_count = 0;
        LOG_DEBUG("Moving to reconnect phase {}", m_reconnect_phase);
    }
    else if (m_reconnect_phase == 3 && m_reconnect_count >= MAX_RETRY_PER_PHASE)
    {
        m_reconnect_count = 0;
        LOG_DEBUG("Phase 3: Resetting retry count for continuous attempts");
    }

    m_reconnect_followup_timer->start(2000);
}


void WsCli::scheduleNextReconnectIfNeeded()
{
    LOG_DEBUG("Checking if need to schedule next reconnect");
    if (m_shutdownDone)
    {
        LOG_DEBUG("Shutdown in progress, skipping next reconnect");
        return;
    }
    if (!isSupportedSignalingUrl(m_url))
    {
        LOG_DEBUG("Signaling URL is unavailable, skipping next reconnect");
        return;
    }
    if (!m_connected)
    {
        if (m_ws)
        {
            const auto socketState = m_ws->state();
            if (socketState == QAbstractSocket::HostLookupState ||
                socketState == QAbstractSocket::ConnectingState)
            {
                if (m_socketConnectTimer.isValid() &&
                    m_socketConnectTimer.elapsed() >= 10000)
                {
                    LOG_WARN("WebSocket handshake remained in progress for {} ms; aborting and scheduling a fresh reconnect",
                             m_socketConnectTimer.elapsed());
                    m_socketConnectTimer.invalidate();
                    m_ws->abort();
                    scheduleReconnect();
                    return;
                }
                // Do not call open() again while Qt is still completing the
                // current handshake. Keep polling until it connects or emits
                // disconnected; the latter schedules a fresh attempt.
                m_reconnect_followup_timer->start(1000);
                return;
            }
            if (socketState != QAbstractSocket::UnconnectedState)
            {
                m_reconnect_followup_timer->start(1000);
                return;
            }
        }
        LOG_DEBUG("Still not connected, scheduling next reconnect");
        scheduleReconnect();
    }
    else
    {
        LOG_DEBUG("Connected successfully, stopping reconnect attempts");
    }
}
