#include "webrtc/ctl/webrtc_ctl.h"
#include "webrtc/codec/video_codec_capability_signaling.h"
#include "common/constant.h"
#include "util/json/json_util.h"
#include "util/qt/qt_callback_util.h"

#include <utility>
#include <QCoreApplication>
#include <QPointer>

namespace
{
constexpr int kMaxPendingIceCandidates = 256;
constexpr int kMaxIceCandidateChars = 16 * 1024;
constexpr int kMaxSdpChars = 4 * 1024 * 1024;
}

/*
 * Handles signaling errors returned by the remote side and reports a user-visible reason.
 */
void WebRtcCtl::handleSignalingError(const QJsonObject &object)
{
    QString data = JsonUtil::getString(object, Constant::KEY_DATA);
    if (data.isEmpty())
        data = JsonUtil::getString(object, Constant::KEY_ERROR);
    const QString trimmedData = data.trimmed();
    const QString reason = (data == Constant::ERROR_PASSWORD_INCORRECT)
                               ? tr("Incorrect password")
                               : data.trimmed();
    emit connectionStatusChanged(tr("Connection failed: %1")
                                     .arg(reason.isEmpty() ? tr("Unknown reason") : reason));
    LOG_ERROR("Peer signaling error: {}", data);

    // Authentication and controlled-access errors are terminal for this
    // request. Other TYPE_ERROR messages originate from the controlled
    // WebRTC setup path; rebuild the peer immediately instead of waiting for
    // the startup timer to expire with a dead remote session.
    const bool accessFailure = trimmedData == Constant::ERROR_PASSWORD_INCORRECT ||
                               trimmedData == Constant::ERROR_CONTROLLED_ACCESS_DISABLED ||
                               trimmedData == Constant::ERROR_CONTROLLED_ACCESS_UNAVAILABLE;
    if (!accessFailure)
        requestSessionReconnect(tr("Remote WebRTC startup failed, reconnecting..."));
}

/*
 * Applies a remote SDP offer or answer and the optional codec capability payload.
 */
void WebRtcCtl::handleRemoteDescriptionMessage(const QJsonObject &object, const QString &type)
{
    QString data = JsonUtil::getString(object, Constant::KEY_DATA);
    if (data.isEmpty() || data.size() > kMaxSdpChars)
    {
        LOG_WARN("Rejected invalid remote description size: {}", data.size());
        return;
    }

    if (m_remoteDescriptionInFlight)
    {
        m_pendingRemoteDescriptionData = data;
        m_pendingRemoteDescriptionType = type;
        m_pendingRemoteDescriptionObject = object;
        LOG_WARN("Remote description already in flight; retaining latest {}", type);
        return;
    }

    try
    {
        auto remoteCapabilities = parseVideoCodecCapabilities(object);
        if (!remoteCapabilities.empty())
        {
            logVideoCodecCapabilities("Remote", remoteCapabilities);
            m_peerConnection->setRemoteVideoCodecCapabilities(std::move(remoteCapabilities));
            emit connectionStatusChanged(tr("Remote codec initialization complete: %1")
                                             .arg(summarizeVideoCodecCapabilities(parseVideoCodecCapabilities(object), this)));
        }
        LOG_DEBUG("Setting remote description: {}", type);
        emit connectionStatusChanged(tr("Received remote %1").arg(type));
        rtc::Description desc(data.toStdString(), type.toStdString());
        m_remoteDescriptionInFlight = true;
        const bool shouldAnswer = (type == Constant::TYPE_OFFER);
        const QPointer<WebRtcCtl> guard(this);
        const auto callbackLifetime = m_callbackLifetime;
        m_peerConnection->setRemoteDescription(
            desc,
            [guard, callbackLifetime, type, shouldAnswer]() {
                auto permit = callbackLifetime->tryEnter();
                if (!permit)
                    return;
                if (!guard)
                    return;
                guard->m_callbackDispatcher->post([guard, type, shouldAnswer]() {
                    if (!guard || guard->m_shutdownStarted.load() || !guard->m_peerConnection)
                        return;
                    guard->m_remoteDescriptionInFlight = false;
                    guard->m_remoteDescriptionSet = true;
                    if (shouldAnswer)
                        guard->m_peerConnection->createAnswer();
                    emit guard->connectionStatusChanged(shouldAnswer
                                                            ? QCoreApplication::translate("WebRtcCtl", "Remote description set, creating answer")
                                                            : QCoreApplication::translate("WebRtcCtl", "Remote description set, continuing ICE negotiation"));
                    LOG_DEBUG("Remote description set successfully: {}", type);
                    if (!guard->m_pendingRemoteDescriptionType.isEmpty())
                    {
                        const QString pendingData = std::move(guard->m_pendingRemoteDescriptionData);
                        const QString pendingType = std::move(guard->m_pendingRemoteDescriptionType);
                        QJsonObject pendingObject = std::move(guard->m_pendingRemoteDescriptionObject);
                        guard->m_pendingRemoteDescriptionData.clear();
                        guard->m_pendingRemoteDescriptionType.clear();
                        guard->m_pendingRemoteDescriptionObject = QJsonObject();
                        if (pendingObject.isEmpty())
                            pendingObject.insert(Constant::KEY_DATA, pendingData);
                        guard->handleRemoteDescriptionMessage(pendingObject, pendingType);
                    }
                    else
                    {
                        guard->flushPendingRemoteCandidates();
                    }
                });
            },
            [guard, callbackLifetime, type](const std::string &error) {
                auto permit = callbackLifetime->tryEnter();
                if (!permit)
                    return;
                if (!guard)
                    return;
                const QString errorText = QString::fromStdString(error);
                guard->m_callbackDispatcher->post([guard, type, errorText]() {
                    if (!guard || guard->m_shutdownStarted.load())
                        return;
                    guard->m_remoteDescriptionInFlight = false;
                    emit guard->connectionStatusChanged(
                        QCoreApplication::translate("WebRtcCtl", "Remote description set failed: %1").arg(errorText));
                    LOG_ERROR("Remote description set failed: {}, error={}", type, errorText);
                    if (!guard->m_pendingRemoteDescriptionType.isEmpty())
                    {
                        const QString pendingData = std::move(guard->m_pendingRemoteDescriptionData);
                        const QString pendingType = std::move(guard->m_pendingRemoteDescriptionType);
                        QJsonObject pendingObject = std::move(guard->m_pendingRemoteDescriptionObject);
                        guard->m_pendingRemoteDescriptionData.clear();
                        guard->m_pendingRemoteDescriptionType.clear();
                        guard->m_pendingRemoteDescriptionObject = QJsonObject();
                        if (pendingObject.isEmpty())
                            pendingObject.insert(Constant::KEY_DATA, pendingData);
                        guard->handleRemoteDescriptionMessage(pendingObject, pendingType);
                    }
                    else
                    {
                        guard->m_pendingRemoteCandidates.clear();
                        guard->requestSessionReconnect(
                            QCoreApplication::translate("WebRtcCtl", "Remote description failed, reconnecting..."));
                    }
                });
            });
    }
    catch (const std::exception &e)
    {
        m_remoteDescriptionInFlight = false;
        LOG_ERROR("Failed to set remote description: {}", e.what());
        m_pendingRemoteCandidates.clear();
        requestSessionReconnect(tr("Remote description failed, reconnecting..."));
    }
    catch (...)
    {
        m_remoteDescriptionInFlight = false;
        LOG_ERROR("Failed to set remote description: unknown error");
        m_pendingRemoteCandidates.clear();
        requestSessionReconnect(tr("Remote description failed, reconnecting..."));
    }
}

