#include "desktop_capture_manager.h"
#include "desktop_capture_worker.h"
#include <QGuiApplication>
#include <QScreen>

DesktopCaptureManager *DesktopCaptureManager::instance()
{
    static DesktopCaptureManager s_instance(nullptr);
    return &s_instance;
}

DesktopCaptureManager::DesktopCaptureManager(QObject *parent) : QObject(parent)
{
}

DesktopCaptureManager::~DesktopCaptureManager()
{
    for (auto it = m_workers.begin(); it != m_workers.end(); ++it)
    {
        const int screenIndex = it.key();
        DesktopCaptureWorker *worker = it.value();
        if (!worker)
        {
            continue;
        }

        QThread *workerThread = m_workerThreads.value(screenIndex, nullptr);
        if (worker->thread() == QThread::currentThread())
        {
            worker->stopCapture();
        }
        else if (workerThread && workerThread->isRunning())
        {
            // stopCapture 内会操作 QTimer，必须在 worker 所在线程执行
            QMetaObject::invokeMethod(worker, "stopCapture", Qt::BlockingQueuedConnection);
        }
        else
        {
            worker->stopCapture();
        }

        if (workerThread && workerThread->isRunning() && worker->thread() == workerThread)
        {
            // 在 worker 所在线程中销毁 QObject，避免 QTimer/线程亲和性告警
            QMetaObject::invokeMethod(worker, [worker]() {
                worker->disconnect();
                delete worker;
            },
                                      Qt::BlockingQueuedConnection);
        }
        else
        {
            worker->disconnect();
            delete worker;
        }

        it.value() = nullptr;
    }

    for (QThread *workerThread : m_workerThreads)
    {
        STOP_PTR_THREAD(workerThread);
        delete workerThread;
    }

    m_workers.clear();
    m_workerThreads.clear();
}

bool DesktopCaptureManager::subscribe(const QString &subscriberId, int screenIndex, int dstW, int dstH, int fps)
{
    QMutexLocker locker(&m_mutex);

    if (subscriberId.isEmpty())
    {
        LOG_ERROR("subscribe failed: empty subscriberId");
        return false;
    }
    if (m_workerThreads.contains(screenIndex) == false)
    {
        // 创建工作线程
        QThread *workerThread = new QThread();
        workerThread->setObjectName(QString("DesktopCaptureWorkerThread-Screen%1").arg(screenIndex));
        m_workerThreads.insert(screenIndex, workerThread);
        workerThread->start();
        LOG_INFO("Created worker thread for screenIndex {}", screenIndex);
    }
    if (!m_workers.contains(screenIndex))
    {
        // 创建工作对象
        DesktopCaptureWorker *worker = new DesktopCaptureWorker();
        m_workers.insert(screenIndex, worker);
        // 连接信号
        connect(worker, &DesktopCaptureWorker::frameEncoded,
                this, &DesktopCaptureManager::onWorkerFrameEncoded, Qt::QueuedConnection);

        connect(worker, &DesktopCaptureWorker::errorOccurred,
                this, &DesktopCaptureManager::onWorkerError, Qt::QueuedConnection);
        // 将工作对象移到工作线程
        worker->moveToThread(m_workerThreads.value(screenIndex));
        // 启动线程
        LOG_INFO("DesktopCaptureManager initialized, worker thread started for screenIndex {}", screenIndex);
        QMetaObject::invokeMethod(worker, "initialize", Qt::QueuedConnection,
                                  Q_ARG(int, screenIndex),
                                  Q_ARG(int, fps));
    }

    const bool already = m_subscribers.contains(subscriberId);
    const bool needStart = m_subscribers.isEmpty();

    if (!already)
    {
        m_subscribers.insert(subscriberId);
    }

    LOG_INFO("Subscriber {} subscribe ({}x{} @ {}fps), totalSubscribers={}, already={}",
             subscriberId, dstW, dstH, fps, m_subscribers.size(), already);

    if (already)
    {
        // 同一个控制端重连 id 不变：这里应该更新参数，而不是重复 add
        QMetaObject::invokeMethod(m_workers.value(screenIndex), "updateSubscriber", Qt::QueuedConnection,
                                  Q_ARG(QString, subscriberId),
                                  Q_ARG(int, dstW),
                                  Q_ARG(int, dstH),
                                  Q_ARG(int, fps));
    }
    else
    {
        QMetaObject::invokeMethod(m_workers.value(screenIndex), "addSubscriber", Qt::QueuedConnection,
                                  Q_ARG(QString, subscriberId),
                                  Q_ARG(int, dstW),
                                  Q_ARG(int, dstH),
                                  Q_ARG(int, fps));
    }

    return true;
}

void DesktopCaptureManager::unsubscribe(const QString &subscriberId, int screenIndex)
{
    QMutexLocker locker(&m_mutex);

    if (!m_subscribers.contains(subscriberId))
    {
        LOG_WARN("unsubscribe ignored: subscriber {} not found (totalSubscribers={})", subscriberId, m_subscribers.size());
        return;
    }

    m_subscribers.remove(subscriberId);

    LOG_INFO("Subscriber {} unsubscribe, totalSubscribers={}", subscriberId, m_subscribers.size());

    QMetaObject::invokeMethod(m_workers.value(screenIndex), "removeSubscriber", Qt::QueuedConnection,
                              Q_ARG(QString, subscriberId));
}

int DesktopCaptureManager::subscriberCount()
{
    QMutexLocker locker(&m_mutex);
    return m_subscribers.size();
}

void DesktopCaptureManager::onWorkerFrameEncoded(const QString &subscriberId, std::shared_ptr<rtc::binary> encodedData, quint64 timestamp_us)
{
    LOG_TRACE("DesktopCaptureManager::onWorkerFrameEncoded invoked for {} size={}", subscriberId,
              encodedData ? (int)encodedData->size() : -1);
    // 直接转发信号（从工作线程到主线程）
    emit frameEncoded(subscriberId, encodedData, timestamp_us);
}

void DesktopCaptureManager::onWorkerError(const QString &errorMessage)
{
    LOG_ERROR("Worker error: {}", errorMessage);
    emit captureError(errorMessage);
}