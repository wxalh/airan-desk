#include "webrtc/ctl/webrtc_ctl.h"
#include "util/file/file_packet_util.h"
#include "util/clipboard/clipboard_util.h"
#include "util/clipboard/native/clipboard_file_promise.h"

#include <QMutexLocker>

void WebRtcCtl::destroy()
{
    if (m_shutdownStarted.exchange(true))
        return;
    m_callbackLifetime->closeAndWait();
    failClipboardChunkSendQueue();
    clearClipboardPayloadTransferState();
    LOG_DEBUG("WebRtcCtl destroy started");
    m_connected = false;
    m_remoteDescriptionSet = false;
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
    resetInputMoveBurst();
    if (m_terminalOutputFlushTimer)
        m_terminalOutputFlushTimer->stop();
    {
        QMutexLocker ingressLocker(&m_fileTextIngressMutex);
        m_fileTextIngress.clear();
        m_fileTextIngressBytes.store(0);
        m_fileTextIngressScheduled = false;
    }
    m_pendingTerminalOutput.clear();
    m_terminalOutputInFlight = 0;
    m_terminalConsumerBacklog = 0;
    m_terminalFlowPaused = false;
    stopAudioCapture();
    stopAudioPlayback();
    m_audioMode = QStringLiteral("off");
    
    
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
    m_clipboardInboundTextChunks.clear();
    m_clipboardInboundTextChunkCounts.clear();
    m_clipboardInboundTextNextIndexes.clear();
    m_clipboardInboundTextExpectedBytes.clear();
    m_clipboardInboundTextPasteAfterApply.clear();
    LOG_INFO("WebRtcCtl destroyed");
}
