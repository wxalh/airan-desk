#include "terminal_window.h"

#include "common/constant.h"
#include "terminal/emulator/native_terminal_widget.h"
#include "terminal/file_panel/terminal_file_panel.h"
#include "util/json/json_util.h"

#include <QTimer>
#include <QUuid>

namespace
{
constexpr int kTerminalStartTimeoutMs = 5000;
}

void TerminalWindow::tryStartTerminal()
{
    if (m_started || !m_channelReady || !m_terminal)
        return;

    m_started = true;
    m_terminalFallbackRequested = false;
    m_terminalLegacyResponseMode = false;
    m_terminalStartRequestId.clear();
    sendTerminalStart(false);
}


void TerminalWindow::sendTerminalStart(bool requestFallback)
{
    if (!m_channelReady || !m_terminal)
        return;

    const QSize grid = m_terminal->gridSize();
    m_terminalStartRequestId = QString::number(++m_terminalStartGeneration);
    QJsonObject msg = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_TERMINAL_START)
                          .add(Constant::KEY_COLS, grid.width())
                          .add(Constant::KEY_ROWS, grid.height())
                          .add(Constant::KEY_REQUEST_ID, m_terminalStartRequestId)
                          .add(Constant::KEY_TERMINAL_MODE,
                               requestFallback ? QStringLiteral("fallback") : QStringLiteral("auto"))
                          .build();
    m_terminalLegacyResponseMode = false;
    emit fileTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(msg).toStdString()));
    if (m_terminalStartTimer)
        m_terminalStartTimer->start(kTerminalStartTimeoutMs);
}


void TerminalWindow::onTerminalStartTimeout()
{
    if (isClosing() || !m_started || !m_channelReady || !m_terminal)
        return;

    if (!m_terminalFallbackRequested)
    {
        m_terminalFallbackRequested = true;
        m_terminal->showStatusLine(tr("[Terminal startup timed out; retrying in compatibility mode...]"));
        sendTerminalStart(true);
        return;
    }

    m_started = false;
    m_terminalFallbackRequested = false;
    if (m_terminalStartTimer)
        m_terminalStartTimer->stop();
    QJsonObject stop = JsonUtil::createObject()
                           .add(Constant::KEY_MSGTYPE, Constant::TYPE_TERMINAL_STOP)
                           .add(Constant::KEY_REQUEST_ID, m_terminalStartRequestId)
                           .build();
    emit fileTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(stop).toStdString()));
    m_terminal->showStatusLine(tr("[Terminal failed to start after compatibility fallback]"));
}


void TerminalWindow::sendTerminalResize(const QSize &gridSize)
{
    if (!m_started)
        return;

    QJsonObject msg = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_TERMINAL_RESIZE)
                          .add(Constant::KEY_COLS, gridSize.width())
                          .add(Constant::KEY_ROWS, gridSize.height())
                          .build();
    emit fileTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(msg).toStdString()));
}


void TerminalWindow::sendTerminalInput(const QByteArray &data)
{
    if (!m_started || data.isEmpty())
        return;

    QJsonObject msg = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_TERMINAL_INPUT)
                          .add(Constant::KEY_ENCODING, QStringLiteral("base64"))
                          .add(Constant::KEY_DATA, QString::fromLatin1(data.toBase64()))
                          .build();
    emit fileTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(msg).toStdString()));
}


void TerminalWindow::requestFileList(const QString &path)
{
    if (path.isEmpty())
        return;

    m_pendingFileListRequestId = QUuid::createUuid().toString();
    m_pendingFileListRequestId.remove(QLatin1Char('{'));
    m_pendingFileListRequestId.remove(QLatin1Char('}'));
    if (m_filePanel)
        m_filePanel->setPendingFileListRequestId(m_pendingFileListRequestId);
    QJsonObject msg = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_LIST)
                          .add(Constant::KEY_PATH, path)
                          .add(Constant::KEY_REQUEST_ID, m_pendingFileListRequestId)
                          .build();
    emit filePanelTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(msg).toStdString()));
}


void TerminalWindow::requestDownload(const QString &remotePath, const QString &localPath, bool isDirectory, const QString &transferId)
{
    if (remotePath.isEmpty() || localPath.isEmpty())
        return;

    QJsonObject msg = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_DOWNLOAD)
                          .add(Constant::KEY_PATH_CLI, remotePath)
                          .add(Constant::KEY_PATH_CTL, localPath)
                          .add(Constant::KEY_TRANSFER_ID, transferId)
                          .add("isDirectory", isDirectory)
                          .build();
    emit filePanelTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(msg).toStdString()));
}


void TerminalWindow::requestUpload(const QString &localPath, const QString &remotePath, bool, const QString &transferId)
{
    if (localPath.isEmpty() || remotePath.isEmpty())
        return;

    emit uploadFile2CLI(localPath, remotePath, transferId);
}


void TerminalWindow::injectPathTracking()
{
    if (!m_started || !m_remotePathTracking)
        return;

    QByteArray script;
    if (m_terminal)
        m_terminal->setPromptEchoFiltering(true);
    if (m_remoteOs == QStringLiteral("windows"))
    {
        const QString shell = m_remoteShell.toLower();
        if (shell.contains(QStringLiteral("powershell")) || shell.contains(QStringLiteral("pwsh")))
        {
            script = "function global:prompt { $p=(Get-Location).Path; $u='file://localhost/'+$p.Replace('\\','/').Replace('%','%25').Replace(' ','%20').Replace('#','%23').Replace('?','%3F'); [Console]::Write(\"`e]7;$u`a\"); \"PS $p> \" }\r";
        }
        else
        {
            script = "prompt $E]7;file:///$P$E\\$G$S$P$G\r";
        }
    }
    else
    {
        script = "export AIRAN_OLD_PROMPT_COMMAND=\"$PROMPT_COMMAND\"\n"
                 "export PROMPT_COMMAND='__airan_pwd=$PWD; __airan_pwd=${__airan_pwd//%/%25}; __airan_pwd=${__airan_pwd// /%20}; __airan_pwd=${__airan_pwd//#/%23}; __airan_pwd=${__airan_pwd//\\?/%3F}; printf \"\\033]7;file://localhost%s\\007\" \"$__airan_pwd\"; if [ -n \"$AIRAN_OLD_PROMPT_COMMAND\" ]; then eval \"$AIRAN_OLD_PROMPT_COMMAND\"; fi'\n";
    }
    sendTerminalInput(script);
}
