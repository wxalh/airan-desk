#include "rtc/peer/media/rtc_peer_connection_media_helpers.h"
#include "rtc/media/capture/rtc_desktop_video_source_select.h"

#include "common/logger_manager.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rtc
{
std::shared_ptr<Track> PeerConnection::addTrack(const Description::Video &desc)
{
    if (m_signalingThread && !m_signalingThread->IsQuitting() && !m_signalingThread->IsCurrent())
    {
        return m_signalingThread->BlockingCall([this, desc]() {
            return addTrack(desc);
        });
    }

    std::vector<std::string> streamIds{desc.name()};

    if (desc.direction() == Description::Direction::RecvOnly)
    {
        LOG_INFO("Adding recv-only video transceiver: name={}", desc.name());
        webrtc::RtpTransceiverInit init;
        init.direction = toNativeDirection(desc.direction());
        init.stream_ids = streamIds;
        auto result = m_pc->AddTransceiver(PeerConnectionMedia::nativeVideoMediaType(), init);
        if (!result.ok())
            throw std::runtime_error("failed to add recv-only video transceiver: " + std::string(result.error().message()));
        applyAiranVideoCodecPreferences(result.value(), m_factory, false, &m_remoteVideoCodecCapabilities);
        auto wrapped = std::make_shared<Track>(true, desc.name(), Description::Direction::RecvOnly);
        m_tracks.push_back(wrapped);
        return wrapped;
    }

    scoped_refptr<DesktopVideoSource> source = createDesktopVideoSourceForTrack(desc);
    if (!source)
    {
        LOG_ERROR("Cannot add desktop video track because no capture source is available: name={}", desc.name());
        return nullptr;
    }

#if AIRAN_WEBRTC_MILESTONE >= 144
    auto track = m_factory->CreateVideoTrack(source, desc.name());
#else
    auto track = m_factory->CreateVideoTrack(desc.name(), source.get());
#endif
    if (!track)
        throw std::runtime_error("failed to create Google WebRTC desktop video track");
    track->set_content_hint(webrtc::VideoTrackInterface::ContentHint::kDetailed);

    webrtc::RtpTransceiverInit init;
    init.direction = toNativeDirection(desc.direction());
    init.stream_ids = streamIds;
    init.send_encodings = createDesktopVideoSendEncodings(desc.desktopFps(),
                                                          desc.desktopTargetWidth(),
                                                          desc.desktopTargetHeight(),
                                                          desc.desktopSimulcastRequested(),
                                                          desc.desktopQualityProfile());
    auto result = m_pc->AddTransceiver(track, init);
    if (!result.ok())
        throw std::runtime_error("failed to add Google WebRTC desktop video transceiver: " + std::string(result.error().message()));
    auto transceiver = result.value();
    applyAiranVideoCodecPreferences(transceiver,
                                    m_factory,
                                    true,
                                    &m_remoteVideoCodecCapabilities,
                                    desc.desktopTargetWidth(),
                                    desc.desktopTargetHeight(),
                                    desc.desktopFps());
    configureDesktopVideoSender(transceiver->sender(),
                                desc.desktopFps(),
                                desc.desktopTargetWidth(),
                                desc.desktopTargetHeight(),
                                desc.desktopSimulcastRequested(),
                                desc.desktopQualityProfile());

    auto wrapped = std::make_shared<Track>(source,
                                           track,
                                           transceiver->sender(),
                                           desc.name(),
                                           desc.desktopFps(),
                                           desc.desktopSimulcastRequested(),
                                           desc.desktopTargetWidth(),
                                           desc.desktopTargetHeight(),
                                           desc.desktopQualityProfile(),
                                           m_signalingThread.get());
    wrapped->bindDesktopSourceCallbacks();
    m_tracks.push_back(wrapped);
    return wrapped;
}
} /* namespace rtc */
