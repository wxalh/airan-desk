#ifndef AUDIT_SESSION_H
#define AUDIT_SESSION_H

#include <QJsonObject>
#include <QMutex>
#include <QString>

class AuditLogger;
struct TerminalCommandAuditRecord;

class AuditSession
{
public:
    AuditSession(QString sessionId, QString peerId, QString sourceIp, QString mode,
                 AuditLogger *auditLogger = nullptr);
    ~AuditSession();

    QString sessionId() const;
    QString peerId() const;
    QString sourceIp() const;
    QString mode() const;
    bool record(const QString &event, const QJsonObject &fields = QJsonObject());
    void recordConnected(int width, int height);
    void recordFileOperation(const QString &operation, const QString &path, bool success,
                             const QString &detail = QString());
    void recordFileTransfer(const QString &path, qint64 size, const QString &direction,
                            const QString &sha256, bool success);
    void recordTerminalConnected();
    void recordTerminalCommand(const TerminalCommandAuditRecord &record);
    void recordTerminalDisconnected();
    void finish(const QString &reason);
    static QString sha256ForFile(const QString &path);

private:
    QJsonObject baseFields() const;

    mutable QMutex m_mutex;
    QString m_sessionId;
    QString m_peerId;
    QString m_sourceIp;
    QString m_mode;
    AuditLogger *m_auditLogger{nullptr};
    QString m_commandLogPath;
    qint64 m_startedMs{0};
    qint64 m_totalBytes{0};
    int m_fileCount{0};
    int m_terminalCommandCount{0};
    int m_terminalRedactedCount{0};
    int m_terminalUnavailableCount{0};
    bool m_finished{false};
    bool m_terminalOpen{false};
};

#endif /* AUDIT_SESSION_H */
