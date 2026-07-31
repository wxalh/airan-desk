#ifndef WEBRTC_CLI_H
#define WEBRTC_CLI_H

#include <QThread>
#include <QEventLoop>
#include <QJsonObject>
#include <QMetaEnum>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMutex>
#include <QJsonArray>
#include <QJsonDocument>
#include <QImage>
#include <QSettings>
#include <QDir>
#include <QDirIterator>
#include <QRect>
#include <QHostInfo>
#include <QScreen>
#include <QMetaObject>
#include <QGuiApplication>
#include <QTimer>
#include <QDateTime>
#include <QUuid>
#include <QSet>
#include <QHash>
#include <QQueue>
#include <QWaitCondition>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include "media/capture/core/airan_capture_interface.h"
#include "rtc/core/rtc.hpp"
#include "util/input/input_util.h"
#include "common/constant.h"
#include "util/json/json_util.h"
#include "util/qt/qt_callback_util.h"

class FilePacketUtil;
class TerminalSession;
class QtCallbackDispatcher;
class AuditSession;
class TerminalCommandAuditParser;
struct TerminalCommandAuditRecord;


class WebRtcCli : public QObject,
                  private airan::media::AiranCaptureCallback
{
    Q_OBJECT
public:
    
    WebRtcCli(const QString &remoteId, int fps, bool isOnlyFile, const QString &sessionId = QString(), QObject *parent = nullptr);

    
    WebRtcCli(const QString &remoteId, int fps, bool isOnlyFile, int requestedWidth, int requestedHeight,
              const QString &networkPath = QStringLiteral("auto"),
              const QString &initialAudioMode = QStringLiteral("off"),
              const QString &sessionId = QString(),
              QObject *parent = nullptr);
    WebRtcCli(const QString &remoteId, int fps, bool isOnlyFile, int requestedWidth, int requestedHeight,
              const QString &networkPath, const QString &mediaTopology,
              const QString &qualityProfile = QStringLiteral("auto"),
              const QString &initialAudioMode = QStringLiteral("off"),
              const QString &sessionId = QString(),
              std::shared_ptr<AuditSession> auditSession = std::shared_ptr<AuditSession>(),
              QObject *parent = nullptr);

    
    ~WebRtcCli();

    void setControlledSessionMode(const QString &mode);

    
    void parseWsMsg(const QJsonObject &object);

private:
    void performDestroy();
    void initPeerConnection();
    void createTracksAndChannels();
    void setupCallbacks();
    void sendSignalingError(const QString &message);

    int effectiveCaptureFps() const;
    int currentPipelineFpsLimit() const;
    int clampFpsForCurrentPipeline(int fps) const;
    void applyEffectiveVideoFpsIfNeeded(const char *reason);
    rtc::Configuration buildRtcConfiguration() const;
    QString currentScreenId() const;
    int screenIndexForId(const QString &screenId) const;
    int desktopSourceIndexForScreenIndex(int screenIndex) const;
    bool desktopSourceIdForScreenIndex(int screenIndex, intptr_t *sourceId) const;
    QRect desktopSourceRectForScreenIndex(int screenIndex) const;
    QJsonArray screenCatalogJson() const;
    void selectScreenById(const QString &screenId, bool updateTrack);

    QString m_remoteId;
    QString m_subscriberId;
    QString m_sessionId;
    QString m_controlledSessionMode;
    std::shared_ptr<AuditSession> m_auditSession;
    QString m_disconnectReason{QStringLiteral("remote_or_network")};
    bool m_disconnectSent{false};
    bool m_connectionAudited{false};
    bool m_isOnlyFile;
    QDir m_currentDir;

    std::shared_ptr<rtc::PeerConnection> m_peerConnection;
    std::shared_ptr<rtc::DataChannel> m_fileChannel;
    std::shared_ptr<rtc::DataChannel> m_fileTextChannel;
    std::shared_ptr<rtc::DataChannel> m_inputChannel;
    std::shared_ptr<rtc::DataChannel> m_inputMoveChannel;
    std::shared_ptr<rtc::DataChannel> m_clipboardChannel;
    std::shared_ptr<rtc::DataChannel> m_heartbeatChannel;
    std::shared_ptr<rtc::Track> m_videoTrack;
    std::shared_ptr<rtc::Track> m_audioTrack;

    bool m_connected = false;
    bool m_channelsReady = false;
    bool m_destroying = false;
    std::atomic_bool m_shutdownStarted{false};
    bool m_remoteDescriptionSet = false;
    QVector<QPair<QString, QString>> m_pendingRemoteCandidates;

    int m_fps = 0;
    bool m_subscribed = false;
    QString m_lastStreamConfigSignature;
    qint64 m_lastStreamConfigNotifyMs = 0;
    QJsonObject m_pendingStreamConfig;
    QString m_networkPath{QStringLiteral("auto")};
    QString m_mediaTopology{QStringLiteral("p2p")};
    QString m_qualityProfile{QStringLiteral("weak_clear")};
    bool m_autoQualityProfile{true};
    int m_qualityPoorScore{0};
    int m_qualityGoodScore{0};
    qint64 m_lastQualityProfileSwitchMs{0};
    int m_adaptiveVideoFpsCap{0};
    int m_adaptiveFpsPoorScore{0};
    int m_adaptiveFpsGoodScore{0};
    qint64 m_lastAdaptiveFpsSwitchMs{0};
    int m_adaptiveCpuFpsCap{0};
    int m_adaptiveCpuPoorScore{0};
    int m_adaptiveCpuGoodScore{0};
    qint64 m_lastAdaptiveCpuFpsSwitchMs{0};
    qint64 m_captureRecoveryHoldUntilMs{0};
    quint64 m_lastCpuIdleTicks{0};
    quint64 m_lastCpuTotalTicks{0};
    double m_lastCpuUsagePercent{-1.0};
    int m_baseRequestedEncodeWidth = 0;
    int m_baseRequestedEncodeHeight = 0;

    FilePacketUtil *m_filePacketUtil = nullptr;
    QtCallbackDispatcher *m_callbackDispatcher = nullptr;
    std::shared_ptr<CallbackLifetime> m_callbackLifetime{std::make_shared<CallbackLifetime>()};
    QMutex m_fileIngressMutex;
    QWaitCondition m_fileIngressDrained;
    std::deque<rtc::binary> m_fileIngress;
    qint64 m_fileIngressBytes = 0;
    bool m_fileIngressScheduled = false;
    TerminalSession *m_terminalSession = nullptr;
    QTimer *m_terminalBackpressureTimer = nullptr;
    QQueue<QByteArray> m_pendingTerminalOutputChunks;
    bool m_terminalChannelPaused = false;
    bool m_terminalRemotePaused = false;
    std::unique_ptr<TerminalCommandAuditParser> m_terminalAuditParser;

    std::string m_host;
    uint16_t m_port = 0;
    std::string m_username;
    std::string m_password;
    int m_screen_width = 0;
    int m_screen_height = 0;
    int m_screenIndex = 0;
    int m_currentDesktopSourceIndex = 0;
    QString m_screenId;
    int m_screenCatalogGeneration = 0;
    QRect m_currentDesktopSourceRect;

    int m_coded_width = 0;
    int m_coded_height = 0;
    int m_visible_width = 0;
    int m_visible_height = 0;
    int m_requestedEncodeWidth = 0;
    int m_requestedEncodeHeight = 0;
    QString m_negotiatedVideoCodec{QStringLiteral("H264")};
    QString m_currentCaptureMethod;
    QString m_currentCaptureBackend;
    QString m_currentCapturePath;
    QString m_currentEncodePath;
    QString m_currentFallbackReason;
    QString m_currentEncoderName;
    QString m_currentEncoderType;
    bool m_desktopLocked{false};
    bool m_audioCaptureEnabled{false};
    QString m_audioMode{QStringLiteral("off")};
    quint64 m_lastMouseMoveSequence{0};
    bool m_mouseMoveBoundaryReady{false};
    QTimer *m_inputChannelRecoverTimer = nullptr;
    QTimer *m_streamConfigApplyTimer = nullptr;
    QTimer *m_streamConfigNotifyTimer = nullptr;
    QTimer *m_controlWatchdogTimer = nullptr;
    QTimer *m_sessionHeartbeatTimer = nullptr;
    std::atomic<qint64> m_lastSessionInboundMs{0};
    std::atomic<qint64> m_lastSessionOutboundMs{0};
    std::atomic<qint64> m_lastSessionProgressMs{0};
    quint64 m_lastBufferedAmount = 0;
    quint64 m_heartbeatSequence = 0;
    std::atomic_bool m_heartbeatNegotiated{false};
    QTimer *m_disconnectGraceTimer = nullptr;
    QTimer *m_mediaStatsTimer = nullptr;
    qint64 m_lastControlAliveMs = 0;

    struct PendingDownload
    {
        QString cliPath;
        QString ctlPath;
        QString transferId;
    };
    QQueue<PendingDownload> m_pendingDownloads;
    bool m_downloadQueueActive = false;
    QMap<QString, QVector<QByteArray>> m_uploadFragments;
    mutable QMutex m_transferMutex;
    QHash<QString, qint64> m_cancelledTransfers;
    QSet<QString> m_clipboardCacheRoots;
    QSet<QString> m_pendingClipboardPromiseTargets;
    QSet<QString> m_auditedClipboardDownloads;
    QMap<QString, bool> m_clipboardPromiseFileResults;
    QWaitCondition m_clipboardPromiseFileWait;
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
    QTimer *m_clipboardSnapshotTimer = nullptr;
    QByteArray m_lastSentClipboardPayloadSignature;
    QByteArray m_lastRemoteAppliedClipboardPayloadSignature;
    qint64 m_clipboardAutoSyncAllowedUntilMs = 0;
    bool m_clipboardMonitorConnected = false;
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

signals:
    void shutdownFinished();
    void sendWsCliBinaryMsg(const QByteArray &message);
    void sendWsCliTextMsg(const QString &message);
    void destroyCli();
    void controlledSessionConnected(const QString &sessionId, const QString &peerId,
                                    const QString &mode, const QString &sourceIp);
    void controlledSessionDisconnected(const QString &sessionId, const QString &peerId,
                                       const QString &reason);
    void audioCapturePromptRequested(const QString &requestId, const QString &requestedMode, const QString &modeLabel);
    void audioCaptureDecisionRequested(const QString &requestId, const QString &requestedMode, bool accepted, const QString &message);

public slots:
    void init();
    void destroy();
    void requestLocalDisconnect();
    void requestDisconnect(const QString &reason);
    void shutdownAndMoveToOwnerThread(QObject *owner);
    void onWsCliRecvBinaryMsg(const QByteArray &message);
    void onWsCliRecvTextMsg(const QString &message);
    void handleFileReceived(bool status, const QString &tempPath, const QString &errorMessage);
    void recoverInputChannel();
    void checkControlAlive();
    void setDesktopLocked(bool locked);
    void recoverDesktopCaptureAfterSessionUnlock();
    void handleDesktopScreensChanged();
    void applyAudioCaptureDecision(const QString &requestId, const QString &requestedMode, bool accepted, const QString &message);
    void applyMediaStats(const QString &codec,
                         const QString &encoderImplementation,
                         double availableOutgoingBitrateBps,
                         double targetBitrateBps,
                         double fractionLost,
                         double rttMs,
                         const QString &qualityLimitationReason);
    void applyCapturePipelineState(const QString &captureBackend,
                                   const QString &capturePath,
                                   const QString &encodePath,
                                   const QString &fallbackReason);
    void applyCaptureBackendState(const QString &captureBackend);

private slots:
    void startMediaCapture();
    void stopMediaCapture();
    void setupFileChannelCallbacks();
    void setupFileTextChannelCallbacks();
    void setupInputChannelCallbacks();
    void setupInputMoveChannelCallbacks();
    void setupClipboardChannelCallbacks();
    void setupHeartbeatChannelCallbacks();
    bool ensureAudioTrack();
    void scheduleInputChannelRecovery(const QString &reason);
    void notifyCurrentStreamConfig();
    void notifyDesktopState();
    void setAudioCaptureEnabled(bool enabled);
    void setAudioMode(const QString &mode);
    void onPeerConnectionStateChanged(rtc::PeerConnection::State state);
    void onPeerIceStateChanged(rtc::PeerConnection::IceState state);
    void onPeerGatheringStateChanged(rtc::PeerConnection::GatheringState state);
    void parseFileMsg(const QJsonObject &object);
    void handleTerminalMessage(const QJsonObject &object);
    void recordTerminalAuditRecord(const TerminalCommandAuditRecord &record);
    void onInputChannelOpen();
    void onInputChannelClosed();
    void handleInputChannelErrorOnThread(const QString &reason);
    void parseInputMsgIfAlive(const QJsonObject &object);
    void parseInputMsg(const QJsonObject &object);
    bool sendClipboardChannelMessage(const QJsonObject &message);
    void pollSessionHeartbeat();
    void sendSessionHeartbeat(const QString &action);
    void noteSessionInboundActivity();
    void noteSessionOutboundActivity();
    void noteSessionTransportProgress();
    quint64 sampleSessionBufferedAmount() const;

private:
    void onPeerLocalDescription(rtc::Description description);
    void onPeerLocalCandidate(const rtc::Candidate &candidate);
    void onAudioFrameReceived(rtc::binary data, rtc::FrameInfo info);
    void onFileChannelOpen();
    void onFileChannelMessage(rtc::message_variant data);
    void drainFileIngress();
    void processFileChannelFragment(rtc::binary data);
    void onFileChannelError(std::string error);
    void onFileChannelClosed();
    void onFileTextChannelOpen();
    void onFileTextChannelMessage(rtc::message_variant data);
    void onFileTextChannelError(std::string error);
    void onFileTextChannelClosed();
    void onInputChannelMessage(rtc::message_variant data);
    void onInputChannelError(std::string error);
    void onInputMoveChannelOpen();
    void onInputMoveChannelError(std::string error);
    void onInputMoveChannelClosed();
    void onClipboardChannelOpen();
    void onClipboardChannelMessage(rtc::message_variant data);
    void onClipboardChannelError(std::string error);
    void onClipboardChannelClosed();
    void onHeartbeatChannelOpen();
    void onHeartbeatChannelMessage(rtc::message_variant data);
    void onHeartbeatChannelError(std::string error);
    void onHeartbeatChannelClosed();
    void handleClipboardMessage(const QJsonObject &object);
    void applyClipboardPayload(const QString &requestId, const QJsonObject &payload, bool pasteAfterApply);
    void connectClipboardMonitor();
    void scheduleLocalClipboardSnapshot(const QString &reason);
    void sendLocalClipboardSnapshot(const QString &requestId = QString());
    void sendLocalClipboardSnapshotPayload(const QString &requestId, const QJsonObject &rawPayload);
    bool sendClipboardPayloadInChunks(const QString &requestId,
                                      const QJsonObject &payload,
                                      bool pasteAfterApply = false,
                                      const std::function<void(bool)> &completion = {});
    bool enqueueClipboardChunkTransfer(bool payloadTransfer,
                                       const QString &requestId,
                                       const QByteArray &bytes,
                                       bool pasteAfterApply,
                                       const std::function<void(bool)> &completion);
    QJsonObject currentClipboardChunkMessage(const ClipboardChunkSendState &state) const;
    void scheduleClipboardChunkSend(int delayMs = 0);
    void drainClipboardChunkSendQueue();
    void failClipboardChunkSendQueue();
    void noteClipboardInputActivity();
    bool requestClipboardPromisedFile(const QString &requestId,
                                      const QString &sourcePath,
                                      const QString &localPath,
                                      QString *errorMessage);
    QByteArray readControllerClipboardFileChunk(const QString &baseRequestId,
                                                const QString &sourcePath,
                                                qint64 offset,
                                                qint64 maxBytes,
                                                bool *ok,
                                                QString *errorMessage);
    void noteClipboardPromisedFileResult(const QString &path, bool status);
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
    void onTerminalOutputReady(const QByteArray &data);
    void pollTerminalBackpressure();
    void onTerminalClosed(int exitCode);
    void onTerminalError(const QString &message);
    void onTerminalInfoReady(const QString &osName, const QString &shellPath, const QString &mode, bool pathTracking);
    void onAiranCaptureFrame(airan::media::CaptureFrameDescriptor frame) override;
    void onAiranCaptureTransition(const airan::media::PathTransition &transition) override;

    void setRemoteDescription(const QString &data, const QString &type);
    void addIceCandidate(const QString &candidate, const QString &mid);

    void populateLocalFiles();

    void sendFile(const QString &cliPath, const QString &ctlPath, const QString &transferId);
    void processDownloadQueue();
    bool sendSingleFile(const QString &cliPath, const QString &ctlPath, const QString &transferId,
                        qint64 baseBytes = 0, qint64 totalBytes = -1, int currentFileIndex = 1, int totalFiles = 1);
    void sendDirectory(const QString &cliPath, const QString &ctlPath, const QString &transferId);
    bool sendFileMetadataPacket(const QJsonObject &header, const QString &transferId = QString());
    bool isTransferCancelled(const QString &transferId) const;
    void markTransferCancelled(const QString &transferId);
    void sendTransferProgress(const QString &transferId, qint64 transferredBytes, qint64 totalBytes, int transferredFiles, int totalFiles,
                              const QString &ctlPath = QString(), const QString &cliPath = QString());
    void sendTransferCancel(const QString &transferId);
    qint64 collectDirectoryStats(const QString &path, int *fileCount) const;
    void handleRemoteDescriptionMessage(const QJsonObject &object, const QString &type);
    void handleRemoteCandidateMessage(const QJsonObject &object);
    void addRemoteCandidateOrQueue(const QString &candidate, const QString &mid);
    void flushPendingRemoteCandidates();
    void sendFileErrorResponse(const QString &filePath, const QString &error);
    void sendUploadResponse(const QString &fileName, bool success, const QString &message);
    void handleFileListRequest(const QJsonObject &object);
    void handleFileDownloadRequest(const QJsonObject &object);
    void handleFileTransferCancel(const QJsonObject &object);
    void handleFileDeleteRequest(const QJsonObject &object);
    void handleFileRenameRequest(const QJsonObject &object);
    void handleFileCreateRequest(const QJsonObject &object);

    void handleMouseEvent(const QJsonObject &object);
    void handleKeyboardEvent(const QJsonObject &object);
    void handleStreamConfig(const QJsonObject &object);
    void applyPendingStreamConfig();
    void applyStreamConfig(const QJsonObject &object);
    void handleAudioCaptureConfig(const QJsonObject &object);
    void handleSwitchScreen(const QJsonObject &object);
    void handleRemoteOperation(const QJsonObject &object);
    void handleRunFile(const QJsonObject &object);
    void applyRequestedResolution(int requestedWidth, int requestedHeight);

    void sendFileChannelMessage(const QJsonObject &message);
    bool sendFileTextChannelMessage(const QJsonObject &message);
    bool sendTerminalOutputChunk(const QByteArray &data);
    bool sendInputChannelMessage(const QJsonObject &message);
    void sendAudioCaptureResponse(const QString &requestId, const QString &mode, bool accepted, const QString &message);

    void calculateEncodeResolution(int requestedMaxWidth, int requestedMaxHeight);
    void startAudioCapture();
    void stopAudioCapture();
    void startAudioPlayback();
    void stopAudioPlayback();
    void queryMediaStats();
    void updateAutomaticQualityProfile(double availableOutgoingBitrateBps,
                                       double targetBitrateBps,
                                       double fractionLost,
                                       double rttMs,
                                       const QString &qualityLimitationReason);
    void updateAdaptiveVideoFpsCap(double availableOutgoingBitrateBps,
                                   double targetBitrateBps,
                                   double fractionLost,
                                   double rttMs,
                                   const QString &qualityLimitationReason);
    void updateAdaptiveCpuFpsCap(double fractionLost,
                                 double rttMs,
                                 const QString &qualityLimitationReason);
};

#endif /* WEBRTC_CLI_H */
