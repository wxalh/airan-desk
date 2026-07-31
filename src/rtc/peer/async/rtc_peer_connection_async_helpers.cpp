#include "rtc/peer/async/rtc_peer_connection_async_helpers.h"

#include "common/logger_manager.h"
#include "rtc/signaling/rtc_sdp_simulcast_answer.h"
#include "rtc/signaling/rtc_sdp_video_util.h"

#include <memory>
#include <string>
#include <utility>

namespace rtc
{
namespace
{

class SetLocalObserver : public webrtc::SetLocalDescriptionObserverInterface
{
public:
    SetLocalObserver(std::function<void()> success, std::function<void(std::string)> failure)
        : m_success(std::move(success)), m_failure(std::move(failure))
    {
    }

    void OnSetLocalDescriptionComplete(webrtc::RTCError error) override
    {
        if (error.ok())
        {
            if (m_success)
                m_success();
            return;
        }
        if (m_failure)
            m_failure(error.message());
    }

private:
    std::function<void()> m_success;
    std::function<void(std::string)> m_failure;
};


class SetRemoteObserver : public webrtc::SetRemoteDescriptionObserverInterface
{
public:
    SetRemoteObserver(std::function<void()> success, std::function<void(std::string)> failure)
        : m_success(std::move(success)), m_failure(std::move(failure))
    {
    }

    void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override
    {
        if (error.ok())
        {
            LOG_INFO("SetRemoteDescription succeeded");
            if (m_success)
                m_success();
            return;
        }

        const std::string message = error.message();
        LOG_ERROR("SetRemoteDescription failed: {}", message);
        if (m_failure)
            m_failure(message);
    }

private:
    std::function<void()> m_success;
    std::function<void(std::string)> m_failure;
};


class CreateDescriptionObserver : public webrtc::CreateSessionDescriptionObserver
{
public:
    CreateDescriptionObserver(scoped_refptr<webrtc::PeerConnectionInterface> pc,
                              std::function<void(Description)> localDescription,
                              bool acceptRemoteVideoSimulcast)
        : m_pc(std::move(pc)),
          m_localDescription(std::move(localDescription)),
          m_acceptRemoteVideoSimulcast(acceptRemoteVideoSimulcast)
    {
    }

    void OnSuccess(webrtc::SessionDescriptionInterface *rawDesc) override
    {
        std::unique_ptr<webrtc::SessionDescriptionInterface> desc(rawDesc);
        if (!desc || !m_pc)
            return;

        if (m_acceptRemoteVideoSimulcast)
        {
            const size_t acceptedLayers = acceptRemoteVideoSimulcastInAnswer(*m_pc, *desc);
            if (acceptedLayers > 0)
                LOG_INFO("Accepted remote video simulcast in structured answer: layers={}", acceptedLayers);
        }

        std::string sdp;
#if AIRAN_WEBRTC_MILESTONE >= 144
        sdp = desc->ToString();
#else
        desc->ToString(&sdp);
#endif
        const std::string type = desc->type();
        LOG_DEBUG("Created local description: type={}, size={} bytes", type, sdp.size());
        logSdpVideoCodecs("Local", type, sdp);
        auto observer = make_ref_counted<SetLocalObserver>(
            [localDescription = m_localDescription, sdp, type]() {
                LOG_DEBUG("SetLocalDescription succeeded");
                if (localDescription)
                    localDescription(Description(sdp, type));
            },
            [](std::string error) { LOG_ERROR("SetLocalDescription failed: {}", error); });
        m_pc->SetLocalDescription(std::move(desc), observer);
    }

    void OnFailure(webrtc::RTCError error) override
    {
        LOG_ERROR("Create local description failed: {}", error.message());
    }

private:
    scoped_refptr<webrtc::PeerConnectionInterface> m_pc;
    std::function<void(Description)> m_localDescription;
    bool m_acceptRemoteVideoSimulcast{false};
};
} // namespace

scoped_refptr<webrtc::CreateSessionDescriptionObserver>
createLocalDescriptionObserver(scoped_refptr<webrtc::PeerConnectionInterface> pc,
                               std::function<void(Description)> localDescription,
                               bool acceptRemoteVideoSimulcast)
{
    return make_ref_counted<CreateDescriptionObserver>(std::move(pc),
                                                       std::move(localDescription),
                                                       acceptRemoteVideoSimulcast);
}

scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface>
createSetRemoteDescriptionObserver(std::function<void()> success,
                                   std::function<void(std::string)> failure)
{
    return make_ref_counted<SetRemoteObserver>(std::move(success), std::move(failure));
}

} // namespace rtc
