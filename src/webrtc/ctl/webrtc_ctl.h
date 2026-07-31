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
#include <QImage>
#include <QMargins>
#include <QSize>
#include <QTimer>
#include <QStringList>
#include <QMap>
#include <QHash>
#include <QVector>
#include <QQueue>
#include <QDir>
#include <QDirIterator>
#include <QSettings>
#include <QUuid>
#include <QSet>
#include <QWaitCondition>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <chrono>
#include "rtc/core/rtc.hpp"
#include "util/input/input_util.h"
#include "util/input/mouse_input_policy.h"
#include "util/json/json_util.h"
#include "common/constant.h"
#include "util/qt/qt_callback_util.h"

class FilePacketUtil;
class QtCallbackDispatcher;
class QWidget;


class WebRtcCtl : public QObject
{
    Q_OBJECT
public:
    
    WebRtcCtl(const QString &remoteId, const QString &remotePwdMd5, bool isOnlyFile, QObject *parent = nullptr);
    void setSessionLabel(const QString &label);

    
    ~WebRtcCtl();

    
    void parseWsMsg(const QJsonObject &object);

    
    void init();

private:
    void initPeerConnection();
    void connectFilePacketUtilSignals();
    void createTracks();
    void setupCallbacks();
    void setupFileChannelCallbacks();
    void setupFileTextChannelCallbacks();
    void setupInputChannelCallbacks();
    void setupClipboardChannelCallbacks();
    void setupHeartbeatChannelCallbacks();
    void sendStreamConfig();
    void requestCurrentAudioMode();
    void destroy();
    void performShutdown();

    bool uploadSingleFile(const QString &ctlPath, const QString &cliPath, const QString &transferId,
                          qint64 baseBytes = 0, qint64 totalBytes = -1, int currentFileIndex = 1, int totalFiles = 1);
    void uploadDirectory(const QString &ctlPath, const QString &cliPath, const QString &transferId);
    void processUploadQueue();
    bool sendFileMetadataPacket(const QJsonObject &header, const QString &transferId = QString());
    bool isTransferCancelled(const QString &transferId) const;
    void markTransferCancelled(const QString &transferId);
    void sendTransferCancel(const QString &transferId);
    void emitTransferProgress(const QString &transferId, qint64 transferredBytes, qint64 totalBytes, int transferredFiles, int totalFiles);
    qint64 collectDirectoryStats(const QString &path, int *fileCount) const;
    void handleSignalingError(const QJsonObject &object);
    void handleRemoteDescriptionMessage(const QJsonObject &object, const QString &type);
    void handleRemoteCandidateMessage(const QJsonObject &object);
    void addRemoteCandidateOrQueue(const QString &candidate, const QString &mid);
    void flushPendingRemoteCandidates();

    void processVideoFrame(const rtc::binary &videoData, const rtc::FrameInfo &frameInfo);
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    void processD3D11VideoFrame(rtc::D3D11VideoFrame frame);
#endif
    void processAudioFrame(const rtc::binary &audioData, const rtc::FrameInfo &frameInfo);
    rtc::Configuration buildRtcConfiguration() const;
    void applyLocalStreamConfig(const QJsonObject &object);
    void noteLocalNetworkCandidate(const QString &candidate);
    void publishNetworkPathState(const QString &selectedPath = QString());
    void restartAfterNetworkPathChange();
    void startAudioPlayback();
    void stopAudioPlayback();
    void startAudioCapture();
    void stopAudioCapture();
    bool ensureAudioTrack();

