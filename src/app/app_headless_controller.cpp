#include "app/app_headless_controller.h"

#include <QCoreApplication>
#include "common/thread_shutdown.h"
#include "common/worker_object_shutdown.h"
#include "security/audit_logger.h"
#include <QMetaObject>
#include <QThread>
#include <QPointer>


HeadlessController::HeadlessController(QObject *parent)
    : QObject(parent)
{
    m_ws = new WsCli();
    m_wsThread = new QThread();

    connect(m_ws, &WsCli::onWsCliRecvBinaryMsg, this, &HeadlessController::onWsCliRecvBinaryMsg);
    connect(m_ws, &WsCli::onWsCliRecvTextMsg, this, [this](const QString &message)
            { onWsCliRecvBinaryMsg(message.toUtf8()); });
    connect(m_ws, &WsCli::shutdownFinished, m_wsThread, &QThread::quit, Qt::DirectConnection);
    m_wsThread->setObjectName(QStringLiteral("WsCliThread"));
    m_ws->moveToThread(m_wsThread);
    m_wsThread->start();
    QMetaObject::invokeMethod(m_ws, "init", Qt::QueuedConnection,
                              Q_ARG(QString, buildWsUrl()),
                              Q_ARG(quint64, 30 * 1000));
    QPointer<HeadlessController> guard(this);
    AuditLogger::instance().setFailureHandler([guard](const QString &reason) {
        if (guard)
            QMetaObject::invokeMethod(guard.data(), "handleAuditFailure", Qt::QueuedConnection,
                                      Q_ARG(QString, reason));
    });
}


HeadlessController::~HeadlessController()
{
    AuditLogger::instance().setFailureHandler(AuditLogger::FailureHandler());
    m_shuttingDown = true;
    if (m_ws)
        QObject::disconnect(m_ws, nullptr, this, nullptr);
    cleanupWebRtcCliSessions();
    if (m_ws)
    {
        WsCli *ws = m_ws;
        QThread *thread = m_wsThread;
        m_ws = nullptr;
        m_wsThread = nullptr;
        const bool stopped = WorkerObjectShutdown::shutdownAndDelete(
            ws,
            thread,
            QCoreApplication::instance(),
            "shutdownAndMoveToOwnerThread",
            QStringLiteral("Headless WsCli"));
        if (stopped)
            delete thread;
    }
    else if (m_wsThread)
    {
        QThread *thread = m_wsThread;
        m_wsThread = nullptr;
        if (ThreadShutdown::shutdownThread(thread, "Headless WsCli"))
            delete thread;
    }
}
