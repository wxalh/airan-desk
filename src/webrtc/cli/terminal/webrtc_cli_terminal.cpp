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
constexpr qint64 kMaxPendingTerminalOutputBytes = 4LL * 1024 * 1024;
const QByteArray kTerminalOutputResync = QByteArrayLiteral("\x18\x1a\x1b[2J\x1b[H\x1b[0m");
} /* namespace */


void WebRtcCli::handleTerminalMessage(const QJsonObject &object)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
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
        const int cols = qBound(20, JsonUtil::getInt(object, Constant::KEY_COLS, 80), 500);
        const int rows = qBound(5, JsonUtil::getInt(object, Constant::KEY_ROWS, 24), 300);
        const QString requestedMode = JsonUtil::getString(object, Constant::KEY_TERMINAL_MODE);
        m_pendingTerminalEndType.clear();
        m_pendingTerminalEndError.clear();
        m_pendingTerminalEndRequestId.clear();
        m_pendingTerminalExitCode = 0;
        m_terminalStartRequestId = JsonUtil::getString(object, Constant::KEY_REQUEST_ID);
        const quint64 terminalGeneration = ++m_terminalSessionGeneration;
        LOG_INFO("Terminal start received: cols={}, rows={}, requestedMode={}, existingSession={}",
                 cols, rows, requestedMode, (m_terminalSession != nullptr));
        if (m_auditSession)
            m_auditSession->recordTerminalConnected();
        if (m_terminalBackpressureTimer)
            m_terminalBackpressureTimer->stop();
        m_pendingTerminalOutputChunks.clear();
        m_pendingTerminalOutputBytes = 0;
        m_terminalOutputNeedsReset = false;
        if (!m_terminalSession)
        {
            m_terminalSession = new TerminalSession(this);
        }
        disconnect(m_terminalSession, nullptr, this, nullptr);
        connect(m_terminalSession, &TerminalSession::outputReady, this,
                [this, terminalGeneration](const QByteArray &data) {
                    if (terminalGeneration != m_terminalSessionGeneration)
                    {
                        LOG_DEBUG("Ignoring terminal output from stale startup generation");
                        return;
                    }
                    onTerminalOutputReady(data);
                });
        connect(m_terminalSession, &TerminalSession::closed, this,
                [this, terminalGeneration](int exitCode) {
                    if (terminalGeneration != m_terminalSessionGeneration)
                    {
                        LOG_DEBUG("Ignoring terminal closed signal from stale startup generation");
                        return;
                    }
                    onTerminalClosed(exitCode);
                });
        connect(m_terminalSession, &TerminalSession::errorOccurred, this,
                [this, terminalGeneration](const QString &message) {
                    if (terminalGeneration != m_terminalSessionGeneration)
                    {
                        LOG_DEBUG("Ignoring terminal error from stale startup generation");
                        return;
                    }
                    onTerminalError(message);
                });
        connect(m_terminalSession, &TerminalSession::terminalInfoReady, this,
                [this, terminalGeneration](const QString &osName,
                                           const QString &shellPath,
                                           const QString &mode,
                                           bool pathTracking) {
                    if (terminalGeneration != m_terminalSessionGeneration)
                    {
                        LOG_DEBUG("Ignoring terminal info from stale startup generation");
                        return;
                    }
                    onTerminalInfoReady(osName, shellPath, mode, pathTracking);
                });
        if (!m_terminalAuditParser)
            m_terminalAuditParser = std::make_unique<TerminalCommandAuditParser>();
        m_terminalAuditParser->initialize(
            cols, rows, TerminalCommandAuditParser::EchoMode::Pty);
        m_terminalSession->start(cols, rows, requestedMode);
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
        const int cols = qBound(20, JsonUtil::getInt(object, Constant::KEY_COLS, 80), 500);
        const int rows = qBound(5, JsonUtil::getInt(object, Constant::KEY_ROWS, 24), 300);
        if (m_terminalAuditParser)
            m_terminalAuditParser->resize(cols, rows);
        m_terminalSession->resize(cols, rows);
    }
    else if (msgType == Constant::TYPE_TERMINAL_STOP)
    {
        LOG_INFO("Terminal stop received: activeSession={}", (m_terminalSession != nullptr));
        ++m_terminalSessionGeneration;
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
        m_pendingTerminalOutputBytes = 0;
        m_terminalOutputNeedsReset = false;
        m_terminalChannelPaused = false;
        m_terminalRemotePaused = false;
        m_pendingTerminalEndType.clear();
        m_pendingTerminalEndError.clear();
        m_pendingTerminalEndRequestId.clear();
        m_pendingTerminalExitCode = 0;
        if (m_terminalAuditParser)
            m_terminalAuditParser->reset();
        // Keep the old id until the next start replaces it. A queued
        // closed/error signal from the stopped process must remain stale
        // instead of becoming an id-less event accepted by the controller.
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
    if (sender() && sender() != m_terminalSession)
    {
        LOG_DEBUG("Ignoring terminal output from stale session");
        return;
    }
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
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
    bool outputOverflowed = false;
    if (!m_pendingTerminalOutputChunks.isEmpty() || !sendTerminalOutputChunk(data))
    {
        if (m_pendingTerminalOutputBytes + data.size() > kMaxPendingTerminalOutputBytes)
        {
            LOG_WARN("Dropping queued terminal output after reaching the pending limit: pendingBytes={}, incomingBytes={}",
                     m_pendingTerminalOutputBytes,
                     data.size());
            m_pendingTerminalOutputChunks.clear();
            m_pendingTerminalOutputBytes = 0;
            m_terminalOutputNeedsReset = true;
            outputOverflowed = true;
        }
        else
        {
            m_pendingTerminalOutputChunks.enqueue(data);
            m_pendingTerminalOutputBytes += data.size();
        }
    }

    if (outputOverflowed || m_terminalChannelPaused || !m_pendingTerminalOutputChunks.isEmpty() ||
        (m_fileTextChannel && m_fileTextChannel->bufferedAmount() >= kTerminalChannelHighWatermark))
    {
        scheduleTerminalBackpressurePoll();
    }
}


