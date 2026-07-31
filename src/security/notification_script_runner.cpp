#include "notification_script_runner.h"

#include "common/logger_manager.h"
#include "security/audit_logger.h"
#include "util/config/config_util.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

#include <memory>

namespace
{
constexpr int kMaximumCapturedOutput = 64 * 1024;

struct InvocationState
{
    bool completed{false};
    QByteArray captured;
};

QString quoteBatchArgument(QString value)
{
    value.replace(QStringLiteral("%"), QStringLiteral("%%"));
    value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}

bool validEvent(const QString &event)
{
    static const QStringList allowed{
        QStringLiteral("connection_requested"),
        QStringLiteral("authentication_failed"),
        QStringLiteral("connection_established"),
        QStringLiteral("connection_disconnected"),
        QStringLiteral("test")};
    return allowed.contains(event);
}

bool validPeerId(const QString &peerId)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_.:-]{1,128}$"));
    return pattern.match(peerId).hasMatch();
}

bool validDetail(const QString &detail)
{
    return detail.size() <= 512 && !detail.contains(QChar::Null) &&
           !detail.contains(QLatin1Char('\r')) && !detail.contains(QLatin1Char('\n'));
}

void auditScriptFailure(const QString &event, const QString &peerId, const QString &message)
{
    if (!AuditLogger::instance().isReady())
        return;
    AuditLogger::instance().append(QStringLiteral("notification_script_failed"),
                                   QJsonObject{{QStringLiteral("notification_event"), event},
                                               {QStringLiteral("peer_id"), peerId},
                                               {QStringLiteral("detail"), message}});
}
}

QString NotificationScriptRunner::resolvedScriptPath()
{
    QString path = ConfigUtil->notify_script.trimmed();
    path.replace(QStringLiteral("{app_dir}"), QCoreApplication::applicationDirPath());
    return path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}

bool NotificationScriptRunner::isReady()
{
    const QFileInfo script(resolvedScriptPath());
    if (!script.exists() || !script.isFile())
        return false;
#if defined(Q_OS_UNIX)
    return script.isExecutable();
#else
    return true;
#endif
}

void NotificationScriptRunner::runAsync(QObject *context,
                                         const QString &event,
                                         const QString &peerId,
                                         const QString &detail,
                                         Callback callback,
                                         int timeoutMs,
                                         const QString &scriptPathOverride)
{
    NotificationScriptResult early;
    QString scriptPath = scriptPathOverride.trimmed();
    scriptPath.replace(QStringLiteral("{app_dir}"), QCoreApplication::applicationDirPath());
    scriptPath = scriptPath.isEmpty() ? resolvedScriptPath() : QFileInfo(scriptPath).absoluteFilePath();
    if (!context || scriptPath.isEmpty())
    {
        early.message = scriptPath.isEmpty() ? QStringLiteral("Notification script is not configured.")
                                             : QStringLiteral("Notification context is unavailable.");
        if (callback)
            callback(early);
        return;
    }
    if (!validEvent(event) || !validPeerId(peerId) || !validDetail(detail) ||
        !QFileInfo::exists(scriptPath))
    {
        early.message = !QFileInfo::exists(scriptPath)
                            ? QStringLiteral("Notification script does not exist: %1").arg(scriptPath)
                            : QStringLiteral("Notification script arguments are invalid.");
        LOG_WARN("{}", early.message);
        auditScriptFailure(event, peerId, early.message);
        if (callback)
            callback(early);
        return;
    }

    auto *process = new QProcess(context);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    const QPointer<QProcess> processGuard(process);
    const auto state = std::make_shared<InvocationState>();
    const auto finish = [processGuard, timer, state, callback, event, peerId](NotificationScriptResult result) {
        if (state->completed)
            return;
        state->completed = true;
        timer->stop();
        result.output = QString::fromLocal8Bit(state->captured);
        if (!result.success)
        {
            LOG_WARN("Notification script failed for {} / {}: {}", event, peerId, result.message);
            auditScriptFailure(event, peerId, result.message);
        }
        if (callback)
            callback(result);
        if (processGuard)
            processGuard->deleteLater();
    };
    QObject::connect(process, &QProcess::readyReadStandardOutput, process, [process, state]() {
        const QByteArray data = process->readAllStandardOutput();
        if (state->captured.size() < kMaximumCapturedOutput)
            state->captured.append(data.left(kMaximumCapturedOutput - state->captured.size()));
    });
    QObject::connect(process, &QProcess::readyReadStandardError, process, [process, state]() {
        const QByteArray data = process->readAllStandardError();
        if (state->captured.size() < kMaximumCapturedOutput)
            state->captured.append(data.left(kMaximumCapturedOutput - state->captured.size()));
    });
    QObject::connect(process,
                     static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                     process,
                     [finish](int exitCode, QProcess::ExitStatus status) {
        NotificationScriptResult result;
        result.started = true;
        result.exitCode = exitCode;
        result.success = status == QProcess::NormalExit && exitCode == 0;
        result.message = result.success
                             ? QCoreApplication::translate("NotificationScriptRunner", "Notification script completed successfully.")
                             : QCoreApplication::translate("NotificationScriptRunner", "Notification script exited with code %1.").arg(exitCode);
        finish(result);
    });
    QObject::connect(process, &QProcess::errorOccurred, process, [finish](QProcess::ProcessError error) {
        if (error == QProcess::Timedout)
            return;
        NotificationScriptResult result;
        result.message = QCoreApplication::translate("NotificationScriptRunner", "Notification script failed to start or run.");
        finish(result);
    });
    QObject::connect(timer, &QTimer::timeout, process, [process, finish]() {
        process->kill();
        NotificationScriptResult result;
        result.started = true;
        result.timedOut = true;
        result.message = QCoreApplication::translate("NotificationScriptRunner", "Notification script timed out.");
        finish(result);
    });

    const QStringList arguments{event, peerId, detail};
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    if (scriptPath.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive) ||
        scriptPath.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive))
    {
        const QString command = quoteBatchArgument(scriptPath) + QLatin1Char(' ') +
                                quoteBatchArgument(event) + QLatin1Char(' ') +
                                quoteBatchArgument(peerId) + QLatin1Char(' ') +
                                quoteBatchArgument(detail);
        process->start(QString::fromLocal8Bit(qgetenv("ComSpec")),
                       QStringList{QStringLiteral("/D"), QStringLiteral("/V:OFF"),
                                   QStringLiteral("/S"), QStringLiteral("/C"), command});
    }
    else
#endif
    {
        process->start(scriptPath, arguments);
    }
    timer->start(qBound(1000, timeoutMs, 60000));
}
