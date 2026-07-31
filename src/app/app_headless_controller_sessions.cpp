#include "app/app_headless_controller.h"

#include "common/constant.h"
#include "common/logger_manager.h"
#include "security/audit_session.h"
#include "security/controlled_access_gate.h"
#include "security/notification_script_runner.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"
#include "webrtc/cli/lifecycle/webrtc_cli_session_shutdown.h"

#include <QMetaObject>
#include <QThread>

void HeadlessController::cleanupWebRtcCliSessions()
{
    auto sessions = m_rtcCliSessions;
    m_rtcCliSessions.clear();
    for (auto it = sessions.begin(); it != sessions.end(); ++it)
    {
        WebRtcCli *webrtcCli = it.key();
        QThread *rtcCliThread = it.value();
        if (m_ws && webrtcCli)
            QObject::disconnect(m_ws, nullptr, webrtcCli, nullptr);
        const bool stopped = WebRtcCliSessionShutdown::shutdown(webrtcCli, rtcCliThread);
        if (stopped)
            delete rtcCliThread;
    }
}

void HeadlessController::handleAuditFailure(const QString &reason)
{
    Q_UNUSED(reason);
    for (auto it = m_rtcCliSessions.begin(); it != m_rtcCliSessions.end(); ++it)
    {
        if (it.key())
            QMetaObject::invokeMethod(it.key(), "requestDisconnect", Qt::QueuedConnection,
                                      Q_ARG(QString, QStringLiteral("audit_failure")));
    }
}


bool HeadlessController::handleIncomingConnectRequest(const QString &sender, const QJsonObject &object)
{
    if (m_shuttingDown)
        return false;
    const QString receiver = JsonUtil::getString(object, Constant::KEY_RECEIVER);
    if (receiver != ConfigUtil->local_id)
        return false;

    const bool isOnlyFile = JsonUtil::getBool(object, Constant::KEY_IS_ONLY_FILE, false);
    const QString notificationDetail = isOnlyFile ? QStringLiteral("file") : QStringLiteral("desktop");
    if (!ConfigUtil->allow_remote || !NotificationScriptRunner::isReady())
    {
        completeIncomingConnectRequest(sender, object, false);
        return true;
    }

    NotificationScriptRunner::runAsync(
        this,
        QStringLiteral("connection_requested"),
        sender,
        notificationDetail,
        [this, sender, object](const NotificationScriptResult &result) {
            if (!result.success)
            {
                completeIncomingConnectRequest(sender, object, false);
                return;
            }
            startAuthorizedIncomingSession(sender, object,
                                           ControlledAccessGate::evaluate(sender, object, true));
        });
    return true;
}


void HeadlessController::completeIncomingConnectRequest(const QString &sender,
                                                        const QJsonObject &object,
                                                        bool notificationDelivered)
{
    if (m_shuttingDown)
        return;
    startAuthorizedIncomingSession(
        sender, object, ControlledAccessGate::evaluate(sender, object, notificationDelivered));
}


void HeadlessController::sendIncomingConnectError(const QString &sender, const QString &reason)
{
    if (!m_ws || m_shuttingDown)
        return;

    const QString error = reason == QStringLiteral("authentication_failed")
                              ? Constant::ERROR_PASSWORD_INCORRECT
                              : (reason == QStringLiteral("controlled_access_disabled")
                                     ? Constant::ERROR_CONTROLLED_ACCESS_DISABLED
                                     : Constant::ERROR_CONTROLLED_ACCESS_UNAVAILABLE);
    QJsonObject errorMsg = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_TYPE, Constant::TYPE_ERROR)
                               .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                               .add(Constant::KEY_RECEIVER, sender)
                               .add(Constant::KEY_DATA, error)
                               .build();
    QMetaObject::invokeMethod(m_ws, "sendWsCliTextMsg", Qt::QueuedConnection,
                              Q_ARG(QString, JsonUtil::toCompactString(errorMsg)));
}