    void onPeerConnectionStateChanged(rtc::PeerConnection::State state);
    void onPeerIceStateChanged(rtc::PeerConnection::IceState state);
    void onPeerGatheringStateChanged(rtc::PeerConnection::GatheringState state);
    void onPeerLocalDescription(rtc::Description description);
    void onPeerLocalCandidate(const rtc::Candidate &candidate);
    void onVideoFrameReceived(rtc::binary data, rtc::FrameInfo info);
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    void onD3D11VideoFrameReceived(rtc::D3D11VideoFrame frame);
#endif
    void onAudioFrameReceived(rtc::binary data, rtc::FrameInfo info);
    void onRemoteTrack(std::shared_ptr<rtc::Track> track);
    void onRemoteDataChannel(std::shared_ptr<rtc::DataChannel> channel);
    void onHeartbeatChannelOpen();
    void onHeartbeatChannelMessage(rtc::message_variant message);
    void onHeartbeatChannelError(std::string error);
    void onHeartbeatChannelClosed();
    void onFileChannelOpen();
    void onFileChannelClosed();
    void onFileChannelError(const std::string &error);
    void onFileChannelMessage(const rtc::message_variant &message);
    void drainFileIngress();
    void processFileChannelFragment(rtc::binary data);
    void onFileTextChannelOpen();
    void onFileTextChannelClosed();
    void onFileTextChannelError(const std::string &error);
    void onFileTextChannelMessage(const rtc::message_variant &message);
    void processFileTextChannelMessage(const QByteArray &data);
    void drainFileTextIngress();
    void handleFileTextChannelObject(const QJsonObject &object);
    void flushTerminalOutput();
    void updateTerminalFlowControl();
    void sendTerminalFlowControl(bool enabled);
    bool handleTerminalTextChannelObject(const QJsonObject &object, const QString &msgType);
    bool handleFileTransferTextChannelObject(const QJsonObject &object, const QString &msgType);
    void onInputChannelOpen();
    void onInputChannelClosed();
    void onInputChannelError(const std::string &error);
    void onInputChannelMessage(const rtc::message_variant &message);
    void onClipboardChannelOpen();
    void onClipboardChannelClosed();
    void onClipboardChannelError(const std::string &error);
    void onClipboardChannelMessage(const rtc::message_variant &message);
    void handleClipboardChannelObject(const QJsonObject &object);
    void handleClipboardPrepareUploadResult(const QJsonObject &object);
    void handleClipboardPromiseFileRequest(const QJsonObject &object);
    void handleClipboardStreamReadRequest(const QJsonObject &object);
    void handleClipboardStreamReadResult(const QJsonObject &object);
    void handleClipboardTextBegin(const QJsonObject &object);
    void handleClipboardTextChunk(const QJsonObject &object);
    void handleClipboardTextEnd(const QJsonObject &object);
    void handleClipboardPayloadBegin(const QJsonObject &object);
    void handleClipboardPayloadChunk(const QJsonObject &object);
    void handleClipboardPayloadEnd(const QJsonObject &object);
    void removeClipboardInboundPayloadLocked(const QString &requestId);
    void expireClipboardPayloadRequests();
    void clearClipboardPayloadTransferState();
    void handleClipboardSnapshot(const QJsonObject &object);
    void handleClipboardApplyResult(const QJsonObject &object);
    void noteClipboardUploadResult(const QString &path, bool status);
    void noteClipboardDownloadResult(const QString &path, bool status);
    bool downloadClipboardPromisedFile(const QString &baseRequestId,
                                       const QString &remotePath,
                                       const QString &localPath,
                                       QString *errorMessage);
    QByteArray readRemoteClipboardFileChunk(const QString &baseRequestId,
                                            const QString &remotePath,
                                            qint64 offset,
                                            qint64 maxBytes,
                                            bool *ok,
                                            QString *errorMessage);
    void sendClipboardSetPayload(const QString &requestId, const QJsonObject &payload, bool pasteAfterApply);
    bool sendClipboardTextPayloadInChunks(const QString &requestId, const QJsonObject &payload, bool pasteAfterApply);
    bool sendClipboardPayloadInChunks(const QString &requestId,
                                      const QJsonObject &payload,
                                      bool pasteAfterApply,
                                      const std::function<void(bool)> &completion = {});
    bool enqueueClipboardChunkTransfer(bool payloadTransfer,
                                       const QString &requestId,
                                       const QByteArray &bytes,
                                       bool pasteAfterApply,
                                       const std::function<void(bool)> &completion);
    struct ClipboardChunkSendState;
    QJsonObject currentClipboardChunkMessage(const ClipboardChunkSendState &state) const;
    void scheduleClipboardChunkSend(int delayMs = 0);
    void drainClipboardChunkSendQueue();
    void failClipboardChunkSendQueue();
    void sendClipboardPayloadToRemote(const QJsonObject &payload, bool pasteAfterApply);
    void sendRemotePasteShortcut();
    Q_INVOKABLE bool sendClipboardChannelObject(const QJsonObject &object);
    void pollSessionHeartbeat();
    void sendSessionHeartbeat(const QString &action);
    void noteSessionInboundActivity();
    void noteSessionOutboundActivity();
    void noteSessionTransportProgress();
    quint64 sampleSessionBufferedAmount() const;
    void setSessionHealth(int state, const QString &message);
    void requestSessionReconnect(const QString &message);
    void flushPendingInputMove();
    void finishInputMoveBurst();
    void suspendInputMoveBurst();
    void retryPendingInputMoveBoundary();
    void resetInputMoveBurst();
    void sendDisconnectSignal(const QString &reason);
    void disableReconnect();
    bool sendInputMoveDispatch(const rtc::message_variant &data,
                               const MouseMoveBurstPolicy::Dispatch &dispatch);
    bool sendInputChannelNow(const rtc::message_variant &data,
                             bool isMouseMove,
                             bool reliableBoundary);
    void setupInputMoveChannelCallbacks();
    void onInputMoveChannelOpen();
    void onInputMoveChannelClosed();
    void onInputMoveChannelError(const std::string &error);

