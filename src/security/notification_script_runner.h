#ifndef NOTIFICATION_SCRIPT_RUNNER_H
#define NOTIFICATION_SCRIPT_RUNNER_H

#include <QString>

#include <functional>

class QObject;

struct NotificationScriptResult
{
    bool started{false};
    bool success{false};
    bool timedOut{false};
    int exitCode{-1};
    QString message;
    QString output;
};

namespace NotificationScriptRunner
{
using Callback = std::function<void(const NotificationScriptResult &)>;

void runAsync(QObject *context,
              const QString &event,
              const QString &peerId,
              const QString &detail,
              Callback callback = Callback(),
              int timeoutMs = 10000,
              const QString &scriptPathOverride = QString());
QString resolvedScriptPath();
bool isReady();
}

#endif /* NOTIFICATION_SCRIPT_RUNNER_H */
