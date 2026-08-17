#include "webrtc/cli/webrtc_cli.h"

#include "terminal/session/terminal_session.h"
#include "util/clipboard/clipboard_util.h"
#include "util/clipboard/native/clipboard_file_promise.h"
#include "util/file/file_packet_util.h"
#include "security/audit_session.h"
#include "security/terminal_command_audit_parser.h"

#include <QMutexLocker>
#include <QElapsedTimer>

void WebRtcCli::wakeClipboardWaitersForShutdown()
{
    QMutexLocker locker(&m_transferMutex);
    for (const QString &path : m_pendingClipboardPromiseTargets)
        m_clipboardPromiseFileResults.insert(path, false);
    for (const QString &requestId : m_pendingClipboardStreamRequests.keys())
        m_clipboardStreamErrors.insert(requestId, QStringLiteral("Session closed."));
    m_clipboardPromiseFileWait.wakeAll();
    m_clipboardStreamWait.wakeAll();
}


void WebRtcCli::performDestroy()
{
    QElapsedTimer shutdownTimer;
    shutdownTimer.start();
    LOG_DEBUG("WebRtcCli shutdown started: session={}", m_sessionId);
    m_shutdownRequested.store(true);
    if (m_shutdownStarted.exchange(true))
        return;
    if (!m_disconnectSent && !m_remoteDisconnectReceived)
        sendSessionDisconnect(m_disconnectReason);
    ++m_fileListGeneration;
    m_fileListScanPending = false;
    m_pendingFileListRequestId.clear();
    if (m_auditSession)
    {
        m_auditSession->finish(m_disconnectReason);
        emit controlledSessionDisconnected(m_sessionId, m_remoteId, m_disconnectReason);
    }
    wakeClipboardWaitersForShutdown();
    m_callbackLifetime->closeAndWait();
    LOG_DEBUG("WebRtcCli shutdown stage callback-lifetime closed: session={}, elapsedMs={}",
              m_sessionId, shutdownTimer.elapsed());
    m_destroying = true;
    m_peerConnected.store(false);
    failClipboardChunkSendQueue();
    clearClipboardPayloadTransferState();
    m_subscribed = false;
    m_mediaStatsQueryInFlight.store(false);
    m_connected = false;
    m_channelsReady = false;
    m_remoteDescriptionSet = false;
    m_remoteDescriptionInFlight = false;
    m_pendingRemoteDescriptionData.clear();
    m_pendingRemoteDescriptionType.clear();
    m_pendingRemoteDescriptionObject = QJsonObject();
    m_pendingRemoteCandidates.clear();
    {
        QMutexLocker locker(&m_fileIngressMutex);
        m_fileIngress.clear();
        m_fileIngressBytes = 0;
        m_fileIngressScheduled = false;
        m_fileIngressDrained.wakeAll();
    }
    {
        QMutexLocker locker(&m_captureStateIngressMutex);
        m_pendingCaptureState = PendingCaptureState();
        m_captureStateIngressScheduled = false;
    }
    {
        QMutexLocker locker(&m_inputMoveIngressMutex);
        m_pendingInputMoveMessage = QJsonObject();
        m_pendingInputMoveBoundary = false;
        m_inputMoveIngressScheduled = false;
    }
    {
        QMutexLocker locker(&m_fileTextIngressMutex);
        m_fileTextIngress.clear();
        m_fileTextIngressBytes = 0;
        m_fileTextIngressScheduled = false;
        m_fileTextIngressOverflowed.store(false);
    }
    {
        QMutexLocker locker(&m_transferMutex);
        for (const QString &path : m_pendingClipboardPromiseTargets)
            m_clipboardPromiseFileResults.insert(path, false);
        m_pendingClipboardPromiseTargets.clear();
        const auto streamKeys = m_clipboardStreamChunks.keys();
        for (const QString &requestId : streamKeys)
            m_clipboardStreamErrors.insert(requestId, QStringLiteral("Session closed."));
        m_clipboardStreamChunks.clear();
        m_pendingClipboardStreamRequests.clear();
        m_cancelledTransfers.clear();
        m_clipboardPromiseFileWait.wakeAll();
        m_clipboardStreamWait.wakeAll();
    }
    if (m_inputChannelRecoverTimer)
        m_inputChannelRecoverTimer->stop();
    if (m_peerStartupTimer)
        m_peerStartupTimer->stop();
    if (m_streamConfigApplyTimer)
        m_streamConfigApplyTimer->stop();
    if (m_streamConfigNotifyTimer)
        m_streamConfigNotifyTimer->stop();
    m_pendingStreamConfig = QJsonObject();
    m_lastStreamConfigNotifyMs = 0;
    if (m_controlWatchdogTimer)
        m_controlWatchdogTimer->stop();
    if (m_sessionHeartbeatTimer)
        m_sessionHeartbeatTimer->stop();
    if (m_disconnectGraceTimer)
        m_disconnectGraceTimer->stop();
    if (m_mediaStatsTimer)
        m_mediaStatsTimer->stop();
    if (m_clipboardSnapshotTimer)
        m_clipboardSnapshotTimer->stop();
    m_lastSentClipboardPayloadSignature.clear();
    m_lastRemoteAppliedClipboardPayloadSignature.clear();
    m_clipboardAutoSyncAllowedUntilMs = 0;
    m_lastMouseMoveSequence = 0;
    m_mouseMoveBoundaryReady = false;

    stopAudioCapture();
    stopAudioPlayback();
    m_audioCaptureEnabled = false;
    m_audioMode = QStringLiteral("off");
    m_currentCaptureMethod.clear();
    m_currentCaptureBackend.clear();
    m_currentCapturePath.clear();
    m_currentEncodePath.clear();
    m_currentFallbackReason.clear();
    m_currentEncoderName.clear();
    m_currentEncoderType.clear();

    if (m_terminalSession)
    {
        ++m_terminalSessionGeneration;
        m_terminalSession->stop();
        m_terminalSession->deleteLater();
        m_terminalSession = nullptr;
    }
    LOG_DEBUG("WebRtcCli shutdown stage terminal stopped: session={}, elapsedMs={}",
              m_sessionId, shutdownTimer.elapsed());
    m_pendingTerminalOutputChunks.clear();
    m_pendingTerminalOutputBytes = 0;
    m_pendingTerminalEndType.clear();
    m_pendingTerminalEndError.clear();
    m_pendingTerminalEndRequestId.clear();
    m_pendingTerminalExitCode = 0;
    m_pendingFileTextMessages.clear();
    m_pendingFileTextMessageBytes = 0;
    m_pendingClipboardControlMessages.clear();
    m_pendingClipboardControlMessageBytes = 0;
    m_lastSessionHeartbeatSentMs = 0;
    m_pendingInputMessages.clear();
    m_pendingInputMessageBytes = 0;
    m_pendingDownloads.clear();
    m_activeFileMutationTasks = 0;
    m_activeClipboardExpansionTasks = 0;
    m_activeClipboardStreamReadRequests.clear();
    m_terminalAuditParser.reset();
    m_auditedClipboardDownloads.clear();

    auto closeChannel = [](std::shared_ptr<rtc::DataChannel> &channel) {
        if (!channel)
            return;
        try
        {
            channel->resetCallbacks();
            channel->close();
        }
        catch (...)
        {
        }
        channel.reset();
    };

    closeChannel(m_inputChannel);
    closeChannel(m_inputMoveChannel);
    closeChannel(m_fileChannel);
    closeChannel(m_fileTextChannel);
    closeChannel(m_clipboardChannel);
    closeChannel(m_heartbeatChannel);
    LOG_DEBUG("WebRtcCli shutdown stage data channels closed: session={}, elapsedMs={}",
              m_sessionId, shutdownTimer.elapsed());

    auto closeTrack = [](std::shared_ptr<rtc::Track> &track) {
        if (!track)
            return;
        try
        {
            track->resetCallbacks();
            track->setAiranCaptureCallback(nullptr);
            track->setMediaHandler(nullptr);
            track->close();
        }
        catch (...)
        {
        }
        track.reset();
    };

    closeTrack(m_videoTrack);
    closeTrack(m_audioTrack);

    if (m_peerConnection)
    {
        try
        {
            m_peerConnection->resetCallbacks();
            m_peerConnection->close();
        }
        catch (...)
        {
        }
        m_peerConnection.reset();
    }
    LOG_DEBUG("WebRtcCli shutdown stage peer connection closed: session={}, elapsedMs={}",
              m_sessionId, shutdownTimer.elapsed());

    if (m_filePacketUtil)
    {
        m_filePacketUtil->disconnect();
        m_filePacketUtil->deleteLater();
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
    m_uploadFragments.clear();
    LOG_INFO("WebRtcCli destroyed: session={}, elapsedMs={}", m_sessionId, shutdownTimer.elapsed());
}

void WebRtcCli::requestShutdown()
{
    m_shutdownRequested.store(true);
}

void WebRtcCli::destroy()
{
    performDestroy();
    emit shutdownFinished();
    disconnect();
}

void WebRtcCli::shutdownAndMoveToOwnerThread(QObject *owner)
{
    performDestroy();
    if (owner)
        moveToThread(owner->thread());
    emit shutdownFinished();
    disconnect();
}