    QString m_remoteId;
    QString m_remotePwdMd5;
    QString m_sessionId;
    QString m_sessionLabel;
    bool m_isOnlyFile;
    std::shared_ptr<rtc::PeerConnection> m_peerConnection;
    std::shared_ptr<rtc::DataChannel> m_fileChannel;
    std::shared_ptr<rtc::DataChannel> m_fileTextChannel;
    std::shared_ptr<rtc::DataChannel> m_inputChannel;
    std::shared_ptr<rtc::DataChannel> m_inputMoveChannel;
    std::shared_ptr<rtc::DataChannel> m_clipboardChannel;
    std::shared_ptr<rtc::DataChannel> m_heartbeatChannel;
    std::shared_ptr<rtc::Track> m_videoTrack;
    std::shared_ptr<rtc::Track> m_localAudioTrack;
    std::shared_ptr<rtc::Track> m_remoteAudioTrack;

    bool m_connected = false;
    bool m_disconnectSent = false;
    bool m_remoteDisconnectReceived = false;
    bool needSendAnswer = false;
    bool m_remoteDescriptionSet = false;
    QVector<QPair<QString, QString>> m_pendingRemoteCandidates;

    std::unique_ptr<FilePacketUtil> m_filePacketUtil;
    QtCallbackDispatcher *m_callbackDispatcher = nullptr;
    std::shared_ptr<CallbackLifetime> m_callbackLifetime{std::make_shared<CallbackLifetime>()};
    QMutex m_fileIngressMutex;
    QWaitCondition m_fileIngressDrained;
    std::deque<rtc::binary> m_fileIngress;
    qint64 m_fileIngressBytes = 0;
    bool m_fileIngressScheduled = false;

    std::string m_host;
    uint16_t m_port = 0;
    std::string m_username;
    std::string m_password;
    QString m_networkPath{QStringLiteral("auto")};
    QString m_mediaTopology{QStringLiteral("p2p")};
    QString m_qualityProfile{QStringLiteral("auto")};
    int m_requestedWidth{0};
    int m_requestedHeight{0};
    QStringList m_availableNetworkPaths{QStringLiteral("auto")};
    QString m_selectedNetworkPath;
    QString m_pendingNetworkPathReconnect;
    QString m_audioMode{QStringLiteral("off")};
    QSize m_remoteCodedSize;
    QSize m_remoteVisibleSize;
    QMargins m_remotePadding;
    QJsonArray m_remoteScreens;
    QString m_remoteScreensSignature;
    QString m_remoteScreenId;
    QString m_remoteOsName;
    QString m_remoteEncoderName;
    QString m_remoteEncoderType;
    QString m_remoteMediaCodec;
    QString m_remoteMediaCaptureLabel;

    QTimer *m_reconnectTimer = nullptr;
    int m_reconnectAttempts = 0;
    int m_reconnectBackoffMs = 5000;
    bool m_allowReconnect = true;
    std::atomic_bool m_shutdownStarted{false};
    std::atomic_bool m_reconnectPending{false};
    bool m_shutdownDone = false;

