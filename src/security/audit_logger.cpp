#include "audit_logger.h"

#include <QDate>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QRegularExpression>

#include <cstdio>

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
bool durableFlush(QFile &file)
{
    if (!file.flush())
        return false;
    const int handle = file.handle();
    if (handle < 0)
        return false;
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    const intptr_t nativeHandle = _get_osfhandle(handle);
    return nativeHandle != -1 && FlushFileBuffers(reinterpret_cast<HANDLE>(nativeHandle));
#else
    return ::fsync(handle) == 0;
#endif
}
}

AuditLogger &AuditLogger::instance()
{
    static AuditLogger logger;
    return logger;
}

AuditLogger::AuditLogger(QString directoryOverride)
    : m_directoryOverride(std::move(directoryOverride))
{
}

QString AuditLogger::directoryPath() const
{
    if (!m_directoryOverride.isEmpty())
        return m_directoryOverride;
    return QDir(QDir::homePath()).filePath(QStringLiteral(".wxalh/airan-desk/audit"));
}

bool AuditLogger::initialize(QString *errorMessage)
{
    QMutexLocker locker(&m_mutex);
    if (m_ready)
        return true;

    const QString directory = directoryPath();
    if (!QDir().mkpath(directory))
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("AuditLogger", "Failed to create the fixed audit directory: %1").arg(directory);
        return false;
    }
    if (!cleanupExpiredFilesLocked(errorMessage))
        return false;

    m_ready = true;
    m_currentDate.clear();
    QString writeError;
    if (!appendLocked(QStringLiteral("audit_started"), QJsonObject(), &writeError))
    {
        m_ready = false;
        if (errorMessage)
            *errorMessage = writeError;
        return false;
    }
    return true;
}

bool AuditLogger::cleanupExpiredFilesLocked(QString *errorMessage)
{
    QDir directory(directoryPath());
    const QDate cutoff = QDate::currentDate().addMonths(-6);
    if (!TerminalCommandLog::cleanupExpiredSessionDirectories(
            directoryPath(), cutoff, errorMessage))
    {
        return false;
    }
    const QRegularExpression pattern(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2})\\.jsonl$"));
    const QFileInfoList files = directory.entryInfoList(QStringList{QStringLiteral("*.jsonl")}, QDir::Files);
    for (const QFileInfo &info : files)
    {
        const QRegularExpressionMatch match = pattern.match(info.fileName());
        if (!match.hasMatch())
            continue;
        const QDate date = QDate::fromString(match.captured(1), QStringLiteral("yyyy-MM-dd"));
        if (!date.isValid() || date >= cutoff)
            continue;
        if (!QFile::remove(info.absoluteFilePath()))
        {
            if (errorMessage)
                *errorMessage = QCoreApplication::translate("AuditLogger", "Failed to remove expired audit file: %1").arg(info.absoluteFilePath());
            return false;
        }
    }
    return true;
}

bool AuditLogger::append(const QString &event, const QJsonObject &fields)
{
    FailureHandler handler;
    QString error;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_ready)
            return false;
        if (appendLocked(event, fields, &error))
            return true;
        m_ready = false;
        handler = m_failureHandler;
    }
    std::fprintf(stderr, "Audit log became unavailable: %s\n", error.toLocal8Bit().constData());
    if (handler)
        handler(error);
    return false;
}

bool AuditLogger::appendLocked(const QString &event, const QJsonObject &fields, QString *errorMessage)
{
    const QString date = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    if (date != m_currentDate)
    {
        if (!cleanupExpiredFilesLocked(errorMessage))
            return false;
        m_currentDate = date;
    }

    QJsonObject object = fields;
    object.insert(QStringLiteral("schema_version"), 1);
    object.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("event"), event);
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    const QString path = QDir(directoryPath()).filePath(date + QStringLiteral(".jsonl"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("AuditLogger", "Failed to open audit file: %1").arg(file.errorString());
        return false;
    }
    if (file.write(line) != line.size() || !durableFlush(file))
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("AuditLogger", "Failed to durably append audit file: %1").arg(file.errorString());
        return false;
    }
    return true;
}

bool AuditLogger::isReady() const
{
    QMutexLocker locker(&m_mutex);
    return m_ready;
}

void AuditLogger::setFailureHandler(FailureHandler handler)
{
    QMutexLocker locker(&m_mutex);
    m_failureHandler = std::move(handler);
}

bool AuditLogger::openTerminalCommandLog(const TerminalCommandLog::Metadata &metadata,
                                         QString *relativePath)
{
    FailureHandler handler;
    QString error;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_ready)
            return false;

        m_terminalCommandLogs.erase(metadata.sessionId);
        auto log = std::make_unique<TerminalCommandLog>();
        if (log->open(directoryPath(), metadata, &error))
        {
            if (relativePath)
                *relativePath = log->relativePath();
            m_terminalCommandLogs[metadata.sessionId] = std::move(log);
            return true;
        }
        m_ready = false;
        handler = m_failureHandler;
    }

    std::fprintf(stderr, "Audit log became unavailable: %s\n",
                 error.toLocal8Bit().constData());
    if (handler)
        handler(error);
    return false;
}

bool AuditLogger::appendTerminalCommand(const QString &sessionId,
                                        const TerminalCommandAuditRecord &record)
{
    FailureHandler handler;
    QString error;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_ready)
            return false;
        const auto it = m_terminalCommandLogs.find(sessionId);
        if (it != m_terminalCommandLogs.end() &&
            it->second->append(record, QDateTime::currentDateTime(), &error))
        {
            return true;
        }
        if (error.isEmpty())
        {
            error = QCoreApplication::translate(
                "AuditLogger", "Terminal command audit session is not open.");
        }
        m_ready = false;
        handler = m_failureHandler;
    }

    std::fprintf(stderr, "Audit log became unavailable: %s\n",
                 error.toLocal8Bit().constData());
    if (handler)
        handler(error);
    return false;
}

void AuditLogger::closeTerminalCommandLog(const QString &sessionId)
{
    QMutexLocker locker(&m_mutex);
    m_terminalCommandLogs.erase(sessionId);
}
