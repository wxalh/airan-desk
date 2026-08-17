#include "webrtc/cli/webrtc_cli.h"

#include "common/constant.h"
#include "util/qt/qt_callback_util.h"

#include <QPointer>

namespace
{
constexpr int kMaxPendingIceCandidates = 256;
constexpr int kMaxIceCandidateChars = 16 * 1024;
}


void WebRtcCli::setRemoteDescription(const QString &data, const QString &type)
{
    if (!m_peerConnection)
        return;

    if (m_remoteDescriptionInFlight)
    {
        m_pendingRemoteDescriptionData = data;
        m_pendingRemoteDescriptionType = type;
        LOG_WARN("Remote description already in flight; retaining latest {}", type);
        return;
    }

    try
    {
        rtc::Description::Type descType;
        if (type == Constant::TYPE_OFFER)
            descType = rtc::Description::Type::Offer;
        else if (type == Constant::TYPE_ANSWER)
            descType = rtc::Description::Type::Answer;
        else
        {
            LOG_ERROR("Unknown description type: {}", type);
            return;
        }

        const bool shouldAnswer = (type == Constant::TYPE_OFFER);
        m_remoteDescriptionInFlight = true;
        rtc::Description description(data.toStdString(), descType);
        const QPointer<WebRtcCli> guard(this);
        const auto callbackLifetime = m_callbackLifetime;
        m_peerConnection->setRemoteDescription(
            description,
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
                    LOG_DEBUG("Set remote description succeeded: {}", type);
                    if (!guard->m_pendingRemoteDescriptionType.isEmpty())
                    {
                        const QString pendingData = std::move(guard->m_pendingRemoteDescriptionData);
                        const QString pendingType = std::move(guard->m_pendingRemoteDescriptionType);
                        QJsonObject pendingObject = std::move(guard->m_pendingRemoteDescriptionObject);
                        guard->m_pendingRemoteDescriptionData.clear();
                        guard->m_pendingRemoteDescriptionType.clear();
                        guard->m_pendingRemoteDescriptionObject = QJsonObject();
                        if (pendingObject.isEmpty())
                            guard->setRemoteDescription(pendingData, pendingType);
                        else
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
                    LOG_ERROR("Set remote description failed: {}, error={}", type, errorText);
                    if (!guard->m_pendingRemoteDescriptionType.isEmpty())
                    {
                        const QString pendingData = std::move(guard->m_pendingRemoteDescriptionData);
                        const QString pendingType = std::move(guard->m_pendingRemoteDescriptionType);
                        QJsonObject pendingObject = std::move(guard->m_pendingRemoteDescriptionObject);
                        guard->m_pendingRemoteDescriptionData.clear();
                        guard->m_pendingRemoteDescriptionType.clear();
                        guard->m_pendingRemoteDescriptionObject = QJsonObject();
                        if (pendingObject.isEmpty())
                            guard->setRemoteDescription(pendingData, pendingType);
                        else
                            guard->handleRemoteDescriptionMessage(pendingObject, pendingType);
                    }
                    else
                    {
                        guard->m_pendingRemoteCandidates.clear();
                        guard->m_disconnectReason = QStringLiteral("remote_description_failed");
                        emit guard->destroyCli();
                    }
                });
            });
    }
    catch (const std::exception &e)
    {
        m_remoteDescriptionInFlight = false;
        LOG_ERROR("Failed to set remote description: {}", e.what());
        m_pendingRemoteCandidates.clear();
        m_disconnectReason = QStringLiteral("remote_description_failed");
        emit destroyCli();
    }
    catch (...)
    {
        m_remoteDescriptionInFlight = false;
        LOG_ERROR("Failed to set remote description: unknown error");
        m_pendingRemoteCandidates.clear();
        m_disconnectReason = QStringLiteral("remote_description_failed");
        emit destroyCli();
    }
}


void WebRtcCli::addIceCandidate(const QString &candidate, const QString &mid)
{
    addRemoteCandidateOrQueue(candidate, mid);
}


void WebRtcCli::addRemoteCandidateOrQueue(const QString &candidate, const QString &mid)
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
            LOG_ERROR("ICE candidate queue is full; destroying the incomplete controlled session");
            m_disconnectReason = QStringLiteral("ice_candidate_queue_overflow");
            emit destroyCli();
            return;
        }
        m_pendingRemoteCandidates.append(pendingCandidate);
        LOG_DEBUG("Queued remote ICE candidate until remote description is set, pending={}", m_pendingRemoteCandidates.size());
        return;
    }

    try
    {
        rtc::Candidate rtcCandidate(candidate.toStdString(), mid.toStdString());
        m_peerConnection->addRemoteCandidate(rtcCandidate);
        LOG_TRACE("Added ICE candidate");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to add ICE candidate: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Failed to add ICE candidate: unknown error");
    }
}


void WebRtcCli::flushPendingRemoteCandidates()
{
    if (!m_peerConnection || !m_remoteDescriptionSet || m_pendingRemoteCandidates.isEmpty())
        return;

    const QVector<QPair<QString, QString>> pending = m_pendingRemoteCandidates;
    m_pendingRemoteCandidates.clear();
    for (const auto &candidate : pending)
        addRemoteCandidateOrQueue(candidate.first, candidate.second);
}
