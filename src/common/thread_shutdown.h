#ifndef AIRAN_THREAD_SHUTDOWN_H
#define AIRAN_THREAD_SHUTDOWN_H

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QThread>

#include "logger_manager.h"

namespace ThreadShutdown
{

// QThread::quit() is thread-safe: it only requests the target event loop to exit.
// A DirectConnection from a worker completion signal to QThread::quit is safe.
// This does not make arbitrary QThread/QObject methods safe for direct cross-thread calls,
// and the QThread object must not be deleted until isRunning() becomes false.
inline bool waitForThread(QThread &thread, int timeoutMs = 5000, int pumpSliceMs = 50)
{
    if (!thread.isRunning())
        return true;

    QDeadlineTimer deadline(timeoutMs);
    while (thread.isRunning() && !deadline.hasExpired())
    {
        thread.wait(pumpSliceMs);
        if (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread())
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, qMin(pumpSliceMs, 10));
    }
    return !thread.isRunning();
}

inline bool shutdownThread(QThread &thread, const char *tag, int timeoutMs = 5000)
{
    if (!thread.isRunning())
        return true;

    const QByteArray name = (tag && *tag) ? QByteArray(tag) : thread.objectName().toUtf8();
    LOG_INFO("{} thread stopping", name.constData());
    thread.quit();

    const bool stopped = waitForThread(thread, timeoutMs);
    if (!stopped)
    {
        LOG_ERROR("{} thread did not quit within {} ms; leaving it running for diagnostics", name.constData(), timeoutMs);
        return false;
    }

    LOG_INFO("{} thread stopped", name.constData());
    return true;
}

inline bool shutdownThread(QThread *thread, const char *tag, int timeoutMs = 5000)
{
    if (!thread)
        return true;
    return shutdownThread(*thread, tag, timeoutMs);
}

} // namespace ThreadShutdown

#endif /* AIRAN_THREAD_SHUTDOWN_H */
