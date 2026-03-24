#ifndef WEBRTC_CTL_H
#define WEBRTC_CTL_H

#include <QObject>
#include <QThread>
#include <QEventLoop>
#include <QJsonObject>
#include <QMessageBox>
#include <QMetaEnum>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMutex>
#include <QJsonArray>
#include <QJsonDocument>
#include <QApplication>
#include <QImage>
#include <QScreen>
#include <QTimer>
#include <QDir>
#include <QSettings>
#include <QUuid>
#include <memory>
#include <atomic>
#include "../util/input_util.h"
#include "../util/json_util.h"
#include "../common/constant.h"

// 前向声明
class H264Decoder;
class FilePacketUtil;

/**
 * @brief The WebRtcCtl class 控制端的webrtc对象（control_window需要用到的）
 */
class WebRtcCtl : public QObject
{
    Q_OBJECT
public:
    // 构造函数：传入远程ID、远程密码MD5
    WebRtcCtl(const QString &remoteId, const QString &remotePwdMd5, bool isOnlyFile,
              bool adaptiveResolution = false, QObject *parent = nullptr);
    ~WebRtcCtl();

    // 解析来自WebSocket的消息
    void parseWsMsg(const QJsonObject &object);

    // 初始化WebRTC连接
    void init();

    // 重连控制（用于在连接失败时尝试恢复）
    void scheduleReconnect();
    void stopReconnect();

private:
    // WebRTC核心功能
    void initPeerConnection();
    void createTracks();
    void setupCallbacks();
    void setupFileChannelCallbacks();
    void setupFileTextChannelCallbacks();
    void setupInputChannelCallbacks();
    void destroy();

    // 文件上传相关
    void uploadSingleFile(const QString &ctlPath, const QString &cliPath);
    void uploadDirectory(const QString &ctlPath, const QString &cliPath);

    // 媒体数据处理
    void processVideoFrame(const rtc::binary &videoData, const rtc::FrameInfo &frameInfo);
    void processAudioFrame(const rtc::binary &audioData, const rtc::FrameInfo &frameInfo);

    // 成员变量
    QString m_remoteId;
    QString m_remotePwdMd5;
    bool m_isOnlyFile;         // 是否仅文件传输模式
    bool m_adaptiveResolution; // 是否启用自适应分辨率

    // WebRTC相关
    std::shared_ptr<rtc::PeerConnection> m_peerConnection;
    std::shared_ptr<rtc::DataChannel> m_fileChannel;
    std::shared_ptr<rtc::DataChannel> m_fileTextChannel;
    std::shared_ptr<rtc::DataChannel> m_inputChannel;
    std::shared_ptr<rtc::Track> m_videoTrack;
    std::shared_ptr<rtc::Track> m_audioTrack;

    // 连接状态
    bool m_connected;
    bool needSendAnswer; // 是否需要发送Answer

    // 文件分包工具
    std::unique_ptr<FilePacketUtil> m_filePacketUtil;

    // ICE服务器配置
    std::string m_host;
    uint16_t m_port;
    std::string m_username;
    std::string m_password;

    // 媒体处理
    std::unique_ptr<H264Decoder> m_h264Decoder;

    // H264帧重组
    rtc::binary m_h264FrameBuffer; // 累积NAL单元的缓冲区
    QMutex m_h264BufferMutex;      // 保护缓冲区的互斥锁

    // 重连相关
    QTimer m_reconnectTimer;
    int m_reconnectAttempts = 0;
    int m_reconnectBackoffMs = 5000; // 基本退避时间（ms）
    bool m_allowReconnect = true;    // 是否允许自动重连
    std::atomic_bool m_reconnectPending{false}; // 防止多线程回调重复调度重连

    // 输入通道发送侧弱网保护：鼠标移动限流（ms 时间戳）
    qint64 m_lastInputMoveSendMs = 0;
    qint64 m_lastInputNotReadyLogMs = 0;

private slots:
    void doReconnect();

signals:
    // WebSocket消息发送
    void sendWsCliBinaryMsg(const QByteArray &message);
    void sendWsCliTextMsg(const QString &message);

    // 文件传输相关
    void recvGetFileList(const QJsonObject &object);
    void recvDownloadFile(bool status, const QString &filePath);
    void recvUploadFileRes(bool status, const QString &filePath);

    // 媒体相关
    void videoFrameDecoded(const QImage &frame);

public slots:
    // WebSocket消息处理
    void onWsCliRecvBinaryMsg(const QByteArray &message);
    void onWsCliRecvTextMsg(const QString &message);

    // 数据通道消息发送
    void inputChannelSendMsg(const rtc::message_variant &data);
    void fileChannelSendMsg(const rtc::message_variant &data);
    void fileTextChannelSendMsg(const rtc::message_variant &data);
    void uploadFile2CLI(const QString &ctlPath, const QString &cliPath);
};

#endif // WEBRTC_CTL_H
