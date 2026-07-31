#include "terminal_window.h"

#include "common/constant.h"
#include "terminal/emulator/native_terminal_widget.h"
#include "util/json/json_util.h"

void TerminalWindow::tryStartTerminal()
{
    if (m_started || !m_channelReady || !m_terminal)
        return;

    m_started = true;
    const QSize grid = m_terminal->gridSize();
    QJsonObject msg = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_TERMINAL_START)
                          .add(Constant::KEY_COLS, grid.width())
                          .add(Constant::KEY_ROWS, grid.height())
                          .build();
    emit fileTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(msg).toStdString()));
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

    QJsonObject msg = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_FILE_LIST)
                          .add(Constant::KEY_PATH, path)
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
    if (m_remoteOs == QStringLiteral("windows"))
    {
        const QString shell = m_remoteShell.toLower();
        if (shell.contains(QStringLiteral("powershell")) || shell.contains(QStringLiteral("pwsh")))
        {
            script = "function global:prompt { $p=(Get-Location).Path; $u='file:///'+$p.Replace('\\\\','/').Replace(' ','%20'); [Console]::Write(\"`e]7;$u`a\"); \"PS $p> \" }\r";
        }
        else
        {
            if (m_terminal)
                m_terminal->setPromptEchoFiltering(true);
            script = "prompt $E]7;file:///$P$E\\$G$S$P$G\r";
        }
    }
    else
    {
        script = "export AIRAN_OLD_PROMPT_COMMAND=\"$PROMPT_COMMAND\"\n"
                 "export PROMPT_COMMAND='printf \"\\033]7;file://localhost%s\\007\" \"$PWD\"; if [ -n \"$AIRAN_OLD_PROMPT_COMMAND\" ]; then eval \"$AIRAN_OLD_PROMPT_COMMAND\"; fi'\n";
    }
    sendTerminalInput(script);
}
