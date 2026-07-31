#ifndef AIRAN_WORKER_OBJECT_SHUTDOWN_H
#define AIRAN_WORKER_OBJECT_SHUTDOWN_H

#include <QString>

class QObject;
class QThread;

namespace WorkerObjectShutdown
{

bool shutdownAndDelete(QObject *worker,
                       QThread *thread,
                       QObject *stableOwner,
                       const char *shutdownMethod,
                       const QString &tag,
                       int timeoutMs = 5000);

} // namespace WorkerObjectShutdown

#endif /* AIRAN_WORKER_OBJECT_SHUTDOWN_H */
