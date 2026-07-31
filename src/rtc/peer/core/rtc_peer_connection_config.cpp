#include "rtc/core/rtc_internal.h"

#include "common/logger_manager.h"

#include <utility>

namespace rtc
{

webrtc::PeerConnectionInterface::RTCConfiguration PeerConnection::toNativeConfiguration(const Configuration &config)
{
    webrtc::PeerConnectionInterface::RTCConfiguration native;
    native.type = config.iceTransportPolicy == TransportPolicy::Relay ? webrtc::PeerConnectionInterface::kRelay : webrtc::PeerConnectionInterface::kAll;
    native.tcp_candidate_policy = config.enableIceTcp ? webrtc::PeerConnectionInterface::kTcpCandidatePolicyEnabled : webrtc::PeerConnectionInterface::kTcpCandidatePolicyDisabled;
    native.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    native.set_cpu_adaptation(true);
    native.set_prerenderer_smoothing(false);

    for (size_t i = 0; i < config.iceServers.size(); ++i)
    {
        webrtc::PeerConnectionInterface::IceServer server;
        server.urls.push_back(config.iceServers[i].uri());
        server.uri = config.iceServers[i].uri();
        server.username = config.iceServers[i].username;
        server.password = config.iceServers[i].password;
        native.servers.push_back(std::move(server));
        LOG_INFO("Configured ICE server[{}]: uri={}", i, config.iceServers[i].uri());
    }
    return native;
}
} // namespace rtc
