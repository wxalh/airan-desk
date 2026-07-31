#include "webrtc/cli/webrtc_cli.h"

#include "terminal/session/terminal_session.h"
#include "common/constant.h"
#include "util/json/json_util.h"
#include "security/audit_session.h"
#include "security/terminal_command_audit_parser.h"

#include <QRegularExpression>

namespace
{
constexpr uint64_t kTerminalChannelHighWatermark = 256 * 1024;
constexpr uint64_t kTerminalChannelLowWatermark = 64 * 1024;
constexpr int kMaxTerminalInputEncodedBytes = 1024 * 1024;
} /* namespace */


void WebRtcCli::handleTerminalMessage(const QJsonObject &object)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, "handleTerminalMessage",
                                  Qt::QueuedConnection,
                                  Q_ARG(QJsonObject, object));
        return;
    }

    const QString msgType = JsonUtil::getString(object, Constant::KEY_MSGTYPE);
    if (msgType == Constant::TYPE_TERMINAL_START)
    {
        if (m_auditSession)
            m_auditSession->recordTerminalConnected();
        if (m_terminalBackpressureTimer)
            m_terminalBackpressureTimer->stop();
        m_pendingTerminalOutputChunks.clear();
        if (!m_terminalSession)
        {
            m_terminalSession = new TerminalSession(this);
            connect(m_terminalSession, &TerminalSession::outputReady, this, &WebRtcCli::onTerminalOutputReady);
            connect(m_terminalSession, &TerminalSession::closed, this, &WebRtcCli::onTerminalClosed);
            connect(m_terminalSession, &TerminalSession::errorOccurred, this, &WebRtcCli::onTerminalError);
            connect(m_terminalSession, &TerminalSession::terminalInfoReady, this, &WebRtcCli::onTerminalInfoReady);
        }
        const int cols = JsonUtil::getInt(object, Constant::KEY_COLS, 80);
        const int rows = JsonUtil::getInt(object, Constant::KEY_ROWS, 24);
        if (!m_terminalAuditParser)
            m_terminalAuditParser = std::make_unique<TerminalCommandAuditParser>();
        m_terminalAuditParser->initialize(
            cols, rows, TerminalCommandAuditParser::EchoMode::Pty);
        m_terminalSession->start(cols, rows);
        m_terminalChannelPaused = false;
        m_terminalRemotePaused = false;
    }
    else if (msgType == Constant::TYPE_TERMINAL_INPUT)
    {
        if (!m_terminalSession)
            return;
        const QString encodedData = JsonUtil::getString(object, Constant::KEY_DATA);
        if (encodedData.size() > kMaxTerminalInputEncodedBytes)
        {
            LOG_WARN("Rejected oversized terminal input message: encodedSize={} bytes", encodedData.size());
            return;
        }
        const QByteArray data = QByteArray::fromBase64(encodedData.toLatin1());
        LOG_TRACE("Terminal input received from controller: size={}", data.size());
        if (m_terminalAuditParser)
        {
            const QVector<TerminalCommandAuditRecord> records =
                m_terminalAuditParser->noteInput(data);
            for (const TerminalCommandAuditRecord &record : records)
                recordTerminalAuditRecord(record);
        }
        m_terminalSession->writeInput(data);
    }
    else if (msgType == Constant::TYPE_TERMINAL_RESIZE)
    {
        if (!m_terminalSession)
            return;
        const int cols = JsonUtil::getInt(object, Constant::KEY_COLS, 80);
        const int rows = JsonUtil::getInt(object, Constant::KEY_ROWS, 24);
        if (m_terminalAuditParser)
            m_terminalAuditParser->resize(cols, rows);
        m_terminalSession->resize(cols, rows);
    }
    else if (msgType == Constant::TYPE_TERMINAL_STOP)
    {
        if (m_auditSession)
            m_auditSession->recordTerminalDisconnected();
        if (m_terminalSession)
        {
            m_terminalSession->stop();
            m_terminalSession->deleteLater();
            m_terminalSession = nullptr;
        }
        if (m_terminalBackpressureTimer)
            m_terminalBackpressureTimer->stop();
        m_pendingTerminalOutputChunks.clear();
        m_terminalChannelPaused = false;
        m_terminalRemotePaused = false;
        if (m_terminalAuditParser)
            m_terminalAuditParser->reset();
    }
    else if (msgType == Constant::TYPE_TERMINAL_FLOW_CONTROL)
    {
        m_terminalRemotePaused = !JsonUtil::getBool(object, Constant::KEY_ENABLED, true);
        if (m_terminalSession)
            m_terminalSession->setOutputPaused(m_terminalRemotePaused || m_terminalChannelPaused);
    }
}

void WebRtcCli::recordTerminalAuditRecord(const TerminalCommandAuditRecord &recordValue)
{
    if (!m_auditSession)
        return;

    TerminalCommandAuditRecord safeRecord = recordValue;
    if (safeRecord.kind == TerminalCommandAuditRecord::Command)
    {
        static const QRegularExpression sensitive(
            QStringLiteral("(?:password|passwd|passphrase|token|secret|api[_-]?key)"),
            QRegularExpression::CaseInsensitiveOption);
        if (safeRecord.command.trimmed().isEmpty())
            return;
        if (sensitive.match(safeRecord.command).hasMatch())
        {
            safeRecord.kind = TerminalCommandAuditRecord::Redacted;
            safeRecord.command.clear();
        }
    }
    m_auditSession->recordTerminalCommand(safeRecord);
}