    qint64 m_lastInputNotReadyLogMs = 0;
    MouseMoveBurstPolicy m_mouseMovePolicy;
    QTimer *m_inputMoveFlushTimer = nullptr;
    QTimer *m_inputMoveTailTimer = nullptr;
    rtc::message_variant m_latestInputMove;
    bool m_hasLatestInputMove = false;
    bool m_pendingInputMoveBoundary = false;
    QTimer *m_controlHeartbeatTimer = nullptr;
    QTimer *m_sessionHeartbeatTimer = nullptr;
    std::atomic<qint64> m_lastSessionInboundMs{0};
    std::atomic<qint64> m_lastSessionOutboundMs{0};
    std::atomic<qint64> m_lastSessionProgressMs{0};
    quint64 m_lastBufferedAmount = 0;
    quint64 m_heartbeatSequence = 0;
    std::atomic_bool m_heartbeatNegotiated{false};
    int m_sessionHealthState = 0;
    QMutex m_fileTextIngressMutex;
    QQueue<QByteArray> m_fileTextIngress;
    std::atomic<qint64> m_fileTextIngressBytes{0};
    bool m_fileTextIngressScheduled = false;
    QTimer *m_terminalOutputFlushTimer = nullptr;
    QByteArray m_pendingTerminalOutput;
    qint64 m_terminalOutputInFlight = 0;
    qint64 m_terminalConsumerBacklog = 0;
    bool m_terminalFlowPaused = false;

    qint64 m_videoStatsStartMs = 0;
    qint64 m_videoStatsBytes = 0;
    qint64 m_lastVideoDecodedMs = 0;
    int m_videoFeedbackDecodedFrames = 0;
    mutable QMutex m_transferMutex;
    QHash<QString, qint64> m_cancelledTransfers;
    struct PendingUpload
    {
        QString ctlPath;
        QString cliPath;
        QString transferId;
    };
    QQueue<PendingUpload> m_pendingUploads;
    bool m_uploadQueueActive = false;
    QMap<QString, QJsonObject> m_pendingClipboardUploadPayloads;
    QMap<QString, QSet<QString>> m_pendingClipboardUploadTargets;
    QMap<QString, bool> m_pendingClipboardUploadPasteAfterApply;
    QMap<QString, QString> m_pendingClipboardPromiseUploadTargets;
    QMap<QString, QString> m_clipboardPromiseUploadSourceToTransferId;
    QMap<QString, QString> m_clipboardPromiseUploadSourceToTargetPath;
    QMap<QString, qint64> m_clipboardPromiseUploadSourceToTotalBytes;
    QMap<QString, qint64> m_clipboardPromiseUploadSourceToTransferredBytes;
    QSet<QString> m_startedClipboardPromiseUploadSources;
    QMap<QString, QJsonObject> m_pendingClipboardDownloadPayloads;
    QMap<QString, QSet<QString>> m_pendingClipboardDownloadTargets;
    QSet<QString> m_pendingClipboardPromiseTargets;
    QMap<QString, bool> m_clipboardPromiseDownloadResults;
    QWaitCondition m_clipboardPromiseDownloadWait;
    QMap<QString, QString> m_clipboardPromiseRemotePathToTransferId;
    QMap<QString, qint64> m_clipboardPromiseRemotePathToTotalBytes;
    QMap<QString, qint64> m_clipboardPromiseRemotePathToTransferredBytes;
    QSet<QString> m_startedClipboardPromiseRemotePaths;
    QMap<QString, QByteArray> m_clipboardInboundTextChunks;
    QMap<QString, int> m_clipboardInboundTextChunkCounts;
    QMap<QString, int> m_clipboardInboundTextNextIndexes;
    QMap<QString, qint64> m_clipboardInboundTextExpectedBytes;
    QMap<QString, bool> m_clipboardInboundTextPasteAfterApply;
    QMap<QString, QByteArray> m_clipboardInboundPayloadChunks;
    QMap<QString, int> m_clipboardInboundPayloadChunkCounts;
    QMap<QString, int> m_clipboardInboundPayloadNextIndexes;
    QMap<QString, qint64> m_clipboardInboundPayloadExpectedBytes;
    QMap<QString, bool> m_clipboardInboundPayloadPasteAfterApply;
    QMap<QString, qint64> m_clipboardInboundPayloadDeadlinesMs;
    qint64 m_clipboardInboundPayloadReservedBytes = 0;
    QMap<QString, QByteArray> m_clipboardStreamChunks;
    QMap<QString, QString> m_clipboardStreamErrors;
    QMap<QString, qint64> m_pendingClipboardStreamRequests;
    QWaitCondition m_clipboardStreamWait;
    QSet<QString> m_clipboardCacheRoots;
    bool m_remoteSupportsClipboardPayloadV2 = false;
    struct ClipboardChunkSendState
    {
        bool payloadTransfer = false;
        QString requestId;
        QByteArray bytes;
        bool pasteAfterApply = false;
        int chunkCount = 0;
        int nextStep = 0;
        qint64 deadlineMs = 0;
        std::function<void(bool)> completion;
    };
    QQueue<ClipboardChunkSendState> m_clipboardChunkSendQueue;
    qint64 m_clipboardChunkSendQueuedBytes = 0;
    bool m_clipboardChunkSendScheduled = false;
    bool m_remoteDesktopLocked = false;

private slots:
    void doReconnect();
    void sendControlHeartbeat();
    void onVideoFrameBytesReceived(const QByteArray &data, qint64 timestampUs);
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    void onD3D11VideoFrameQueued(const rtc::D3D11VideoFrame &frame);
#endif

signals:
    void shutdownFinished();
    void sendWsCliBinaryMsg(const QByteArray &message);
    void sendWsCliTextMsg(const QString &message);
    void remoteEncoderChanged(const QString &encoderName, const QString &encoderType);
    void remoteMediaStateChanged(const QString &codec, const QString &captureMethod);
    void remoteScreensChanged(const QJsonArray &screens, const QString &currentScreenId);
    void audioModeRequestFinished(const QString &requestId, const QString &mode, bool accepted, const QString &message);
    void remoteOsChanged(const QString &osName);
    void networkPathStateChanged(const QStringList &availablePaths, const QString &selectedPath, const QString &requestedPath);
    void desktopStateChanged(bool locked, const QString &message);
    void connectionStatusChanged(const QString &status);
    void sessionHealthChanged(int state, const QString &message);
    void remoteDisconnectRequested(const QString &reason, bool peerWide);

