#include "util/qt/qt_callback_util.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>

namespace
{
constexpr int kMaxCallbacksPerDrain = 256;
constexpr qint64 kMaxDrainDurationMs = 4;
}

class QtCallbackDispatcherPrivate
{
public:
    QMutex mutex;
    QQueue<std::function<void()>> callbacks;
    bool drainScheduled = false;
};

QtCallbackDispatcher::QtCallbackDispatcher(QObject *parent)
    : QObject(parent), d(new QtCallbackDispatcherPrivate)
{
}

QtCallbackDispatcher::~QtCallbackDispatcher()
{
    delete d;
}

void QtCallbackDispatcher::post(std::function<void()> callback)
{
    if (!callback)
        return;

    bool scheduleDrain = false;
    {
        QMutexLocker locker(&d->mutex);
        d->callbacks.enqueue(std::move(callback));
        if (!d->drainScheduled)
        {
            d->drainScheduled = true;
            scheduleDrain = true;
        }
    }

    if (scheduleDrain && !QMetaObject::invokeMethod(this, "drain", Qt::QueuedConnection))
    {
        QMutexLocker locker(&d->mutex);
        d->callbacks.clear();
        d->drainScheduled = false;
    }
}

void QtCallbackDispatcher::drain()
{
    QElapsedTimer elapsed;
    elapsed.start();

    int drained = 0;
    while (drained < kMaxCallbacksPerDrain && elapsed.elapsed() < kMaxDrainDurationMs)
    {
        std::function<void()> callback;
        {
            QMutexLocker locker(&d->mutex);
            if (d->callbacks.isEmpty())
            {
                d->drainScheduled = false;
                return;
            }
            callback = d->callbacks.dequeue();
        }

        if (callback)
            callback();
        ++drained;
    }

    bool scheduleAgain = false;
    {
        QMutexLocker locker(&d->mutex);
        if (d->callbacks.isEmpty())
            d->drainScheduled = false;
        else
            scheduleAgain = true;
    }

    if (scheduleAgain && !QMetaObject::invokeMethod(this, "drain", Qt::QueuedConnection))
    {
        QMutexLocker locker(&d->mutex);
        d->callbacks.clear();
        d->drainScheduled = false;
    }
}
