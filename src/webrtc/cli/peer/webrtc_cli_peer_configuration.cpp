#include "webrtc/cli/webrtc_cli.h"


rtc::Configuration WebRtcCli::buildRtcConfiguration() const
{
    rtc::Configuration config;
    config.enableAudioDeviceModule = !m_isOnlyFile && m_audioMode != QStringLiteral("off");
    const QString path = m_networkPath.toLower();
    const QString topology = m_mediaTopology.toLower();
    const bool hasIceServer = !m_host.empty() && m_port != 0;
    const bool hasTurnCreds = hasIceServer && !m_username.empty() && !m_password.empty();
    const bool forceRelay = (path == QStringLiteral("turn_udp") || path == QStringLiteral("turn_tcp"));
    config.mediaTopology = topology == QStringLiteral("sfu") ? rtc::MediaTopology::Sfu
                                                              : rtc::MediaTopology::PeerToPeer;

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