void WebRtcCli::onTerminalOutputReady(const QByteArray &data)
{
    if (m_shutdownStarted.load())
        return;
    LOG_TRACE("Terminal output ready for controller: size={}", data.size());
    const QByteArray lower = data.toLower();
    if (m_terminalAuditParser)
    {
        if (lower.contains("password:") || lower.contains("passphrase:") || lower.contains("pin:"))
            m_terminalAuditParser->setSecretInput(true);
        const QVector<TerminalCommandAuditRecord> records =
            m_terminalAuditParser->processOutput(data);
        for (const TerminalCommandAuditRecord &record : records)
            recordTerminalAuditRecord(record);
    }
    if (!m_pendingTerminalOutputChunks.isEmpty() || !sendTerminalOutputChunk(data))
        m_pendingTerminalOutputChunks.enqueue(data);

    if (!m_pendingTerminalOutputChunks.isEmpty() ||
        (m_fileTextChannel && m_fileTextChannel->bufferedAmount() >= kTerminalChannelHighWatermark))
    {
        m_terminalChannelPaused = true;
        if (m_terminalSession)
            m_terminalSession->setOutputPaused(true);
        if (!m_terminalBackpressureTimer)
        {
            m_terminalBackpressureTimer = new QTimer(this);
            m_terminalBackpressureTimer->setInterval(10);
            connect(m_terminalBackpressureTimer, &QTimer::timeout,
                    this, &WebRtcCli::pollTerminalBackpressure);
        }
        m_terminalBackpressureTimer->start();
    }
}


bool WebRtcCli::sendTerminalOutputChunk(const QByteArray &data)
{
    QJsonObject response = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_TERMINAL_OUTPUT)
                               .add(Constant::KEY_ENCODING, QStringLiteral("base64"))
                               .add(Constant::KEY_DATA, QString::fromLatin1(data.toBase64()))
                               .build();
    return sendFileTextChannelMessage(response);
}


void WebRtcCli::pollTerminalBackpressure()
{
    if (!m_fileTextChannel || !m_fileTextChannel->isOpen())
    {
        m_terminalChannelPaused = true;
        if (m_terminalSession)
            m_terminalSession->setOutputPaused(true);
        return;
    }

    if (m_fileTextChannel->bufferedAmount() > kTerminalChannelLowWatermark)
        return;

    while (!m_pendingTerminalOutputChunks.isEmpty() &&
           m_fileTextChannel->bufferedAmount() < kTerminalChannelHighWatermark)
    {
        if (!sendTerminalOutputChunk(m_pendingTerminalOutputChunks.head()))
            return;
        m_pendingTerminalOutputChunks.dequeue();
    }

    if (!m_pendingTerminalOutputChunks.isEmpty() ||
        m_fileTextChannel->bufferedAmount() > kTerminalChannelLowWatermark)
        return;

    m_terminalChannelPaused = false;
    if (m_terminalBackpressureTimer)
        m_terminalBackpressureTimer->stop();
    if (m_terminalSession)
        m_terminalSession->setOutputPaused(m_terminalRemotePaused);
}


void WebRtcCli::onTerminalClosed(int exitCode)
{
    if (m_auditSession)
        m_auditSession->recordTerminalDisconnected();
    if (m_terminalAuditParser)
        m_terminalAuditParser->reset();
    QJsonObject response = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_TERMINAL_CLOSED)
                               .add(Constant::KEY_STATUS, exitCode)
                               .build();
    sendFileTextChannelMessage(response);
}


void WebRtcCli::onTerminalError(const QString &message)
{
    if (m_auditSession)
        m_auditSession->recordTerminalDisconnected();
    if (m_terminalAuditParser)
        m_terminalAuditParser->reset();
    QJsonObject response = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_TERMINAL_ERROR)
                               .add(Constant::KEY_ERROR, message)
                               .build();
    sendFileTextChannelMessage(response);
}


void WebRtcCli::onTerminalInfoReady(const QString &osName, const QString &shellPath, const QString &mode, bool pathTracking)
{
    if (m_terminalAuditParser)
    {
        m_terminalAuditParser->setEchoMode(
            mode == QStringLiteral("pipe")
                ? TerminalCommandAuditParser::EchoMode::PipeFallback
                : TerminalCommandAuditParser::EchoMode::Pty);
    }
    QJsonObject response = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_TERMINAL_INFO)
                               .add(Constant::KEY_OS, osName)
                               .add(Constant::KEY_SHELL, shellPath)
                               .add(Constant::KEY_TERMINAL_MODE, mode)
                               .add(Constant::KEY_PATH_TRACKING, pathTracking)
                               .add(Constant::KEY_PATH_TRACKING_READY, pathTracking)
                               .build();
    sendFileTextChannelMessage(response);
}


