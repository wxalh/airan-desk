#include "webrtc/ctl/webrtc_ctl.h"
#include "common/constant.h"
#include "util/qt/qt_callback_util.h"


void WebRtcCtl::initPeerConnection()
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
        emit connectionStatusChanged(tr("Connection failed: %1")
                                         .arg(QString::fromUtf8(e.what())));
    }
    catch (...)
    {
        LOG_ERROR("Failed to initialize PeerConnection: unknown error");
        emit connectionStatusChanged(tr("Connection failed: %1")
                                         .arg(tr("Unknown reason")));
    }
}


rtc::Configuration WebRtcCtl::buildRtcConfiguration() const
{
    rtc::Configuration config;
    config.enableAudioDeviceModule = !m_isOnlyFile && m_audioMode != QStringLiteral("off");
    config.mediaTopology = m_mediaTopology.toLower() == QStringLiteral("sfu") ? rtc::MediaTopology::Sfu
                                                                              : rtc::MediaTopology::PeerToPeer;
    const QString path = m_networkPath.toLower();
    const bool hasIceServer = !m_host.empty() && m_port != 0;
    const bool hasTurnCreds = hasIceServer && !m_username.empty() && !m_password.empty();
    const bool forceRelay = (path == QStringLiteral("turn_udp") || path == QStringLiteral("turn_tcp"));

    if (forceRelay && !hasTurnCreds)
    {
        LOG_WARN("TURN relay path requested but the private TURN configuration is incomplete; continuing without a relay server");
        return config;
    }

    if (hasIceServer && (path == QStringLiteral("direct") || path == QStringLiteral("auto")))
        config.iceServers.push_back(rtc::IceServer(m_host, m_port));

    if ((path == QStringLiteral("turn_udp") || path == QStringLiteral("auto")) && hasTurnCreds)
        config.iceServers.push_back(rtc::IceServer(m_host, m_port, m_username, m_password, rtc::IceServer::RelayType::TurnUdp));

    if ((path == QStringLiteral("turn_tcp") || path == QStringLiteral("auto")) && hasTurnCreds)
    {
        config.iceServers.push_back(rtc::IceServer(m_host, m_port, m_username, m_password, rtc::IceServer::RelayType::TurnTcp));
        config.enableIceTcp = true;
    }

    if (forceRelay)
        config.iceTransportPolicy = rtc::TransportPolicy::Relay;

    return config;
}


void WebRtcCtl::createTracks()
{
    if (!m_peerConnection)
    {
        LOG_ERROR("PeerConnection not available for creating tracks");
        return;
    }

    try
    {
        
        rtc::Description::Video videoDesc(Constant::TYPE_VIDEO.toStdString());
        videoDesc.setDirection(rtc::Description::Direction::RecvOnly);
        m_videoTrack = m_peerConnection->addTrack(videoDesc);

        if (m_audioMode != QStringLiteral("off"))
            ensureAudioTrack();

        LOG_INFO("Control side media transceivers created successfully");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to create tracks: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Failed to create tracks: unknown error");
    }
}


void WebRtcCtl::setupCallbacks()
{
    if (!m_peerConnection)
        return;

    m_peerConnection->onStateChange(makeWeakCallback(this, &WebRtcCtl::onPeerConnectionStateChanged, m_callbackLifetime));
    m_peerConnection->onIceStateChange(makeWeakCallback(this, &WebRtcCtl::onPeerIceStateChanged, m_callbackLifetime));
    m_peerConnection->onGatheringStateChange(makeWeakCallback(this, &WebRtcCtl::onPeerGatheringStateChanged, m_callbackLifetime));
    m_peerConnection->onLocalDescription(makeWeakCallback(this, &WebRtcCtl::onPeerLocalDescription, m_callbackLifetime));
    m_peerConnection->onLocalCandidate(makeWeakCallback(this, &WebRtcCtl::onPeerLocalCandidate, m_callbackLifetime));
    m_peerConnection->onTrack(makeWeakCallback(this, &WebRtcCtl::onRemoteTrack, m_callbackLifetime));
    m_peerConnection->onDataChannel(makeWeakCallback(this, &WebRtcCtl::onRemoteDataChannel, m_callbackLifetime));
}
