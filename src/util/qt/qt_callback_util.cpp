#include "util/qt/qt_callback_util.h"

#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>

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
    QQueue<std::function<void()>> callbacks;
    {
        QMutexLocker locker(&d->mutex);
        callbacks.swap(d->callbacks);
        d->drainScheduled = false;
    }

    while (!callbacks.isEmpty())
    {
        std::function<void()> callback = callbacks.dequeue();
        if (callback)
            callback();
    }
}
