#include "webrtc/cli/webrtc_cli.h"


void WebRtcCli::initPeerConnection()
{
    try
    {
        rtc::Configuration config = buildRtcConfiguration();

        
        m_peerConnection = std::make_shared<rtc::PeerConnection>(config);
        LOG_INFO("PeerConnection created successfully, networkPath={}, mediaTopology={}",
                 m_networkPath, m_mediaTopology);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to initialize PeerConnection: {}", e.what());
        sendSignalingError(tr("Remote PeerConnection initialization failed: %1").arg(QString::fromUtf8(e.what())));
    }
    catch (...)
    {
        LOG_ERROR("Unknown error during PeerConnection initialization");
        sendSignalingError(tr("Remote PeerConnection initialization failed: unknown error"));
    }
}
