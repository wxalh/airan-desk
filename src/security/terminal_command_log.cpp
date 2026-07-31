#include "terminal_command_log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

TerminalCommandLog::~TerminalCommandLog()
{
    close();
}

bool TerminalCommandLog::open(const QString &auditRoot,
                              const Metadata &metadata,
                              QString *errorMessage)
{
    close();
    const QDateTime startedAt = metadata.startedAt.isValid()
                                        ? metadata.startedAt
                                        : QDateTime::currentDateTime();
    const QString date = startedAt.date().toString(QStringLiteral("yyyy-MM-dd"));
    const QString relativeDirectory = QStringLiteral("sessions/") + date;
    QDir root(auditRoot);
    if (!root.mkpath(relativeDirectory))
    {
        if (errorMessage)
        {
            *errorMessage = QCoreApplication::translate(
                                "AuditLogger",
                                "Failed to create terminal command audit directory: %1")
                                .arg(root.filePath(relativeDirectory));
        }
        return false;
    }

    const QString timestamp = startedAt.toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString baseName = timestamp + QLatin1Char('_') + safeFileComponent(metadata.sessionId);
    QString fileName = baseName + QStringLiteral(".commands.tsv");
    int suffix = 1;
    while (QFileInfo::exists(root.filePath(relativeDirectory + QLatin1Char('/') + fileName)))
    {
        fileName = baseName + QLatin1Char('_') + QString::number(suffix++) +
                   QStringLiteral(".commands.tsv");
    }

    m_relativePath = relativeDirectory + QLatin1Char('/') + fileName;
    m_file.setFileName(root.filePath(m_relativePath));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (errorMessage)
        {
            *errorMessage = QCoreApplication::translate(
                                "AuditLogger",
                                "Failed to open terminal command audit file: %1")
                                .arg(m_file.errorString());
        }
        m_relativePath.clear();
        return false;
    }

    QByteArray header;
    header.append("# Airan Desk Terminal Command Audit\n");
    header.append("# session_id: ").append(escapedField(metadata.sessionId)).append('\n');
    header.append("# peer_id: ").append(escapedField(metadata.peerId)).append('\n');
    header.append("# source_ip: ").append(escapedField(metadata.sourceIp)).append('\n');
    header.append("# started_at: ")
        .append(startedAt.toString(Qt::ISODateWithMs).toUtf8())
        .append('\n');
    header.append("time\tcommand\n");
    if (m_file.write(header) != header.size() || !durableFlush(&m_file))
    {
        if (errorMessage)
        {
            *errorMessage = QCoreApplication::translate(
                                "AuditLogger",
                                "Failed to durably append terminal command audit file: %1")
                                .arg(m_file.errorString());
        }
        close();
        return false;
    }
    return true;
}

bool TerminalCommandLog::append(const TerminalCommandAuditRecord &record,
                                const QDateTime &timestamp,
                                QString *errorMessage)
{
    if (!m_file.isOpen())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("AuditLogger", "Terminal command audit file is not open.");
        return false;
    }

    QString command;
    if (record.kind == TerminalCommandAuditRecord::Redacted)
        command = QStringLiteral("[REDACTED]");
    else if (record.kind == TerminalCommandAuditRecord::Unavailable)
        command = QStringLiteral("[UNAVAILABLE]");
    else
        command = record.command;

    const QDateTime effectiveTimestamp = timestamp.isValid()
                                                   ? timestamp
                                                   : QDateTime::currentDateTime();
    QByteArray line = effectiveTimestamp.toString(Qt::ISODateWithMs).toUtf8();
    line.append('\t').append(escapedField(command)).append('\n');
    if (m_file.write(line) == line.size() && durableFlush(&m_file))
        return true;

    if (errorMessage)
    {
        *errorMessage = QCoreApplication::translate(
                            "AuditLogger",
                            "Failed to durably append terminal command audit file: %1")
                            .arg(m_file.errorString());
    }
    return false;
}

void TerminalCommandLog::close()
{
    if (m_file.isOpen())
        m_file.close();
}

QString TerminalCommandLog::relativePath() const
{
    return m_relativePath;
}

bool TerminalCommandLog::cleanupExpiredSessionDirectories(const QString &auditRoot,
                                                          const QDate &cutoff,
                                                          QString *errorMessage)
{
    QDir sessions(QDir(auditRoot).filePath(QStringLiteral("sessions")));
    if (!sessions.exists())
        return true;

    static const QRegularExpression pattern(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
    const QFileInfoList directories = sessions.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : directories)
    {
        if (!pattern.match(info.fileName()).hasMatch())
            continue;
        const QDate date = QDate::fromString(info.fileName(), QStringLiteral("yyyy-MM-dd"));
        if (!date.isValid() || date >= cutoff)
            continue;
        QDir expired(info.absoluteFilePath());
        if (expired.removeRecursively())
            continue;
        if (errorMessage)
        {
            *errorMessage = QCoreApplication::translate(
                                "AuditLogger",
                                "Failed to remove expired terminal command audit directory: %1")
                                .arg(info.absoluteFilePath());
        }
        return false;
    }
    return true;
}

QString TerminalCommandLog::safeFileComponent(const QString &value)
{
    QString safe;
    safe.reserve(qMin(80, value.size()));
    for (const QChar ch : value)
    {
        const ushort code = ch.unicode();
        const bool allowed = (code >= 'a' && code <= 'z') ||
                             (code >= 'A' && code <= 'Z') ||
                             (code >= '0' && code <= '9') ||
                             ch == QLatin1Char('.') || ch == QLatin1Char('-') ||
                             ch == QLatin1Char('_');
        safe.append(allowed ? ch : QLatin1Char('_'));
        if (safe.size() >= 80)
            break;
    }
    while (safe.contains(QStringLiteral("..")))
        safe.replace(QStringLiteral(".."), QStringLiteral("__"));
    if (safe.isEmpty() || safe == QStringLiteral(".") || safe == QStringLiteral(".."))
        safe = QStringLiteral("session");
    return safe;
}

QByteArray TerminalCommandLog::escapedField(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    escaped.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    escaped.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    escaped.replace(QChar(0), QStringLiteral("\\0"));
    return escaped.toUtf8();
}

bool TerminalCommandLog::durableFlush(QFile *file)
{
    if (!file || !file->flush())
        return false;
    const int handle = file->handle();
    if (handle < 0)
        return false;
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    const intptr_t nativeHandle = _get_osfhandle(handle);
    return nativeHandle != -1 && FlushFileBuffers(reinterpret_cast<HANDLE>(nativeHandle));
#else
    return ::fsync(handle) == 0;
#endif
}
