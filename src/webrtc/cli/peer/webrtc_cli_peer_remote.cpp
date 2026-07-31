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
                    guard->m_remoteDescriptionSet = true;
                    guard->flushPendingRemoteCandidates();
                    if (shouldAnswer)
                        guard->m_peerConnection->createAnswer();
                    LOG_DEBUG("Set remote description succeeded: {}", type);
                });
            },
            [callbackLifetime, type](const std::string &error) {
                auto permit = callbackLifetime->tryEnter();
                if (!permit)
                    return;
                LOG_ERROR("Set remote description failed: {}, error={}", type, error);
            });
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to set remote description: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Failed to set remote description: unknown error");
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

    if (!m_remoteDescriptionSet)
    {
        if (m_pendingRemoteCandidates.size() >= kMaxPendingIceCandidates)
        {
            LOG_WARN("Rejected ICE candidate because the pending queue is full");
            return;
        }
        m_pendingRemoteCandidates.append(qMakePair(candidate, mid));
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
