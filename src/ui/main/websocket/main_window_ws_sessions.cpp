#include "ui/main/main_window.h"

#include "common/constant.h"
#include "security/audit_logger.h"
#include "security/audit_session.h"
#include "security/controlled_access_gate.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"

#include <QMetaObject>
#include <QPointer>
#include <QCoreApplication>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <chrono>
#include <functional>

namespace
{
constexpr int kMaxControlledSessionCount = 16;
constexpr int kMaxControlledSessionsPerPeer = 8;
constexpr int kMaxPendingAccessEvaluations = 64;

class BackgroundTask final : public QRunnable
{
public:
    explicit BackgroundTask(std::function<void()> task)
        : m_task(std::move(task))
    {
    }

    void run() override
    {
        if (m_task)
            m_task();
    }

private:
    std::function<void()> m_task;
};

void runBackgroundTask(std::function<void()> task)
{
    QThreadPool::globalInstance()->start(new BackgroundTask(std::move(task)));
}

void finishUnusedAuditSession(std::shared_ptr<AuditSession> auditSession, const QString &reason)
{
    if (!auditSession)
        return;
    runBackgroundTask([auditSession = std::move(auditSession), reason]() {
        auditSession->finish(reason);
    });
}

void revokeUnusedAuditSession(std::shared_ptr<AuditSession> auditSession, const QString &reason)
{
    if (!auditSession)
        return;
    runBackgroundTask([auditSession = std::move(auditSession), reason]() {
        auditSession->record(QStringLiteral("connection_request_revoked"),
                             QJsonObject{{QStringLiteral("reason"), reason}});
        auditSession->finish(reason);
    });
}

void scheduleAccessQueueOverflowAudit(
    const std::shared_ptr<std::atomic_ullong> &overflowCount,
    const std::shared_ptr<std::atomic_bool> &auditScheduled,
    const QString &sender,
    const QJsonObject &object)
{
    overflowCount->fetch_add(1);
    if (auditScheduled->exchange(true))
        return;

    const QString sessionId = JsonUtil::getString(object, Constant::KEY_SESSION_ID);
    const QString sourceIp = JsonUtil::getString(object, QStringLiteral("source_ip"));
    runBackgroundTask([overflowCount, auditScheduled, sender, sessionId, sourceIp]() {
        for (;;)
        {
            const unsigned long long count = overflowCount->exchange(0);
            if (count > 0)
            {
                const QJsonObject fields{
                    {QStringLiteral("session_id"), sessionId},
                    {QStringLiteral("peer_id"), sender},
                    {QStringLiteral("source_ip"), sourceIp},
                    {QStringLiteral("success"), false},
                    {QStringLiteral("reason"), QStringLiteral("access_evaluation_queue_full")},
                    {QStringLiteral("overflow_count"), static_cast<double>(count)}};
                if (!AuditLogger::instance().append(
                        QStringLiteral("connection_request_queue_overflow"), fields))
                {
                    LOG_ERROR("Failed to audit controlled access queue overflow: count={}", count);
                }
            }

            auditScheduled->store(false);
            if (overflowCount->load() == 0 || auditScheduled->exchange(true))
                return;
        }
    });
}
}