bool WebRtcCli::sendTerminalOutputChunk(const QByteArray &data)
{
    const bool needsReset = m_terminalOutputNeedsReset;
    const QByteArray payload = needsReset ? kTerminalOutputResync + data : data;
    QJsonObject response = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_TERMINAL_OUTPUT)
                               .add(Constant::KEY_ENCODING, QStringLiteral("base64"))
                               .add(Constant::KEY_REQUEST_ID, m_terminalStartRequestId)
                               .add(Constant::KEY_DATA, QString::fromLatin1(payload.toBase64()))
                               .build();
    const bool sent = sendFileTextChannelMessage(response);
    if (sent && needsReset)
        m_terminalOutputNeedsReset = false;
    return sent;
}


void WebRtcCli::scheduleTerminalBackpressurePoll()
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


void WebRtcCli::pollTerminalBackpressure()
{
    if (!m_fileTextChannel || !m_fileTextChannel->isOpen())
    {
        m_terminalChannelPaused = true;
        if (m_terminalSession)
            m_terminalSession->setOutputPaused(true);
        if (m_terminalBackpressureTimer)
            m_terminalBackpressureTimer->stop();
        return;
    }

    if (m_fileTextChannel->bufferedAmount() > kTerminalChannelLowWatermark)
        return;

    while (!m_pendingTerminalOutputChunks.isEmpty() &&
           m_fileTextChannel->bufferedAmount() < kTerminalChannelHighWatermark)
    {
        if (!sendTerminalOutputChunk(m_pendingTerminalOutputChunks.head()))
            return;
        m_pendingTerminalOutputBytes -= m_pendingTerminalOutputChunks.head().size();
        m_pendingTerminalOutputChunks.dequeue();
    }

    if (m_pendingTerminalOutputChunks.isEmpty())
        sendPendingTerminalEnd();

    if (!m_pendingTerminalOutputChunks.isEmpty() ||
        m_fileTextChannel->bufferedAmount() > kTerminalChannelLowWatermark)
        return;

    m_terminalChannelPaused = false;
    if (m_terminalBackpressureTimer)
        m_terminalBackpressureTimer->stop();
    if (m_terminalSession)
        m_terminalSession->setOutputPaused(m_terminalRemotePaused);
}


