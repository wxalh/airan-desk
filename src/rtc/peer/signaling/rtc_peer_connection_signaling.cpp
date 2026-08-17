#include "rtc/core/rtc_internal.h"
#include "rtc/peer/async/rtc_peer_connection_async_helpers.h"
#include "rtc/signaling/rtc_sdp_video_util.h"

#include "common/logger_manager.h"

#include <api/peer_connection_interface.h>

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

namespace rtc
{


void PeerConnection::setLocalDescription(Description::Type type)
{
    if (m_closed.load() || !m_pc)
    {
        LOG_WARN("Ignoring local description request because PeerConnection is closed");
        return;
    }
    LOG_DEBUG("Creating local description: type={}", type == Description::Type::Offer ? "offer" : "answer");
    std::function<void(Description)> onLocalDescription;
    {
        std::lock_guard<std::mutex> locker(m_callbackMutex);
        onLocalDescription = m_onLocalDescription;
    }
    auto observer = createLocalDescriptionObserver(m_pc,
                                                   std::move(onLocalDescription),
                                                   type == Description::Type::Answer && m_acceptRemoteVideoSimulcast);
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
    if (type == Description::Type::Offer)
        m_pc->CreateOffer(observer.get(), options);
    else
        m_pc->CreateAnswer(observer.get(), options);
}


void PeerConnection::setRemoteDescription(const Description &description,
                                          std::function<void()> onSuccess,
                                          std::function<void(std::string)> onFailure)
{
    if (m_closed.load() || !m_pc)
    {
        if (onFailure)
            onFailure("PeerConnection is closed");
        return;
    }
    const std::string sdp = std::string(description);
    LOG_DEBUG("Setting remote description: type={}, size={} bytes", description.typeString(), sdp.size());
    logSdpVideoCodecs("Remote", description.typeString(), sdp);
    webrtc::SdpParseError error;
    auto native = webrtc::CreateSessionDescription(toNativeSdpType(description.type()), sdp, &error);
    if (!native)
    {
        const std::string message = "SDP parse failed: " + error.description;
        LOG_ERROR("{}", message);
        if (onFailure)
            onFailure(message);
        return;
    }
    auto observer = createSetRemoteDescriptionObserver(std::move(onSuccess), std::move(onFailure));
    m_pc->SetRemoteDescription(std::move(native), observer);
}


void PeerConnection::addRemoteCandidate(const Candidate &candidate)
{
    if (m_closed.load() || !m_pc)
    {
        LOG_WARN("Ignoring remote ICE candidate because PeerConnection is closed");
        return;
    }
    LOG_DEBUG("Adding remote ICE candidate: mid={}, size={} bytes", candidate.mid(), std::string(candidate).size());
    webrtc::SdpParseError error;
#if AIRAN_WEBRTC_MILESTONE >= 144
    auto native = webrtc::IceCandidate::Create(candidate.mid(), parseMLineIndex(candidate.mid()), std::string(candidate), &error);
    if (!native)
        throw std::runtime_error("ICE candidate parse failed: " + error.description);
    m_pc->AddIceCandidate(std::move(native), [](webrtc::RTCError error) {
        if (!error.ok())
            LOG_WARN("AddIceCandidate failed: {}", error.message());
    });
#else
    std::unique_ptr<webrtc::IceCandidateInterface> native(
        webrtc::CreateIceCandidate(candidate.mid(), parseMLineIndex(candidate.mid()), std::string(candidate), &error));
    if (!native)
        throw std::runtime_error("ICE candidate parse failed: " + error.description);
    if (!m_pc->AddIceCandidate(native.get()))
        LOG_WARN("AddIceCandidate failed");
#endif
}


void PeerConnection::createAnswer()
{
    reapplyVideoCodecPreferences();
    setLocalDescription(Description::Type::Answer);
}

} // namespace rtc