bool MainWindow::handleIncomingConnectRequest(const QString &sender, const QJsonObject &object)
{
    const QString receiver = JsonUtil::getString(object, Constant::KEY_RECEIVER);
    if (receiver != ConfigUtil->local_id)
    {
        LOG_TRACE("Ignore CONNECT for unrelated receiver: {}", receiver);
        return false;
    }

    const bool notificationReady = m_trayIcon && m_trayIcon->isVisible();
    const ControlledAccessPolicySnapshot policy = ControlledAccessGate::policySnapshot();
    const quint64 policyGeneration = m_accessPolicyGeneration->load();
    const std::shared_ptr<std::atomic_bool> callbackState = m_asyncCallbacksAlive;
    const std::shared_ptr<std::atomic_int> pendingEvaluations = m_pendingAccessEvaluations;
    const int pendingBefore = pendingEvaluations->fetch_add(1);
    if (pendingBefore >= kMaxPendingAccessEvaluations)
    {
        pendingEvaluations->fetch_sub(1);
        const QString sessionId = JsonUtil::getString(object, Constant::KEY_SESSION_ID);
        LOG_WARN("Rejected CONNECT because the access evaluation queue is full: pending={}, sender={}, sessionId={}",
                 pendingBefore,
                 sender,
                 sessionId);
        scheduleAccessQueueOverflowAudit(m_accessQueueOverflowCount,
                                         m_accessQueueOverflowAuditScheduled,
                                         sender,
                                         object);
        const QJsonObject errorMsg = JsonUtil::createObject()
                                         .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                         .add(Constant::KEY_TYPE, Constant::TYPE_ERROR)
                                         .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                         .add(Constant::KEY_RECEIVER, sender)
                                         .add(Constant::KEY_SESSION_ID, sessionId)
                                         .add(Constant::KEY_DATA, Constant::ERROR_CONTROLLED_ACCESS_UNAVAILABLE)
                                         .build();
        emit sendWsCliTextMsg(JsonUtil::toCompactString(errorMsg));
        return true;
    }
    MainWindow *const callbackReceiver = this;
    runBackgroundTask([callbackState, pendingEvaluations, callbackReceiver, sender, object,
                       notificationReady, policy, policyGeneration]() {
        const auto started = std::chrono::steady_clock::now();
        ControlledAccessDecision decision =
            ControlledAccessGate::evaluate(sender, object, policy, notificationReady);
        decision.policyGeneration = policyGeneration;
        pendingEvaluations->fetch_sub(1);
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - started)
                                   .count();
        if (elapsedMs >= 250)
        {
            LOG_WARN("Controlled access evaluation took {} ms for peer={}, sessionId={}",
                     elapsedMs,
                     decision.peerId,
                     decision.sessionId);
        }

        if (!callbackState->load())
        {
            finishUnusedAuditSession(std::move(decision.auditSession),
                                     QStringLiteral("application_shutdown"));
            return;
        }
        QCoreApplication *application = QCoreApplication::instance();
        if (!application)
        {
            finishUnusedAuditSession(std::move(decision.auditSession),
                                     QStringLiteral("application_shutdown"));
            return;
        }
        QTimer::singleShot(0, application,
                           [callbackState, callbackReceiver, sender, object,
                            decision = std::move(decision)]() mutable {
            if (!callbackState->load())
            {
                finishUnusedAuditSession(std::move(decision.auditSession),
                                         QStringLiteral("application_shutdown"));
                return;
            }
            callbackReceiver->completeIncomingConnectRequest(sender, object, std::move(decision));
        });
    });
    return true;
}


