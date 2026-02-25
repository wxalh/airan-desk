#ifndef DESKTOP_CAPTURE_MANAGER_H
#define DESKTOP_CAPTURE_MANAGER_H

#include <QObject>
#include <QMutex>
#include <QTimer>
#include <QMap>
#include <QThread>
#include <QSet>
#include <memory>
#include "../common/constant.h"
// Forward declarations
class DesktopCaptureWorker;

/**
 * @brief 桌面捕获管理器（单例）
 *
 * 线程模型：
 * - 主线程：DesktopCaptureManager、WebRtcCli
 * - 工作线程：DesktopCaptureWorker（DesktopGrab + 所有编码器）
 *
 * 职责：
 * 1. 全局唯一的屏幕捕获实例（DXGI 或 Qt）
 * 2. 管理多个编码器订阅者
 * 3. 每次捕获后分发给所有订阅者
 *
 * 优势：
 * - 避免多次重复捕获（DXGI 捕获很昂贵）
 * - 支持不同分辨率的多路编码
 * - 订阅者动态增删
 * - 耗时操作在工作线程，不阻塞主线程
 */
class DesktopCaptureManager : public QObject
{
    Q_OBJECT
public:
    static DesktopCaptureManager *instance();

    /**
     * @brief 订阅帧数据
     * @param subscriberId 订阅者唯一标识（如 WebRtcCli 的 remoteId）
     * @param screenIndex 屏幕索引（默认0）
     * @param dstW 目标宽度
     * @param dstH 目标高度
     * @param fps 目标帧率
     * @return 是否订阅成功
     */
    bool subscribe(const QString &subscriberId, int screenIndex, int dstW, int dstH, int fps);

    /**
     * @brief 取消订阅
     * @param subscriberId 订阅者标识
     * @param screenIndex 屏幕索引（默认0）
     */
    void unsubscribe(const QString &subscriberId, int screenIndex);

    /**
     * @brief 获取当前订阅者数量
     */
    int subscriberCount();

signals:
    /**
     * @brief 编码完成的帧数据
     * @param subscriberId 订阅者ID
     * @param encodedData H264编码数据
     * @param timestamp_us 时间戳（微秒）
     */
    void frameEncoded(const QString &subscriberId, std::shared_ptr<rtc::binary> encodedData, quint64 timestamp_us);

    /**
     * @brief 捕获错误信号
     */
    void captureError(const QString &errorMessage);

public slots:
    void onWorkerFrameEncoded(const QString &subscriberId, std::shared_ptr<rtc::binary> encodedData, quint64 timestamp_us);
    void onWorkerError(const QString &errorMessage);

private:
    explicit DesktopCaptureManager(QObject *parent = nullptr);
    ~DesktopCaptureManager();
    Q_DISABLE_COPY(DesktopCaptureManager)

    QMutex m_mutex;

    // 工作线程
    QMap<int, QThread *> m_workerThreads;        // key: screenIndex
    QMap<int, DesktopCaptureWorker *> m_workers; // key: screenIndex

    QSet<QString> m_subscribers;
};

#endif // DESKTOP_CAPTURE_MANAGER_H
