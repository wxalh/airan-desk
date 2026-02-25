#ifndef DESKTOP_CAPTURE_WORKER_H
#define DESKTOP_CAPTURE_WORKER_H

#include <QObject>
#include <QMutex>
#include <QTimer>
#include <QMap>
#include <QThread>
#include <QElapsedTimer>
#include <memory>
#include "../codec/h264_encoder.h"

// Forward declarations
class DesktopGrab;

/**
 * @brief 捕获工作线程（运行在独立线程）
 *
 * 职责：
 * - 执行 DXGI/Qt 屏幕捕获
 * - 管理所有编码器实例
 * - 执行 H264 编码
 * - 发出编码完成信号
 */
class DesktopCaptureWorker : public QObject
{
    Q_OBJECT
public:
    explicit DesktopCaptureWorker(QObject *parent = nullptr);
    ~DesktopCaptureWorker();

public slots:
    /**
     * @brief 初始化捕获器
     */
    void initialize(int screenIndex, int fps);

    /**
     * @brief 添加订阅者
     */
    void addSubscriber(const QString &subscriberId, int dstW, int dstH, int fps);

    /**
     * @brief 移除订阅者
     */
    void removeSubscriber(const QString &subscriberId);

    /**
     * @brief 更新订阅者参数
     */
    void updateSubscriber(const QString &subscriberId, int dstW, int dstH, int fps);

    /**
     * @brief 停止采集并释放运行期资源
     */
    void stopCapture();

    /**
     * @brief 重新平衡捕获帧率
     */
    void reBalanceCaptureFps();
signals:
    /**
     * @brief 编码完成信号
     */
    void frameEncoded(const QString &subscriberId, std::shared_ptr<rtc::binary> encodedData, quint64 timestamp_us);

    /**
     * @brief 错误信号
     */
    void errorOccurred(const QString &errorMessage);

private slots:
    void onTimeout();

private:
    struct SubscriberInfo;

    bool captureAndEncodeGPU();
    bool captureAndEncodeCPU();
    bool ensureDesktopGrabberLocked();
#if !defined(AIRAN_COMPATIBLE_WIN7) && ( defined(Q_OS_WIN64) || defined(Q_OS_WIN32) )
    bool scaleTextureForSubscriberLocked(ID3D11Texture2D *srcTexture, SubscriberInfo *subscriber, ID3D11Texture2D *&outTexture);
#endif

    void updateCaptureTimerLocked(int fps);

    struct SubscriberInfo
    {
        QString id;
        int dstW;
        int dstH;
        int fps;
        std::unique_ptr<H264Encoder> encoder;
#if !defined(AIRAN_COMPATIBLE_WIN7) && ( defined(Q_OS_WIN64) || defined(Q_OS_WIN32) )
        ComPtr<ID3D11Device> scaleDevice;
        ComPtr<ID3D11VideoDevice> scaleVideoDevice;
        ComPtr<ID3D11VideoContext> scaleVideoContext;
        ComPtr<ID3D11VideoProcessorEnumerator> scaleVpEnum;
        ComPtr<ID3D11VideoProcessor> scaleProcessor;
        ComPtr<ID3D11Texture2D> scaleInputTex;
        ComPtr<ID3D11VideoProcessorInputView> scaleInputView;
        ComPtr<ID3D11Texture2D> scaleOutputTex;
        ComPtr<ID3D11VideoProcessorOutputView> scaleOutputView;
        UINT scaleSrcW{0};
        UINT scaleSrcH{0};
        DXGI_FORMAT scaleSrcFormat{DXGI_FORMAT_UNKNOWN};
#endif
    };

    QMutex m_mutex;
    QTimer *m_timer = nullptr;
    int m_screenIndex{0};
    int m_captureFps{30}; // 当前采集帧率（取订阅者中的最大值）
    QElapsedTimer m_gpuLogTimer;
    qint64 m_lastGpuGrabWarnMs{0};
    std::shared_ptr<DesktopGrab> m_desktopGrab;
    QMap<QString, SubscriberInfo *> m_subscribers;
};

#endif // DESKTOP_CAPTURE_MANAGER_H