    void recvGetFileList(const QJsonObject &object);
    void recvDownloadFile(bool status, const QString &filePath);
    void recvUploadFileRes(bool status, const QString &filePath, const QString &errorMessage);
    void recvDeleteFileRes(bool status, const QString &filePath, const QString &errorMessage);
    void recvRenameFileRes(bool status, const QString &filePath, const QString &errorMessage);
    void recvCreateFileRes(bool status, const QString &filePath, bool isDirectory, const QString &errorMessage);
    void fileTransferStarted(const QString &transferId, const QString &sourcePath, const QString &targetPath, const QString &operation);
    void fileTransferProgress(const QString &transferId, qint64 transferredBytes, qint64 totalBytes, int transferredFiles, int totalFiles);
    void fileTextChannelOpened();
    void localClipboardPayloadReceived(const QJsonObject &payload);
    void terminalOutput(const QByteArray &data);
    void terminalClosed(int exitCode);
    void terminalError(const QString &message);
    void terminalInfo(const QString &osName, const QString &shellPath, const QString &mode, bool pathTracking, bool pathTrackingReady);

    void videoFrameDecoded(const QImage &frame);
    void videoFrameD3D11Decoded(const rtc::D3D11VideoFrame &frame);
    void videoStatsUpdated(double kbps, const QSize &resolution);

public slots:
    void onWsCliRecvBinaryMsg(const QByteArray &message);
    void onWsCliRecvTextMsg(const QString &message);
    void notifyLocalDisconnect();
    void shutdown();
    void shutdownAndMoveToOwnerThread(QObject *owner);
    void scheduleReconnect();
    void stopReconnect();
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    void disableD3D11VideoRendering();
#endif

    void inputChannelSendMsg(const rtc::message_variant &data);
    void fileChannelSendMsg(const rtc::message_variant &data);
    void fileTextChannelSendMsg(const rtc::message_variant &data);
    void pasteClipboardPayloadToRemote(const QJsonObject &payload);
    void syncClipboardPayloadToRemote(const QJsonObject &payload);
    void requestRemoteClipboardSnapshot();
    bool startRemoteFileDrag(QWidget *dragSource,
                             const QJsonArray &files,
                             const QString &requestId,
                             QString *errorMessage = nullptr);
    void uploadFile2CLI(const QString &ctlPath, const QString &cliPath, const QString &transferId);
    void cancelFileTransfer(const QString &transferId);
    void setNetworkPath(const QString &networkPath);
    void setAudioMode(const QString &mode);
    void acknowledgeTerminalOutput(qint64 bytes);
    void setTerminalConsumerBacklog(qint64 bytes);
};

#endif /* WEBRTC_CTL_H */
