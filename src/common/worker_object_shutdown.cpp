#include "common/worker_object_shutdown.h"

#include "common/logger_manager.h"
#include "common/thread_shutdown.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <memory>

bool WorkerObjectShutdown::shutdownAndDelete(QObject *worker,
                                             QThread *thread,
                                             QObject *stableOwner,
                                             const char *shutdownMethod,
                                             const QString &tag,
                                             int timeoutMs)
{
    if (!worker)
        return !thread || !thread->isRunning();
    if (!thread || !stableOwner || !shutdownMethod || !*shutdownMethod)
    {
        LOG_ERROR("{} shutdown cannot be completed because its lifecycle metadata is incomplete", tag);
        return false;
    }

    const QPointer<QObject> guard(worker);
    QThread *const ownerThread = stableOwner->thread();
    const QMetaObject::Connection affinityRecovery = QObject::connect(
        thread,
        &QThread::finished,
        thread,
        [guard, ownerThread]() {
            if (guard && ownerThread && guard->thread() == QThread::currentThread())
                guard->moveToThread(ownerThread);
        },
        Qt::DirectConnection);

    if (!thread->isRunning())
        thread->start();

    const bool invoked = QMetaObject::invokeMethod(worker,
                                                    shutdownMethod,
                                                    Qt::QueuedConnection,
                                                    Q_ARG(QObject *, stableOwner));
    if (!invoked)
    {
        LOG_ERROR("{} shutdown method could not be queued", tag);
        thread->quit();
    }

    if (!ThreadShutdown::waitForThread(*thread, timeoutMs))
    {
        LOG_ERROR("{} did not finish within {} ms; cleanup remains attached to the worker thread", tag, timeoutMs);
        const auto cleaned = std::make_shared<std::atomic_bool>(false);
        const auto cleanup = [guard, thread, stableOwner, cleaned, tag]() {
            if (cleaned->exchange(true))
                return;
            if (guard)
            {
                if (guard->thread() == QThread::currentThread())
                    delete guard.data();
                else
                    LOG_ERROR("{} could not be deleted on its owner thread after shutdown", tag);
            }
            thread->deleteLater();
        };
        QObject::connect(thread, &QThread::finished,
                         stableOwner, cleanup, Qt::QueuedConnection);
        if (!thread->isRunning())
            QTimer::singleShot(0, stableOwner, cleanup);
        return false;
    }

    QObject::disconnect(affinityRecovery);
    if (!guard)
        return true;
    if (guard->thread() != QThread::currentThread())
    {
        LOG_ERROR("{} stopped without returning to the owner thread", tag);
        return false;
    }

    delete guard.data();
    return true;
}
