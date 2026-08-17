#include "terminal_window.h"

#include "terminal/file_panel/terminal_file_panel.h"
#include "terminal/emulator/native_terminal_widget.h"

#include <QMessageBox>
#include <QTimer>


void TerminalWindow::initCLI()
{
    m_terminalStartTimer = new QTimer(this);
    m_terminalStartTimer->setSingleShot(true);
    connect(m_terminalStartTimer, &QTimer::timeout,
            this, &TerminalWindow::onTerminalStartTimeout);

    connect(&m_rtcCtl, &WebRtcCtl::sendWsCliBinaryMsg, m_ws, &WsCli::sendWsCliBinaryMsg);
    connect(&m_rtcCtl, &WebRtcCtl::sendWsCliTextMsg, m_ws, &WsCli::sendWsCliTextMsg);
    connect(m_ws, &WsCli::onWsCliRecvBinaryMsg, &m_rtcCtl, &WebRtcCtl::onWsCliRecvBinaryMsg);
    connect(m_ws, &WsCli::onWsCliRecvTextMsg, &m_rtcCtl, &WebRtcCtl::onWsCliRecvTextMsg);
    connect(this, &TerminalWindow::initRtcCtl, &m_rtcCtl, &WebRtcCtl::init);
    connect(this, &TerminalWindow::fileTextChannelSendMsg, &m_rtcCtl, &WebRtcCtl::fileTextChannelSendMsg);
    connect(this, &TerminalWindow::filePanelTextChannelSendMsg, &m_rtcCtl, &WebRtcCtl::fileTextChannelSendMsg);
    connect(this, &TerminalWindow::uploadFile2CLI, &m_rtcCtl, &WebRtcCtl::uploadFile2CLI);
    connect(m_terminal, &NativeTerminalWidget::outputConsumed,
            &m_rtcCtl, &WebRtcCtl::acknowledgeTerminalOutput);
    connect(this, &TerminalWindow::terminalConsumerBacklogChanged, &m_rtcCtl, &WebRtcCtl::setTerminalConsumerBacklog);
    connect(&m_rtcCtl, &WebRtcCtl::fileTextChannelOpened, this, &TerminalWindow::onFileTextChannelOpened);
    connect(&m_rtcCtl, &WebRtcCtl::terminalOutput,
            m_terminal, &NativeTerminalWidget::writePtyOutput,
            Qt::DirectConnection);
    connect(&m_rtcCtl, &WebRtcCtl::terminalInfo, this, &TerminalWindow::onTerminalInfo);
    connect(&m_rtcCtl, &WebRtcCtl::terminalClosed, this, &TerminalWindow::onTerminalClosed);
    connect(&m_rtcCtl, &WebRtcCtl::terminalError, this, &TerminalWindow::onTerminalError);
    connect(&m_rtcCtl, &WebRtcCtl::sessionHealthChanged,
            this, &TerminalWindow::onTerminalSessionHealthChanged);
    connect(&m_rtcCtl, &WebRtcCtl::sessionHealthChanged,
            this, &TerminalWindow::onFilePanelSessionHealthChanged);
    connect(&m_rtcCtl, &WebRtcCtl::recvGetFileList, m_filePanel, &TerminalFilePanel::recvGetFileList);
    connect(&m_rtcCtl, &WebRtcCtl::recvDownloadFile, m_filePanel, &TerminalFilePanel::recvDownloadFile);
    connect(&m_rtcCtl, &WebRtcCtl::recvUploadFileRes, m_filePanel, &TerminalFilePanel::recvUploadFile);
    connect(&m_rtcCtl, &WebRtcCtl::recvRenameFileRes, m_filePanel, &TerminalFilePanel::recvRenameFile);
    connect(&m_rtcCtl, &WebRtcCtl::recvDeleteFileRes, m_filePanel, &TerminalFilePanel::recvDeleteFile);
    connect(&m_rtcCtl, &WebRtcCtl::recvCreateFileRes, m_filePanel, &TerminalFilePanel::recvCreateFile);
    connect(&m_rtcCtl, &WebRtcCtl::fileTransferStarted, m_filePanel, &TerminalFilePanel::onTransferStarted);
    connect(&m_rtcCtl, &WebRtcCtl::fileTransferProgress, m_filePanel, &TerminalFilePanel::onTransferProgress);
    connect(&m_rtcCtl, &WebRtcCtl::remoteDisconnectRequested,
            this, &TerminalWindow::onRemoteDisconnectRequested);

    connect(&m_rtcCtl, &WebRtcCtl::shutdownFinished, &m_rtcThread, &QThread::quit, Qt::DirectConnection);
    connect(&m_rtcThread, &QThread::finished, this, &TerminalWindow::finalizeCloseWhenStopped);

    m_rtcThread.setObjectName("TerminalWindow-WebRtcCtlThread");
    m_rtcCtl.moveToThread(&m_rtcThread);
    m_rtcThread.start();
}

void TerminalWindow::onTerminalSessionHealthChanged(int state, const QString &message)
{
    if (isClosing() || !m_terminal)
        return;
    if (state == 2)
    {
        if (m_terminalStartTimer)
            m_terminalStartTimer->stop();
        m_channelReady = false;
        m_started = false;
        m_terminalFallbackRequested = false;
        m_terminalLegacyResponseMode = false;
        if (m_filePanel)
            m_filePanel->setPendingFileListRequestId(QString());
    }
    else if (state == 0 && m_channelReady)
    {
        tryStartTerminal();
    }
    m_terminal->showStatusLine(message);
}

void TerminalWindow::onFilePanelSessionHealthChanged(int state, const QString &message)
{
    if (isClosing())
        return;
    if (state == 2)
    {
        m_filePanel->setConnected(false);
        m_filePanel->abortTransfers(message);
    }
    else if (state == 1 || state == 0)
    {
        m_filePanel->setConnected(true);
    }
}

void TerminalWindow::onRemoteDisconnectRequested(const QString &reason, bool peerWide)
{
    Q_UNUSED(reason);
    if (isClosing())
        return;
    if (!peerWide)
        QMessageBox::information(this, tr("Connection closed"),
                                 tr("The remote device closed this connection."));
    beginAsyncShutdown();
}