void MainWindow::completeIncomingConnectRequest(const QString &sender,
                                                const QJsonObject &object,
                                                ControlledAccessDecision decision)
{
    if (decision.accepted &&
        (!ConfigUtil->allow_remote ||
         decision.policyGeneration != m_accessPolicyGeneration->load()))
    {
        LOG_WARN("Rejected CONNECT because controlled-access policy changed during evaluation: peer={}, sessionId={}, evaluatedGeneration={}, currentGeneration={}, allowRemote={}",
                 decision.peerId,
                 decision.sessionId,
                 decision.policyGeneration,
                 m_accessPolicyGeneration->load(),
                 ConfigUtil->allow_remote);
        decision.accepted = false;
        decision.reason = QStringLiteral("policy_changed_during_evaluation");
        revokeUnusedAuditSession(std::move(decision.auditSession), decision.reason);
    }

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
                                   .add(Constant::KEY_SESSION_ID, decision.sessionId)
                                   .add(Constant::KEY_DATA, error)
                                   .build();
        emit sendWsCliTextMsg(JsonUtil::toCompactString(errorMsg));
        return;
    }

    const bool isOnlyFile = JsonUtil::getBool(object, Constant::KEY_IS_ONLY_FILE, false);
    const QString sessionLabel = JsonUtil::getString(object, Constant::KEY_LABEL_NAME);
    const QString sessionMode = sessionLabel.startsWith(QStringLiteral("terminal-"))
                                    ? QStringLiteral("terminal")
                                    : (isOnlyFile ? QStringLiteral("file")
                                                  : QStringLiteral("desktop"));

    const QString requestedSessionId = decision.sessionId;
    if (!requestedSessionId.isEmpty())
    {
        for (auto it = m_rtcCliSessions.cbegin(); it != m_rtcCliSessions.cend(); ++it)
        {
            if (it.key() && !m_rtcCliShutdownPending.contains(it.key()) &&
                it.key()->remoteId() == sender && it.key()->sessionId() == requestedSessionId)
            {
                LOG_WARN("Ignoring duplicate WebRtcCli session retry: sessionId={}", requestedSessionId);
                finishUnusedAuditSession(std::move(decision.auditSession),
                                         QStringLiteral("duplicate_session_retry"));
                return;
            }
        }
    }

    // A reconnect uses a new session ID. Do not create a second controlled
    // peer while the previous peer of the same mode is still negotiating;
    // otherwise both peers can consume resources until their startup timers
    // expire and the controller may receive competing signaling errors.
    for (auto it = m_rtcCliSessions.cbegin(); it != m_rtcCliSessions.cend(); ++it)
    {
        if (it.key() && !m_rtcCliShutdownPending.contains(it.key()) &&
            it.key()->remoteId() == sender &&
            it.key()->controlledSessionMode() == sessionMode &&
            !it.key()->isConnected())
        {
            LOG_WARN("Ignoring duplicate pending WebRtcCli session retry: peer={}, mode={}, sessionId={}, existingSessionId={}",
                     sender,
                     sessionMode,
                     requestedSessionId,
                     it.key()->sessionId());
            QJsonObject errorMsg = JsonUtil::createObject()
                                       .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                       .add(Constant::KEY_TYPE, Constant::TYPE_ERROR)
                                       .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                       .add(Constant::KEY_RECEIVER, sender)
                                       .add(Constant::KEY_SESSION_ID, requestedSessionId)
                                       .add(Constant::KEY_DATA, Constant::ERROR_CONTROLLED_ACCESS_UNAVAILABLE)
                                       .build();
            emit sendWsCliTextMsg(JsonUtil::toCompactString(errorMsg));
            finishUnusedAuditSession(std::move(decision.auditSession),
                                     QStringLiteral("duplicate_pending_session_retry"));
            return;
        }
    }

    int activeSessionCount = 0;
    int peerSessionCount = 0;
    for (auto it = m_rtcCliSessions.cbegin(); it != m_rtcCliSessions.cend(); ++it)
    {
        if (!it.key() || m_rtcCliShutdownPending.contains(it.key()))
            continue;
        ++activeSessionCount;
        if (it.key()->remoteId() == sender)
            ++peerSessionCount;
    }
    if (activeSessionCount >= kMaxControlledSessionCount ||
        peerSessionCount >= kMaxControlledSessionsPerPeer)
    {
        LOG_WARN("Rejecting controlled session because the resource limit was reached: total={}, peer={}, sender={}, sessionId={}",
                 activeSessionCount,
                 peerSessionCount,
                 sender,
                 requestedSessionId);
        QJsonObject errorMsg = JsonUtil::createObject()
                                   .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                   .add(Constant::KEY_TYPE, Constant::TYPE_ERROR)
                                   .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                   .add(Constant::KEY_RECEIVER, sender)
                                   .add(Constant::KEY_SESSION_ID, requestedSessionId)
                                   .add(Constant::KEY_DATA, Constant::ERROR_CONTROLLED_ACCESS_UNAVAILABLE)
                                   .build();
        emit sendWsCliTextMsg(JsonUtil::toCompactString(errorMsg));
        finishUnusedAuditSession(std::move(decision.auditSession),
                                 QStringLiteral("session_limit"));
        return;
    }
    int fps = JsonUtil::getInt(object, Constant::KEY_FPS, 25);
    const int requestedWidth = JsonUtil::getInt(object, Constant::KEY_WIDTH, -1);
    const int requestedHeight = JsonUtil::getInt(object, Constant::KEY_HEIGHT, -1);
    const QString networkPath = JsonUtil::getString(object, Constant::KEY_NETWORK_PATH, QStringLiteral("auto"));
    const QString mediaTopology = JsonUtil::getString(object, Constant::KEY_MEDIA_TOPOLOGY, QStringLiteral("p2p"));
    const QString qualityProfile = JsonUtil::getString(object, Constant::KEY_QUALITY_PROFILE, QStringLiteral("auto"));
    const QString audioMode = JsonUtil::getString(object, Constant::KEY_AUDIO_MODE, QStringLiteral("off"));
    const QString sessionId = decision.sessionId;
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
    const QPointer<WebRtcCli> destroyGuard(rtcCli);
    connect(rtcCli, &WebRtcCli::destroyCli, this,
            [this, destroyGuard]() {
                if (destroyGuard)
                    destroyWebRtcCli(destroyGuard.data());
            },
            Qt::QueuedConnection);

    connect(rtcCli, &WebRtcCli::shutdownFinished, rtcCliThread, &QThread::quit, Qt::DirectConnection);
    rtcCli->moveToThread(rtcCliThread);
    rtcCliThread->start();
    m_rtcCliSessions.insert(rtcCli, rtcCliThread);
    QMetaObject::invokeMethod(rtcCli, "setDesktopLocked", Qt::QueuedConnection, Q_ARG(bool, m_desktopLocked));
    QMetaObject::invokeMethod(rtcCli, "init", Qt::QueuedConnection);
}
