#include "ui/control/control_window.h"

#include "ui/video/d3d11_video_widget.h"

#include <QMessageBox>


void ControlWindow::initCLI()
{
    connect(&m_rtc_ctl, &WebRtcCtl::sendWsCliBinaryMsg, m_ws, &WsCli::sendWsCliBinaryMsg);
    connect(&m_rtc_ctl, &WebRtcCtl::sendWsCliTextMsg, m_ws, &WsCli::sendWsCliTextMsg);
    connect(m_ws, &WsCli::onWsCliRecvBinaryMsg, &m_rtc_ctl, &WebRtcCtl::onWsCliRecvBinaryMsg);
    connect(m_ws, &WsCli::onWsCliRecvTextMsg, &m_rtc_ctl, &WebRtcCtl::onWsCliRecvTextMsg);

    
    connect(this, &ControlWindow::initRtcCtl, &m_rtc_ctl, &WebRtcCtl::init);
    connect(this, &ControlWindow::sendMsg2InputChannel, &m_rtc_ctl, &WebRtcCtl::inputChannelSendMsg);
    connect(this, &ControlWindow::pasteClipboardPayloadToRemote, &m_rtc_ctl, &WebRtcCtl::pasteClipboardPayloadToRemote);
    connect(this, &ControlWindow::syncClipboardPayloadToRemote, &m_rtc_ctl, &WebRtcCtl::syncClipboardPayloadToRemote);
    connect(this, &ControlWindow::requestRemoteClipboardSnapshot, &m_rtc_ctl, &WebRtcCtl::requestRemoteClipboardSnapshot);

    connect(&m_rtc_ctl, &WebRtcCtl::videoFrameDecoded,
            this, &ControlWindow::enqueueVideoFrame, Qt::DirectConnection);
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    connect(&m_rtc_ctl, &WebRtcCtl::videoFrameD3D11Decoded,
            this, &ControlWindow::enqueueD3D11VideoFrame, Qt::DirectConnection);
#endif
    connect(&m_rtc_ctl, &WebRtcCtl::videoStatsUpdated, this, &ControlWindow::updateVideoStats);
    connect(&m_rtc_ctl, &WebRtcCtl::networkPathStateChanged, this, &ControlWindow::onNetworkPathStateChanged);
    connect(&m_rtc_ctl, &WebRtcCtl::remoteEncoderChanged, this, &ControlWindow::onRemoteEncoderChanged);
    connect(&m_rtc_ctl, &WebRtcCtl::remoteMediaStateChanged, this, &ControlWindow::onRemoteMediaStateChanged);
    connect(&m_rtc_ctl, &WebRtcCtl::remoteScreensChanged, this, &ControlWindow::onRemoteScreensChanged);
    connect(&m_rtc_ctl, &WebRtcCtl::audioModeRequestFinished, this, &ControlWindow::onAudioModeRequestFinished);
    connect(&m_rtc_ctl, &WebRtcCtl::desktopStateChanged, this, &ControlWindow::onRemoteDesktopStateChanged);
    connect(&m_rtc_ctl, &WebRtcCtl::connectionStatusChanged, this, &ControlWindow::onConnectionStatusChanged);
    connect(&m_rtc_ctl, &WebRtcCtl::sessionHealthChanged, this, &ControlWindow::onSessionHealthChanged);
    connect(&m_rtc_ctl, &WebRtcCtl::remoteDisconnectRequested,
            this, &ControlWindow::onRemoteDisconnectRequested);
    connect(&m_rtc_ctl, &WebRtcCtl::remoteOsChanged, this, &ControlWindow::onRemoteOsChanged);
    connect(&m_rtc_ctl, &WebRtcCtl::localClipboardPayloadReceived, this, &ControlWindow::applyLocalClipboardPayload);
    connect(&m_rtc_ctl, &WebRtcCtl::fileTransferStarted, this, &ControlWindow::onTransferStarted);
    connect(&m_rtc_ctl, &WebRtcCtl::fileTransferProgress, this, &ControlWindow::onTransferProgress);
    connect(&m_rtc_ctl, &WebRtcCtl::recvUploadFileRes, this, &ControlWindow::onTransferFinished);
    connect(&m_rtc_ctl, &WebRtcCtl::recvDownloadFile,
            this, &ControlWindow::onDownloadFinished);

    connect(&m_rtc_ctl, &WebRtcCtl::shutdownFinished, &m_rtc_ctl_thread, &QThread::quit, Qt::DirectConnection);
    connect(&m_rtc_ctl_thread, &QThread::finished, this, &ControlWindow::finalizeCloseWhenStopped);

    m_rtc_ctl_thread.setObjectName("ControlWindow-WebRtcCtlThread");
    m_rtc_ctl.moveToThread(&m_rtc_ctl_thread);
    m_rtc_ctl_thread.start();
}

void ControlWindow::onRemoteDisconnectRequested(const QString &reason, bool peerWide)
{
    Q_UNUSED(reason);
    if (isClosing())
        return;
    if (!peerWide)
        QMessageBox::information(this, tr("Connection closed"),
                                 tr("The remote device closed this connection."));
    beginAsyncShutdown();
}

void ControlWindow::onDownloadFinished(bool status, const QString &filePath)
{
    onTransferFinished(status, filePath, QString());
}
