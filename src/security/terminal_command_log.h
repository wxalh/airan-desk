#ifndef TERMINAL_COMMAND_LOG_H
#define TERMINAL_COMMAND_LOG_H

#include "security/terminal_command_audit_parser.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QString>

class TerminalCommandLog
{
public:
    struct Metadata
    {
        QString sessionId;
        QString peerId;
        QString sourceIp;
        QDateTime startedAt;
    };

    TerminalCommandLog() = default;
    ~TerminalCommandLog();

    bool open(const QString &auditRoot, const Metadata &metadata, QString *errorMessage);
    bool append(const TerminalCommandAuditRecord &record,
                const QDateTime &timestamp,
                QString *errorMessage);
    void close();
    QString relativePath() const;

    static bool cleanupExpiredSessionDirectories(const QString &auditRoot,
                                                 const QDate &cutoff,
                                                 QString *errorMessage);

private:
    static QString safeFileComponent(const QString &value);
    static QByteArray escapedField(const QString &value);
    static bool durableFlush(QFile *file);

    QFile m_file;
    QString m_relativePath;
};

#endif /* TERMINAL_COMMAND_LOG_H */
