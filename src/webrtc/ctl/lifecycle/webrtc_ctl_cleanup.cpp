#include "webrtc/ctl/webrtc_ctl.h"
#include "util/file/file_packet_util.h"
#include "util/clipboard/clipboard_util.h"
#include "util/clipboard/native/clipboard_file_promise.h"

#include <QMutexLocker>
#include <QElapsedTimer>

void WebRtcCtl::wakeClipboardWaitersForShutdown()
{
    QMutexLocker locker(&m_transferMutex);
    for (const QString &path : m_pendingClipboardPromiseTargets)
        m_clipboardPromiseDownloadResults.insert(path, false);
    for (const QString &requestId : m_pendingClipboardStreamRequests.keys())
        m_clipboardStreamErrors.insert(requestId, QStringLiteral("Session closed."));
    m_clipboardPromiseDownloadWait.wakeAll();
    m_clipboardStreamWait.wakeAll();
}

void WebRtcCtl::destroy()
{
    QElapsedTimer shutdownTimer;
    shutdownTimer.start();
    LOG_DEBUG("WebRtcCtl shutdown started");
    if (m_shutdownStarted.exchange(true))
        return;
    wakeClipboardWaitersForShutdown();
    m_callbackLifetime->closeAndWait();
    LOG_DEBUG("WebRtcCtl shutdown stage callback-lifetime closed: elapsedMs={}", shutdownTimer.elapsed());
    failClipboardChunkSendQueue();
    clearClipboardPayloadTransferState();
    LOG_DEBUG("WebRtcCtl destroy started");
    m_connected = false;
    m_pendingFileTextMessages.clear();
    m_pendingFileTextMessageBytes = 0;
    m_pendingInputControlMessages.clear();
    m_pendingInputControlMessageBytes = 0;
    m_pendingClipboardControlMessages.clear();
    m_pendingClipboardControlMessageBytes = 0;
    m_pendingUploads.clear();
    m_lastSessionHeartbeatSentMs = 0;
    m_remoteDescriptionSet = false;
    m_remoteDescriptionInFlight = false;
    m_pendingRemoteDescriptionData.clear();
    m_pendingRemoteDescriptionType.clear();
    m_pendingRemoteDescriptionObject = QJsonObject();
    m_pendingRemoteCandidates.clear();
    m_pendingClipboardUploadPayloads.clear();
    m_pendingClipboardUploadTargets.clear();
    m_pendingClipboardUploadPasteAfterApply.clear();
    m_pendingClipboardPromiseUploadTargets.clear();
    m_clipboardPromiseUploadSourceToTransferId.clear();
    m_clipboardPromiseUploadSourceToTargetPath.clear();
    m_clipboardPromiseUploadSourceToTotalBytes.clear();
    m_clipboardPromiseUploadSourceToTransferredBytes.clear();
    m_startedClipboardPromiseUploadSources.clear();
    {
        QMutexLocker locker(&m_fileIngressMutex);
        m_fileIngress.clear();
        m_fileIngressBytes = 0;
        m_fileIngressScheduled = false;
        m_fileIngressDrained.wakeAll();
    }
    {
        QMutexLocker locker(&m_videoFrameIngressMutex);
        m_pendingVideoFrameBytes.clear();
        m_pendingVideoFrameTimestampUs = 0;
        m_pendingVideoFrameKind = 0;
        m_videoFrameIngressScheduled = false;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
        m_pendingD3D11VideoFrame = rtc::D3D11VideoFrame{};
#endif
    }
    {
        QMutexLocker locker(&m_transferMutex);
        for (const QString &path : m_pendingClipboardPromiseTargets)
            m_clipboardPromiseDownloadResults.insert(path, false);
        m_pendingClipboardPromiseTargets.clear();
        m_pendingClipboardDownloadPayloads.clear();
        m_pendingClipboardDownloadTargets.clear();
        m_clipboardPromiseDownloadResults.clear();
        m_clipboardPromiseDownloadWait.wakeAll();
        m_clipboardPromiseRemotePathToTransferId.clear();
        m_clipboardPromiseRemotePathToTotalBytes.clear();
        m_clipboardPromiseRemotePathToTransferredBytes.clear();
        m_startedClipboardPromiseRemotePaths.clear();
        m_cancelledTransfers.clear();
        m_clipboardStreamChunks.clear();
        m_clipboardStreamErrors.clear();
        m_pendingClipboardStreamRequests.clear();
        m_clipboardStreamWait.wakeAll();
    }
    m_lastVideoDecodedMs = 0;
    m_videoFeedbackDecodedFrames = 0;
    if (m_inputMoveFlushTimer)
        m_inputMoveFlushTimer->stop();
    if (m_inputMoveTailTimer)
        m_inputMoveTailTimer->stop();
    if (m_sessionHeartbeatTimer)
        m_sessionHeartbeatTimer->stop();
    if (m_peerStartupTimer)
        m_peerStartupTimer->stop();
    resetInputMoveBurst();
    if (m_terminalOutputFlushTimer)
        m_terminalOutputFlushTimer->stop();
    {
        QMutexLocker ingressLocker(&m_fileTextIngressMutex);
        m_fileTextIngress.clear();
        m_fileTextIngressBytes.store(0);
        m_fileTextIngressScheduled = false;
        m_fileTextIngressOverflowed.store(false);
    }
    m_pendingTerminalOutput.clear();
    m_terminalStartRequestId.clear();
    m_latestFileListRequestId.clear();
    m_terminalOutputInFlight = 0;
    m_terminalConsumerBacklog = 0;
    m_terminalFlowPaused = false;
    m_terminalOutputNeedsReset = false;
    stopAudioCapture();
    stopAudioPlayback();
    m_audioMode = QStringLiteral("off");
    LOG_DEBUG("WebRtcCtl shutdown stage terminal/audio stopped: elapsedMs={}", shutdownTimer.elapsed());
    
    
    if (m_inputChannel)
    {
        LOG_DEBUG("Cleaning up input channel");
        m_inputChannel->resetCallbacks();
        m_inputChannel->close();
        m_inputChannel = nullptr;
    }

    if (m_inputMoveChannel)
    {
        LOG_DEBUG("Cleaning up input movement channel");
        m_inputMoveChannel->resetCallbacks();
        m_inputMoveChannel->close();
        m_inputMoveChannel = nullptr;
    }

    if (m_fileChannel)
    {
        LOG_DEBUG("Cleaning up file channel");
        m_fileChannel->resetCallbacks();
        m_fileChannel->close();
        m_fileChannel = nullptr;
    }

    if (m_fileTextChannel)
    {
        LOG_DEBUG("Cleaning up file text channel");
        m_fileTextChannel->resetCallbacks();
        m_fileTextChannel->close();
        m_fileTextChannel = nullptr;
    }

    if (m_clipboardChannel)
    {
        LOG_DEBUG("Cleaning up clipboard channel");
        m_clipboardChannel->resetCallbacks();
        m_clipboardChannel->close();
        m_clipboardChannel = nullptr;
    }

    if (m_heartbeatChannel)
    {
        LOG_DEBUG("Cleaning up session heartbeat channel");
        m_heartbeatChannel->resetCallbacks();
        m_heartbeatChannel->close();
        m_heartbeatChannel = nullptr;
    }
    LOG_DEBUG("WebRtcCtl shutdown stage data channels closed: elapsedMs={}", shutdownTimer.elapsed());

    
    if (m_remoteAudioTrack)
    {
        LOG_DEBUG("Cleaning up remote audio track");
        m_remoteAudioTrack->resetCallbacks();
        m_remoteAudioTrack->close();
        m_remoteAudioTrack = nullptr;
    }

    if (m_localAudioTrack)
    {
        LOG_DEBUG("Cleaning up local audio track");
        m_localAudioTrack->resetCallbacks();
        m_localAudioTrack->close();
        m_localAudioTrack = nullptr;
    }

    if (m_videoTrack)
    {
        LOG_DEBUG("Cleaning up video track");
        m_videoTrack->resetCallbacks();
        m_videoTrack->close();
        m_videoTrack = nullptr;
    }

    
    if (m_peerConnection)
    {
        LOG_DEBUG("Cleaning up peer connection");
        m_peerConnection->resetCallbacks();
        m_peerConnection->close();
        m_peerConnection = nullptr;
    }
    LOG_DEBUG("WebRtcCtl shutdown stage peer connection closed: elapsedMs={}", shutdownTimer.elapsed());

    if (m_filePacketUtil)
    {
        m_filePacketUtil = nullptr;
    }

    QSet<QString> clipboardCacheRoots;
    {
        QMutexLocker locker(&m_transferMutex);
        clipboardCacheRoots = std::move(m_clipboardCacheRoots);
        m_clipboardCacheRoots.clear();
    }
    for (const QString &cacheRoot : clipboardCacheRoots)
    {
        ClipboardFilePromise::cancelCacheRoot(cacheRoot);
        ClipboardUtil::cleanupCacheRootAsync(cacheRoot);
    }
    LOG_INFO("WebRtcCtl destroyed: elapsedMs={}", shutdownTimer.elapsed());
}
