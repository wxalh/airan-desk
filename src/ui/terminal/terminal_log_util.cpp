#include "terminal_log_util.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

namespace TerminalLogUtil
{
QString defaultTerminalLogPath(const QString &remoteId, const QString &instanceId)
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (baseDir.isEmpty())
        baseDir = QDir::homePath();

    QString safeRemoteId = remoteId;
    const QChar replacement = QLatin1Char('_');
    safeRemoteId.replace(QLatin1Char('/'), replacement);
    safeRemoteId.replace(QLatin1Char('\\'), replacement);
    safeRemoteId.replace(QLatin1Char(':'), replacement);
    safeRemoteId.replace(QLatin1Char('*'), replacement);
    safeRemoteId.replace(QLatin1Char('?'), replacement);
    safeRemoteId.replace(QLatin1Char('"'), replacement);
    safeRemoteId.replace(QLatin1Char('<'), replacement);
    safeRemoteId.replace(QLatin1Char('>'), replacement);
    safeRemoteId.replace(QLatin1Char('|'), replacement);
    if (safeRemoteId.isEmpty())
        safeRemoteId = QStringLiteral("remote");

    QString safeInstanceId = instanceId;
    safeInstanceId.replace(QLatin1Char('/'), replacement);
    safeInstanceId.replace(QLatin1Char('\\'), replacement);
    safeInstanceId.replace(QLatin1Char(':'), replacement);
    safeInstanceId.replace(QLatin1Char('*'), replacement);
    safeInstanceId.replace(QLatin1Char('?'), replacement);
    safeInstanceId.replace(QLatin1Char('"'), replacement);
    safeInstanceId.replace(QLatin1Char('<'), replacement);
    safeInstanceId.replace(QLatin1Char('>'), replacement);
    safeInstanceId.replace(QLatin1Char('|'), replacement);

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString fileName = safeInstanceId.isEmpty()
                                 ? QStringLiteral("terminal_%1_%2.txt").arg(safeRemoteId, timestamp)
                                 : QStringLiteral("terminal_%1_%2_%3.txt").arg(safeRemoteId, safeInstanceId, timestamp);
    return QDir(baseDir).filePath(QStringLiteral("AiranDesk/terminal_logs/") + fileName);
}
} /* namespace TerminalLogUtil */
