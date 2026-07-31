#include "ui/main/main_window.h"

#include "common/constant.h"
#include "security/audit_session.h"
#include "security/controlled_access_gate.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"

#include <QMetaObject>
#include <QThread>

bool MainWindow::handleIncomingConnectRequest(const QString &sender, const QJsonObject &object)
{
    const QString receiver = JsonUtil::getString(object, Constant::KEY_RECEIVER);
    if (receiver != ConfigUtil->local_id)
    {
        LOG_TRACE("Ignore CONNECT for unrelated receiver: {}", receiver);
        return false;
    }

    const bool notificationReady = m_trayIcon && m_trayIcon->isVisible();
    const ControlledAccessDecision decision = ControlledAccessGate::evaluate(sender, object, notificationReady);
    if (!decision.accepted)
    {
        LOG_WARN("Rejected CONNECT from {}: {}", sender, decision.reason);
        const QString error = decision.reason == QStringLiteral("authentication_failed")
                                  ? Constant::ERROR_PASSWORD_INCORRECT
                                  : (decision.reason == QStringLiteral("controlled_access_disabled")
                                         ? Constant::ERROR_CONTROLLED_ACCESS_DISABLED
                                         : Constant::ERROR_CONTROLLED_ACCESS_UNAVAILABLE);
        QJsonObject errorMsg = JsonUtil::createObject()
                                   .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                   .add(Constant::KEY_TYPE, Constant::TYPE_ERROR)
                                   .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                   .add(Constant::KEY_RECEIVER, sender)
                                   .add(Constant::KEY_DATA, error)
                                   .build();
        emit sendWsCliTextMsg(JsonUtil::toCompactString(errorMsg));
        return false;
    }

    int fps = JsonUtil::getInt(object, Constant::KEY_FPS, 25);
    bool isOnlyFile = JsonUtil::getBool(object, Constant::KEY_IS_ONLY_FILE, false);
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

    LOG_INFO("Received connection request; initial desktop constraint {}x{}, networkPath={}, mediaTopology={}, qualityProfile={}, audioMode={}, maxFps={}, wgc={}, dxgi={}, dxgiNativeGpu={}",
             requestedWidth,
             requestedHeight,
             networkPath,
             mediaTopology,
             qualityProfile,
             audioMode,
             fps,
             ConfigUtil->enable_wgc_capture,
             ConfigUtil->enable_dxgi_capture,
             ConfigUtil->enable_dxgi_native_gpu_capture);

    QThread *rtcCliThread = new QThread();
    QString senderName = QString("WebRtcCli_%1_%2").arg(sender, isOnlyFile ? "file" : "desktop");
    rtcCliThread->setObjectName(senderName);
    WebRtcCli *rtcCli = new WebRtcCli(sender, fps, isOnlyFile, requestedWidth, requestedHeight, networkPath, mediaTopology, qualityProfile, audioMode, sessionId, decision.auditSession);
    rtcCli->setControlledSessionMode(sessionMode);

    connect(rtcCli, &WebRtcCli::controlledSessionConnected, this,
            &MainWindow::onControlledSessionConnected);
    connect(rtcCli, &WebRtcCli::controlledSessionDisconnected, this,
            &MainWindow::onControlledSessionDisconnected);

    connect(m_ws, &WsCli::onWsCliRecvBinaryMsg, rtcCli, &WebRtcCli::onWsCliRecvBinaryMsg);
    connect(m_ws, &WsCli::onWsCliRecvTextMsg, rtcCli, &WebRtcCli::onWsCliRecvTextMsg);
    connect(rtcCli, &WebRtcCli::sendWsCliBinaryMsg, m_ws, &WsCli::sendWsCliBinaryMsg);
    connect(rtcCli, &WebRtcCli::sendWsCliTextMsg, m_ws, &WsCli::sendWsCliTextMsg);
    connect(rtcCli, &WebRtcCli::destroyCli, this, &MainWindow::onDestroyWebRtcCli);

    connect(rtcCli, &WebRtcCli::shutdownFinished, rtcCliThread, &QThread::quit, Qt::DirectConnection);
    rtcCli->moveToThread(rtcCliThread);
    rtcCliThread->start();
    m_rtcCliSessions.insert(rtcCli, rtcCliThread);
    QMetaObject::invokeMethod(rtcCli, "setDesktopLocked", Qt::QueuedConnection, Q_ARG(bool, m_desktopLocked));
    QMetaObject::invokeMethod(rtcCli, "init", Qt::QueuedConnection);
    return true;
}