void HeadlessController::startAuthorizedIncomingSession(const QString &sender,
                                                        const QJsonObject &object,
                                                        const ControlledAccessDecision &decision)
{
    if (m_shuttingDown)
        return;

    if (!decision.accepted)
    {
        const bool isOnlyFile = JsonUtil::getBool(object, Constant::KEY_IS_ONLY_FILE, false);
        const QString notificationDetail = isOnlyFile ? QStringLiteral("file") : QStringLiteral("desktop");
        if (decision.reason == QStringLiteral("authentication_failed"))
            NotificationScriptRunner::runAsync(this, QStringLiteral("authentication_failed"), sender, notificationDetail);
        sendIncomingConnectError(sender, decision.reason);
        return;
    }

    const bool isOnlyFile = JsonUtil::getBool(object, Constant::KEY_IS_ONLY_FILE, false);
    const int fps = JsonUtil::getInt(object, Constant::KEY_FPS, 25);
    const int requestedWidth = JsonUtil::getInt(object, Constant::KEY_WIDTH, -1);
    const int requestedHeight = JsonUtil::getInt(object, Constant::KEY_HEIGHT, -1);
    const QString networkPath = JsonUtil::getString(object, Constant::KEY_NETWORK_PATH, QStringLiteral("auto"));
    const QString mediaTopology = JsonUtil::getString(object, Constant::KEY_MEDIA_TOPOLOGY, QStringLiteral("p2p"));
    const QString qualityProfile = JsonUtil::getString(object, Constant::KEY_QUALITY_PROFILE, QStringLiteral("auto"));
    const QString audioMode = JsonUtil::getString(object, Constant::KEY_AUDIO_MODE, QStringLiteral("off"));
    const QString sessionId = decision.sessionId;
    const QString sessionLabel = JsonUtil::getString(object, Constant::KEY_LABEL_NAME);
    const QString sessionMode = sessionLabel.startsWith(QStringLiteral("terminal-"))
                                    ? QStringLiteral("terminal")
                                    : (isOnlyFile ? QStringLiteral("file")
                                                  : QStringLiteral("desktop"));

    LOG_INFO("Received headless connection request; initial desktop constraint {}x{}, networkPath={}, mediaTopology={}, qualityProfile={}, audioMode={}",
             requestedWidth,
             requestedHeight,
             networkPath,
             mediaTopology,
             qualityProfile,
             audioMode);

    QThread *rtcCliThread = new QThread();
    rtcCliThread->setObjectName(QStringLiteral("WebRtcCli_%1_%2").arg(sender, isOnlyFile ? QStringLiteral("file") : QStringLiteral("desktop")));
    WebRtcCli *rtcCli = new WebRtcCli(sender, fps, isOnlyFile, requestedWidth, requestedHeight, networkPath, mediaTopology, qualityProfile, audioMode, sessionId, decision.auditSession);
    rtcCli->setControlledSessionMode(sessionMode);

    connect(rtcCli, &WebRtcCli::controlledSessionConnected, this,
            &HeadlessController::onControlledSessionConnected);
    connect(rtcCli, &WebRtcCli::controlledSessionDisconnected, this,
            &HeadlessController::onControlledSessionDisconnected);

    connect(m_ws, &WsCli::onWsCliRecvBinaryMsg, rtcCli, &WebRtcCli::onWsCliRecvBinaryMsg);
    connect(m_ws, &WsCli::onWsCliRecvTextMsg, rtcCli, &WebRtcCli::onWsCliRecvTextMsg);
    connect(rtcCli, &WebRtcCli::sendWsCliBinaryMsg, m_ws, &WsCli::sendWsCliBinaryMsg);
    connect(rtcCli, &WebRtcCli::sendWsCliTextMsg, m_ws, &WsCli::sendWsCliTextMsg);
    connect(rtcCli, &WebRtcCli::destroyCli,
            this, &HeadlessController::onDestroyWebRtcCli);

    connect(rtcCli, &WebRtcCli::shutdownFinished, rtcCliThread, &QThread::quit, Qt::DirectConnection);
    rtcCli->moveToThread(rtcCliThread);
    rtcCliThread->start();
    m_rtcCliSessions.insert(rtcCli, rtcCliThread);
    QMetaObject::invokeMethod(rtcCli, "init", Qt::QueuedConnection);
}

void HeadlessController::onControlledSessionConnected(const QString &sessionId,
                                                      const QString &peerId,
                                                      const QString &mode,
                                                      const QString &sourceIp)
{
    Q_UNUSED(sessionId);
    Q_UNUSED(sourceIp);
    NotificationScriptRunner::runAsync(
        this, QStringLiteral("connection_established"), peerId, mode);
}

void HeadlessController::onControlledSessionDisconnected(const QString &sessionId,
                                                         const QString &peerId,
                                                         const QString &reason)
{
    Q_UNUSED(sessionId);
    NotificationScriptRunner::runAsync(
        this, QStringLiteral("connection_disconnected"), peerId, reason);
}

void HeadlessController::onDestroyWebRtcCli()
{
    destroyWebRtcCli(qobject_cast<WebRtcCli *>(sender()));
}


void HeadlessController::destroyWebRtcCli(WebRtcCli *webrtcCli)
{
    if (!webrtcCli || m_shuttingDown)
        return;

    const auto sessionIt = m_rtcCliSessions.find(webrtcCli);
    if (sessionIt == m_rtcCliSessions.end())
        return;

    QThread *rtcCliThread = sessionIt.value();
    m_rtcCliSessions.erase(sessionIt);
    const bool stopped = WebRtcCliSessionShutdown::shutdown(webrtcCli, rtcCliThread);
    if (stopped && rtcCliThread)
        delete rtcCliThread;
}
