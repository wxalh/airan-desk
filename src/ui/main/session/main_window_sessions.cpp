#include "ui/main/main_window.h"

#include "ui/control/control_window.h"
#include "ui/transfer/file_transfer_window.h"
#include "ui/terminal/terminal_window.h"
#include "webrtc/cli/lifecycle/webrtc_cli_session_shutdown.h"

#include <QMetaObject>
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
    WebRtcCli *webrtcCli = static_cast<WebRtcCli *>(sender());
    if (webrtcCli == nullptr)
    {
        LOG_ERROR("webrtc_cli is nullptr in onDestroyWebRtcCli");
        return;
    }

    const auto sessionIt = m_rtcCliSessions.find(webrtcCli);
    if (sessionIt == m_rtcCliSessions.end())
        return;

    QThread *rtcCliThread = sessionIt.value();
    QString senderName = rtcCliThread ? rtcCliThread->objectName() : QString("unknown");
    LOG_INFO("Starting destroyCli for {}", senderName);

    m_rtcCliSessions.erase(sessionIt);

    const bool stopped = WebRtcCliSessionShutdown::shutdown(webrtcCli, rtcCliThread);
    if (stopped && rtcCliThread)
        delete rtcCliThread;
    LOG_INFO("Finished destroyCli for {}", senderName);
}