/*
 * Handles a remote ICE candidate message and queues it until the remote description is ready.
 */
void WebRtcCtl::handleRemoteCandidateMessage(const QJsonObject &object)
{
    QString candidateStr = JsonUtil::getString(object, Constant::KEY_DATA);
    QString mid = JsonUtil::getString(object, Constant::KEY_MID);

    if (!candidateStr.isEmpty() && !mid.isEmpty())
        addRemoteCandidateOrQueue(candidateStr, mid);
}

/*
 * Adds a remote ICE candidate immediately or queues it until the remote description is set.
 */
void WebRtcCtl::addRemoteCandidateOrQueue(const QString &candidate, const QString &mid)
{
    if (!m_peerConnection)
        return;
    if (candidate.isEmpty() || candidate.size() > kMaxIceCandidateChars || mid.size() > 256)
    {
        LOG_WARN("Rejected invalid ICE candidate: candidateChars={}, midChars={}", candidate.size(), mid.size());
        return;
    }

    if (!m_remoteDescriptionSet || m_remoteDescriptionInFlight ||
        !m_pendingRemoteDescriptionType.isEmpty())
    {
        const QPair<QString, QString> pendingCandidate = qMakePair(candidate, mid);
        if (m_pendingRemoteCandidates.contains(pendingCandidate))
        {
            LOG_TRACE("Ignored duplicate pending ICE candidate");
            return;
        }
        if (m_pendingRemoteCandidates.size() >= kMaxPendingIceCandidates)
        {
            LOG_ERROR("ICE candidate queue is full, reconnecting instead of dropping signaling data");
            requestSessionReconnect(tr("ICE candidate queue is full, reconnecting..."));
            return;
        }
        m_pendingRemoteCandidates.append(pendingCandidate);
        LOG_DEBUG("Queued remote ICE candidate until remote description is set, pending={}", m_pendingRemoteCandidates.size());
        return;
    }

    try
    {
        m_peerConnection->addRemoteCandidate(rtc::Candidate(candidate.toStdString(), mid.toStdString()));
        LOG_DEBUG("Added remote candidate");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to add remote candidate: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Failed to add remote candidate: unknown error");
    }
}

/*
 * Flushes queued ICE candidates after the remote description has been applied.
 */
void WebRtcCtl::flushPendingRemoteCandidates()
{
    if (!m_peerConnection || !m_remoteDescriptionSet || m_pendingRemoteCandidates.isEmpty())
        return;

    const QVector<QPair<QString, QString>> pending = m_pendingRemoteCandidates;
    m_pendingRemoteCandidates.clear();
    for (const auto &candidate : pending)
    {
        addRemoteCandidateOrQueue(candidate.first, candidate.second);
    }
}