void WebRtcCli::queueTerminalEnd(const QString &type, int exitCode, const QString &error)
{
    m_pendingTerminalEndType = type;
    m_pendingTerminalEndError = error;
    m_pendingTerminalEndRequestId = m_terminalStartRequestId;
    m_pendingTerminalExitCode = exitCode;

    if (m_terminalOutputNeedsReset && m_pendingTerminalOutputChunks.isEmpty())
        m_pendingTerminalOutputChunks.enqueue(QByteArray());
    if (m_pendingTerminalOutputChunks.isEmpty())
    {
        sendPendingTerminalEnd();
        return;
    }

    scheduleTerminalBackpressurePoll();
    pollTerminalBackpressure();
}


void WebRtcCli::sendPendingTerminalEnd()
{
    if (m_pendingTerminalEndType.isEmpty() || !m_pendingTerminalOutputChunks.isEmpty())
        return;

    const QString type = m_pendingTerminalEndType;
    const QString error = m_pendingTerminalEndError;
    const QString requestId = m_pendingTerminalEndRequestId;
    const int exitCode = m_pendingTerminalExitCode;
    m_pendingTerminalEndType.clear();
    m_pendingTerminalEndError.clear();
    m_pendingTerminalEndRequestId.clear();
    m_pendingTerminalExitCode = 0;

    QJsonObject response = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_MSGTYPE, type)
                               .add(Constant::KEY_REQUEST_ID, requestId)
                               .build();
    if (type == Constant::TYPE_TERMINAL_CLOSED)
        response.insert(Constant::KEY_STATUS, exitCode);
    else
        response.insert(Constant::KEY_ERROR, error);
    sendFileTextChannelMessage(response);
}


void WebRtcCli::onTerminalClosed(int exitCode)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    if (sender() && sender() != m_terminalSession)
    {
        LOG_DEBUG("Ignoring terminal closed signal from stale session");
        return;
    }
    if (m_terminalSession)
    {
        ++m_terminalSessionGeneration;
        m_terminalSession->deleteLater();
        m_terminalSession = nullptr;
    }
    if (m_auditSession)
        m_auditSession->recordTerminalDisconnected();
    m_terminalRemotePaused = false;
    if (m_terminalAuditParser)
        m_terminalAuditParser->reset();
    queueTerminalEnd(Constant::TYPE_TERMINAL_CLOSED, exitCode, QString());
}


void WebRtcCli::onTerminalError(const QString &message)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    if (sender() && sender() != m_terminalSession)
    {
        LOG_DEBUG("Ignoring terminal error from stale session");
        return;
    }
    if (m_terminalSession)
    {
        ++m_terminalSessionGeneration;
        m_terminalSession->stop();
        m_terminalSession->deleteLater();
        m_terminalSession = nullptr;
    }
    if (m_auditSession)
        m_auditSession->recordTerminalDisconnected();
    m_terminalRemotePaused = false;
    if (m_terminalAuditParser)
        m_terminalAuditParser->reset();
    queueTerminalEnd(Constant::TYPE_TERMINAL_ERROR, 0, message);
}


void WebRtcCli::onTerminalInfoReady(const QString &osName, const QString &shellPath, const QString &mode, bool pathTracking)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    if (sender() && sender() != m_terminalSession)
    {
        LOG_DEBUG("Ignoring terminal info from stale session");
        return;
    }
    if (m_terminalAuditParser)
    {
        m_terminalAuditParser->setEchoMode(
            mode == QStringLiteral("pipe")
                ? TerminalCommandAuditParser::EchoMode::PipeFallback
                : TerminalCommandAuditParser::EchoMode::Pty);
    }
    // Unix bash PTY sessions install the OSC 7 hook in their rcfile. All
    // other modes still require the controller-side prompt injection.
    const bool pathTrackingReady = pathTracking &&
                                   mode != QStringLiteral("pipe") &&
                                   osName != QStringLiteral("windows");
    QJsonObject response = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_MSGTYPE, Constant::TYPE_TERMINAL_INFO)
                               .add(Constant::KEY_OS, osName)
                               .add(Constant::KEY_SHELL, shellPath)
                               .add(Constant::KEY_TERMINAL_MODE, mode)
                               .add(Constant::KEY_PATH_TRACKING, pathTracking)
                               // The terminal advertises capability here. The
                               // controller still has to install its prompt
                               // hook before path tracking is ready.
                               .add(Constant::KEY_PATH_TRACKING_READY, pathTrackingReady)
                               .add(Constant::KEY_REQUEST_ID, m_terminalStartRequestId)
                               .build();
    sendFileTextChannelMessage(response);
}


