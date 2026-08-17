#include "ui/main/main_window.h"

#include "ui/control/control_window.h"
#include "ui/transfer/file_transfer_window.h"
#include "ui/terminal/terminal_window.h"
#include "webrtc/cli/lifecycle/webrtc_cli_session_shutdown.h"

#include <QMetaObject>
#include <QPointer>
#include <QThread>

void MainWindow::cleanupWebRtcCliSessions()
{
    if (m_rtcCliSessions.isEmpty())
    {
        return;
    }

    auto sessions = m_rtcCliSessions;
    m_rtcCliSessions.clear();

    for (auto it = sessions.begin(); it != sessions.end(); ++it)
    {
        WebRtcCli *webrtcCli = it.key();
        QThread *rtcCliThread = it.value();

        if (m_ws && webrtcCli)
            QObject::disconnect(m_ws, nullptr, webrtcCli, nullptr);

        if (!webrtcCli)
        {
            if (rtcCliThread)
            {
                if (ThreadShutdown::shutdownThread(rtcCliThread, rtcCliThread->objectName().toUtf8().constData()))
                    delete rtcCliThread;
            }
            continue;
        }

        const bool stopped = WebRtcCliSessionShutdown::shutdown(webrtcCli, rtcCliThread);
        if (stopped && rtcCliThread)
            delete rtcCliThread;
    }

    LOG_INFO("All WebRtcCli sessions cleaned up");
}

void MainWindow::handleAuditFailure(const QString &reason)
{
    Q_UNUSED(reason);
    for (auto it = m_rtcCliSessions.begin(); it != m_rtcCliSessions.end(); ++it)
    {
        if (it.key())
            QMetaObject::invokeMethod(it.key(), "requestDisconnect", Qt::QueuedConnection,
                                      Q_ARG(QString, QStringLiteral("audit_failure")));
    }
}


void MainWindow::connFileMgr(const QString &remote_id, const QString &remote_pwd_md5)
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    FileTransferWindow *fw = new FileTransferWindow(remote_id, remote_pwd_md5, m_ws);
    fw->showMaximized();
}


void MainWindow::connDesktopMgr(const QString &remote_id, const QString &remote_pwd_md5)
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    ControlWindow *cw = new ControlWindow(remote_id, remote_pwd_md5, m_ws);
    cw->show();
}


void MainWindow::connTerminalMgr(const QString &remote_id, const QString &remote_pwd_md5)
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    TerminalWindow *tw = new TerminalWindow(remote_id, remote_pwd_md5, m_ws);
    tw->show();
}


void MainWindow::onDestroyWebRtcCli()
{
    destroyWebRtcCli(qobject_cast<WebRtcCli *>(sender()));
}


void MainWindow::destroyWebRtcCli(WebRtcCli *webrtcCli)
{
    if (webrtcCli == nullptr)
    {
        LOG_WARN("Ignoring delayed WebRtcCli destroy callback without a live session");
        return;
    }

    const auto sessionIt = m_rtcCliSessions.find(webrtcCli);
    if (sessionIt == m_rtcCliSessions.end())
        return;
    if (m_rtcCliShutdownPending.contains(webrtcCli))
        return;

    QThread *rtcCliThread = sessionIt.value();
    QString senderName = rtcCliThread ? rtcCliThread->objectName() : QString("unknown");
    LOG_INFO("Starting destroyCli for {}", senderName);

    if (m_ws)
        QObject::disconnect(m_ws, nullptr, webrtcCli, nullptr);
    webrtcCli->requestShutdown();
    m_rtcCliShutdownPending.insert(webrtcCli);

    if (!rtcCliThread)
    {
        m_rtcCliShutdownPending.remove(webrtcCli);
        m_rtcCliSessions.remove(webrtcCli);
        delete webrtcCli;
        return;
    }

    // A worker can finish before its queued destroy callback is delivered.
    // In that case QThread::finished has already fired, so installing a new
    // finished handler would leak the session and keep it counted forever.
    if (!rtcCliThread->isRunning())
    {
        const bool stopped = WebRtcCliSessionShutdown::shutdown(webrtcCli, rtcCliThread);
        m_rtcCliShutdownPending.remove(webrtcCli);
        m_rtcCliSessions.remove(webrtcCli);
        if (stopped)
            delete rtcCliThread;
        return;
    }

    const QPointer<WebRtcCli> guard(webrtcCli);
    QThread *const ownerThread = thread();
    const QMetaObject::Connection affinityRecovery = QObject::connect(
        rtcCliThread,
        &QThread::finished,
        rtcCliThread,
        [guard, ownerThread]() {
            if (guard && ownerThread && guard->thread() == QThread::currentThread())
                guard->moveToThread(ownerThread);
        },
        Qt::DirectConnection);
    QObject::connect(rtcCliThread, &QThread::finished, this,
                     [this, webrtcCli, guard, rtcCliThread, affinityRecovery]() {
                         QObject::disconnect(affinityRecovery);
                         if (m_rtcCliSessions.value(webrtcCli) == rtcCliThread)
                             m_rtcCliSessions.remove(webrtcCli);
                         m_rtcCliShutdownPending.remove(webrtcCli);
                         if (guard)
                             guard->deleteLater();
                         rtcCliThread->deleteLater();
                     },
                     Qt::QueuedConnection);
    if (!QMetaObject::invokeMethod(webrtcCli,
                                   "shutdownAndMoveToOwnerThread",
                                   Qt::QueuedConnection,
                                   Q_ARG(QObject *, this)))
    {
        LOG_ERROR("Failed to queue asynchronous destroyCli for {}", senderName);
        rtcCliThread->quit();
    }
    LOG_INFO("Finished destroyCli for {}", senderName);
}
