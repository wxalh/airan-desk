#include "audit_session.h"

#include "security/audit_logger.h"
#include "security/terminal_command_audit_parser.h"
#include "security/terminal_command_log.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>

AuditSession::AuditSession(QString sessionId, QString peerId, QString sourceIp, QString mode,
                           AuditLogger *auditLogger)
    : m_sessionId(std::move(sessionId)),
      m_peerId(std::move(peerId)),
      m_sourceIp(std::move(sourceIp)),
      m_mode(std::move(mode)),
      m_auditLogger(auditLogger ? auditLogger : &AuditLogger::instance()),
      m_startedMs(QDateTime::currentMSecsSinceEpoch())
{
}

AuditSession::~AuditSession()
{
    finish(QStringLiteral("shutdown"));
}

QString AuditSession::sessionId() const { return m_sessionId; }
QString AuditSession::peerId() const { return m_peerId; }
QString AuditSession::sourceIp() const { return m_sourceIp; }
QString AuditSession::mode() const { return m_mode; }

QJsonObject AuditSession::baseFields() const
{
    QJsonObject fields;
    fields.insert(QStringLiteral("session_id"), m_sessionId);
    fields.insert(QStringLiteral("peer_id"), m_peerId);
    fields.insert(QStringLiteral("source_ip"), m_sourceIp);
    fields.insert(QStringLiteral("mode"), m_mode);
    return fields;
}

bool AuditSession::record(const QString &event, const QJsonObject &eventFields)
{
    QJsonObject fields = baseFields();
    for (auto it = eventFields.constBegin(); it != eventFields.constEnd(); ++it)
        fields.insert(it.key(), it.value());
    return m_auditLogger->append(event, fields);
}

void AuditSession::recordConnected(int width, int height)
{
    QJsonObject fields;
    fields.insert(QStringLiteral("width"), width);
    fields.insert(QStringLiteral("height"), height);
    record(m_mode == QStringLiteral("file") ? QStringLiteral("file_manager_connected")
                                             : QStringLiteral("desktop_connected"),
           fields);
}

void AuditSession::recordFileOperation(const QString &operation, const QString &path,
                                       bool success, const QString &detail)
{
    QJsonObject fields;
    fields.insert(QStringLiteral("operation"), operation);
    fields.insert(QStringLiteral("file_name"), QFileInfo(path).fileName());
    fields.insert(QStringLiteral("success"), success);
    if (!detail.isEmpty())
        fields.insert(QStringLiteral("detail"), detail);
    record(QStringLiteral("file_operation"), fields);
}

void AuditSession::recordFileTransfer(const QString &path, qint64 size, const QString &direction,
                                      const QString &sha256, bool success)
{
    {
        QMutexLocker locker(&m_mutex);
        if (success)
        {
            ++m_fileCount;
            m_totalBytes += qMax<qint64>(0, size);
        }
    }
    QJsonObject fields;
    fields.insert(QStringLiteral("file_name"), QFileInfo(path).fileName());
    fields.insert(QStringLiteral("size"), static_cast<double>(size));
    fields.insert(QStringLiteral("direction"), direction);
    fields.insert(QStringLiteral("sha256"), sha256);
    fields.insert(QStringLiteral("success"), success);
    record(QStringLiteral("file_transfer"), fields);
}

void AuditSession::recordTerminalConnected()
{
    QMutexLocker locker(&m_mutex);
    if (m_terminalOpen)
        return;
    m_terminalOpen = true;
    locker.unlock();

    TerminalCommandLog::Metadata metadata;
    metadata.sessionId = m_sessionId;
    metadata.peerId = m_peerId;
    metadata.sourceIp = m_sourceIp;
    metadata.startedAt = QDateTime::fromMSecsSinceEpoch(m_startedMs);
    QString relativePath;
    if (!m_auditLogger->openTerminalCommandLog(metadata, &relativePath))
    {
        QMutexLocker failedLocker(&m_mutex);
        m_terminalOpen = false;
        return;
    }
    {
        QMutexLocker pathLocker(&m_mutex);
        m_commandLogPath = relativePath;
    }
    record(QStringLiteral("terminal_connected"));
    record(QStringLiteral("terminal_command_log_started"),
           QJsonObject{{QStringLiteral("command_log_path"), relativePath}});
}

void AuditSession::recordTerminalCommand(const TerminalCommandAuditRecord &recordValue)
{
    QMutexLocker locker(&m_mutex);
    if (!m_terminalOpen || m_finished)
        return;
    if (!m_auditLogger->appendTerminalCommand(m_sessionId, recordValue))
        return;

    if (recordValue.kind == TerminalCommandAuditRecord::Command)
        ++m_terminalCommandCount;
    else if (recordValue.kind == TerminalCommandAuditRecord::Redacted)
        ++m_terminalRedactedCount;
    else
        ++m_terminalUnavailableCount;
}

void AuditSession::recordTerminalDisconnected()
{
    QMutexLocker locker(&m_mutex);
    if (!m_terminalOpen)
        return;
    m_terminalOpen = false;
    locker.unlock();
    m_auditLogger->closeTerminalCommandLog(m_sessionId);
    record(QStringLiteral("terminal_disconnected"));
}

void AuditSession::finish(const QString &reason)
{
    qint64 totalBytes = 0;
    int fileCount = 0;
    int terminalCommandCount = 0;
    int terminalRedactedCount = 0;
    int terminalUnavailableCount = 0;
    QString commandLogPath;
    {
        QMutexLocker locker(&m_mutex);
        if (m_finished)
            return;
        m_finished = true;
    }
    recordTerminalDisconnected();
    {
        QMutexLocker locker(&m_mutex);
        totalBytes = m_totalBytes;
        fileCount = m_fileCount;
        terminalCommandCount = m_terminalCommandCount;
        terminalRedactedCount = m_terminalRedactedCount;
        terminalUnavailableCount = m_terminalUnavailableCount;
        commandLogPath = m_commandLogPath;
    }
    const qint64 durationMs = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - m_startedMs);
    record(QStringLiteral("connection_disconnected"), QJsonObject{{QStringLiteral("reason"), reason},
                                                                  {QStringLiteral("duration_ms"), static_cast<double>(durationMs)}});
    record(QStringLiteral("session_summary"), QJsonObject{{QStringLiteral("duration_ms"), static_cast<double>(durationMs)},
                                                           {QStringLiteral("file_count"), fileCount},
                                                           {QStringLiteral("total_bytes"), static_cast<double>(totalBytes)},
                                                           {QStringLiteral("terminal_command_count"), terminalCommandCount},
                                                           {QStringLiteral("terminal_redacted_count"), terminalRedactedCount},
                                                           {QStringLiteral("terminal_unavailable_count"), terminalUnavailableCount},
                                                           {QStringLiteral("command_log_path"), commandLogPath}});
}

QString AuditSession::sha256ForFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex());
}
