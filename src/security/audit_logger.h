#ifndef AUDIT_LOGGER_H
#define AUDIT_LOGGER_H

#include <QJsonObject>
#include <QMutex>
#include <QString>

#include <functional>
#include <map>
#include <memory>

#include "security/terminal_command_audit_parser.h"
#include "security/terminal_command_log.h"

class AuditLogger
{
public:
    using FailureHandler = std::function<void(const QString &)>;

    explicit AuditLogger(QString directoryOverride = QString());
    static AuditLogger &instance();
    bool initialize(QString *errorMessage = nullptr);
    bool append(const QString &event, const QJsonObject &fields = QJsonObject());
    bool isReady() const;
    QString directoryPath() const;
    void setFailureHandler(FailureHandler handler);
    bool openTerminalCommandLog(const TerminalCommandLog::Metadata &metadata,
                                QString *relativePath);
    bool appendTerminalCommand(const QString &sessionId,
                               const TerminalCommandAuditRecord &record);
    void closeTerminalCommandLog(const QString &sessionId);

private:
    bool cleanupExpiredFilesLocked(QString *errorMessage);
    bool appendLocked(const QString &event, const QJsonObject &fields, QString *errorMessage);
    void markUnavailable(const QString &reason);

    mutable QMutex m_mutex;
    bool m_ready{false};
    QString m_currentDate;
    FailureHandler m_failureHandler;
    QString m_directoryOverride;
    std::map<QString, std::unique_ptr<TerminalCommandLog>> m_terminalCommandLogs;
};

#endif /* AUDIT_LOGGER_H */
