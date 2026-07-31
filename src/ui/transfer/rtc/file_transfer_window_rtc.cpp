#include "ui/transfer/file_transfer_window.h"

#include "common/constant.h"

#include <QMessageBox>


void FileTransferWindow::initCLI()
{
    WebRtcCtl *rtcCtl = &m_rtc_ctl;
    connect(&m_rtc_ctl, &WebRtcCtl::sendWsCliBinaryMsg, m_ws, &WsCli::sendWsCliBinaryMsg);
    connect(&m_rtc_ctl, &WebRtcCtl::sendWsCliTextMsg, m_ws, &WsCli::sendWsCliTextMsg);
    connect(m_ws, &WsCli::onWsCliRecvBinaryMsg, &m_rtc_ctl, &WebRtcCtl::onWsCliRecvBinaryMsg);
    connect(m_ws, &WsCli::onWsCliRecvTextMsg, &m_rtc_ctl, &WebRtcCtl::onWsCliRecvTextMsg);
    connect(this, &FileTransferWindow::initRtcCtl, &m_rtc_ctl, &WebRtcCtl::init);

    connect(&m_rtc_ctl, &WebRtcCtl::shutdownFinished, &m_rtc_ctl_thread, &QThread::quit, Qt::DirectConnection);
    connect(&m_rtc_ctl_thread, &QThread::finished, this, &FileTransferWindow::finalizeCloseWhenStopped);
    connect(&m_rtc_ctl, &WebRtcCtl::remoteDisconnectRequested,
            this, &FileTransferWindow::onRemoteDisconnectRequested);

    m_rtc_ctl_thread.setObjectName("FileTransferWindow-WebRtcCtlThread");
    m_rtc_ctl.moveToThread(&m_rtc_ctl_thread);
    m_rtc_ctl_thread.start();

    connect(this, &FileTransferWindow::inputChannelSendMsg, rtcCtl, &WebRtcCtl::inputChannelSendMsg);
    connect(this, &FileTransferWindow::fileChannelSendMsg, rtcCtl, &WebRtcCtl::fileChannelSendMsg);
    connect(this, &FileTransferWindow::fileTextChannelSendMsg, rtcCtl, &WebRtcCtl::fileTextChannelSendMsg);
    connect(this, &FileTransferWindow::uploadFile2CLI, rtcCtl, &WebRtcCtl::uploadFile2CLI);
    connect(this,
            &FileTransferWindow::cancelFileTransfer,
            rtcCtl,
            &WebRtcCtl::cancelFileTransfer,
            Qt::DirectConnection);

    connect(rtcCtl, &WebRtcCtl::recvGetFileList, this, &FileTransferWindow::recvGetFileList);
    connect(rtcCtl, &WebRtcCtl::recvDownloadFile, this, &FileTransferWindow::recvDownloadFile);
    connect(rtcCtl, &WebRtcCtl::recvUploadFileRes, this, &FileTransferWindow::recvUploadFileRes);
    connect(rtcCtl, &WebRtcCtl::recvDeleteFileRes, this, &FileTransferWindow::recvDeleteFileRes);
    connect(rtcCtl, &WebRtcCtl::recvRenameFileRes, this, &FileTransferWindow::recvRenameFileRes);
    connect(rtcCtl, &WebRtcCtl::recvCreateFileRes, this, &FileTransferWindow::recvCreateFileRes);
    connect(rtcCtl, &WebRtcCtl::fileTransferStarted, this, &FileTransferWindow::onTransferStarted);
    connect(rtcCtl, &WebRtcCtl::fileTransferProgress, this, &FileTransferWindow::onTransferProgress);
    connect(rtcCtl, &WebRtcCtl::fileTextChannelOpened,
            this, &FileTransferWindow::onFileTextChannelOpened);
    connect(rtcCtl, &WebRtcCtl::sessionHealthChanged,
            this, &FileTransferWindow::onSessionHealthChanged);
}

void FileTransferWindow::onFileTextChannelOpened()
{
    connected = true;
    requestRemoteFileList(currentRemotePath.isEmpty()
                              ? Constant::FOLDER_HOME
                              : currentRemotePath);
}

void FileTransferWindow::onSessionHealthChanged(int state, const QString &message)
{
    if (isClosing())
        return;
    connected = false;
    if (state == 2)
        failActiveTransfers(message);
}

void FileTransferWindow::onRemoteDisconnectRequested(const QString &reason, bool peerWide)
{
    Q_UNUSED(reason);
    if (isClosing())
        return;
    if (!peerWide)
        QMessageBox::information(this, tr("Connection closed"),
                                 tr("The remote device closed this connection."));
    beginAsyncShutdown();
}
