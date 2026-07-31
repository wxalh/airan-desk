#include "ui/main/main_window.h"
#include "common/constant.h"
#include "common/thread_shutdown.h"
#include "common/worker_object_shutdown.h"
#include "security/audit_logger.h"
#include "ui_main_window.h"
#include <QApplication>
#include <QCoreApplication>
#include <QThread>
#include <QPointer>

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <windows.h>
#include <wtsapi32.h>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent),
      windowTitle(tr("AiRan")),
      textToCopy(tr("Welcome to %1 remote tool. Your ID: %2\nVerification code: %3")),
      isCaptureing(false)
{
    if (RuntimeEnvironment::uiAvailable())
    {
        initUI();
        initTray();
        initDesktopScreenChangeMonitor();
    }
    initCli();
    QPointer<MainWindow> guard(this);
    AuditLogger::instance().setFailureHandler([guard](const QString &reason) {
        if (guard)
            QMetaObject::invokeMethod(guard.data(), "handleAuditFailure", Qt::QueuedConnection,
                                      Q_ARG(QString, reason));
    });
}

MainWindow::~MainWindow()
{
    AuditLogger::instance().setFailureHandler(AuditLogger::FailureHandler());
    cleanupTray();
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    if (ui)
        WTSUnRegisterSessionNotification(reinterpret_cast<HWND>(winId()));
#endif
    disconnect();
    cleanupWebRtcCliSessions();
    if (m_ws)
    {
        WsCli *ws = m_ws;
        QThread *thread = m_ws_thread;
        m_ws = nullptr;
        m_ws_thread = nullptr;
        const bool stopped = WorkerObjectShutdown::shutdownAndDelete(
            ws,
            thread,
            QCoreApplication::instance(),
            "shutdownAndMoveToOwnerThread",
            QStringLiteral("MainWindow WsCli"));
        if (stopped)
            delete thread;
    }
    else if (m_ws_thread)
    {
        QThread *thread = m_ws_thread;
        m_ws_thread = nullptr;
        if (ThreadShutdown::shutdownThread(thread, "MainWindow WsCli"))
            delete thread;
    }
    delete ui;
}
