#include "webrtc/cli/lifecycle/webrtc_cli_session_shutdown.h"

#include "common/worker_object_shutdown.h"
#include "webrtc/cli/webrtc_cli.h"

#include <QCoreApplication>
#include <QThread>

namespace WebRtcCliSessionShutdown
{
bool shutdown(WebRtcCli *webrtcCli, QThread *rtcCliThread)
{
    const QString tag = rtcCliThread && !rtcCliThread->objectName().isEmpty()
                            ? rtcCliThread->objectName()
                            : QStringLiteral("WebRtcCli");
    return WorkerObjectShutdown::shutdownAndDelete(
        webrtcCli,
        rtcCliThread,
        QCoreApplication::instance(),
        "shutdownAndMoveToOwnerThread",
        tag);
}
} // namespace WebRtcCliSessionShutdown
