#include "terminal_window.h"

#include "common/constant.h"
#include "terminal/emulator/native_terminal_widget.h"
#include "terminal/file_panel/terminal_file_panel.h"
#include "util/config/config_util.h"

#include <QMessageBox>


void TerminalWindow::onFileTextChannelOpened()
{
    if (isClosing())
        return;
    m_channelReady = true;
    tryStartTerminal();
    m_filePanel->setConnected(true);
    requestFileList(m_filePanel->currentRemotePath().isEmpty()
                        ? Constant::FOLDER_HOME
                        : m_filePanel->currentRemotePath());
}


void TerminalWindow::onTerminalInfo(const QString &osName,
                                    const QString &shellPath,
                                    const QString &mode,
                                    bool pathTracking,
                                    bool pathTrackingReady)
{
    if (isClosing())
        return;
    m_remoteOs = osName;
    m_remoteShell = shellPath;
    m_remoteTerminalMode = mode;
    m_remotePathTracking = pathTracking;
    if (m_terminal)
    {
        const bool pipeMode = m_remoteTerminalMode == QStringLiteral("pipe");
        m_terminal->setLocalEchoEnabled(pipeMode);
        m_terminal->setPipeMode(pipeMode);
        m_terminal->setFocus(Qt::OtherFocusReason);
    }
    if (m_remotePathTracking && !pathTrackingReady)
        injectPathTracking();
}


void TerminalWindow::onTerminalClosed(int exitCode)
{
    if (isClosing())
        return;
    m_terminal->showStatusLine(tr("[Remote terminal exited with code %1]").arg(exitCode));
}


void TerminalWindow::onTerminalError(const QString &message)
{
    if (isClosing())
        return;
    m_terminal->showStatusLine(tr("[Terminal error] %1").arg(message));
    if (RuntimeEnvironment::uiAvailable())
        QMessageBox::warning(this, tr("Terminal Error"), message);
}
