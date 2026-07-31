#include "rtc/core/rtc_internal.h"

#include "common/logger_manager.h"
#include "rtc/peer/factory/rtc_peer_connection_factories.h"

#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/create_peerconnection_factory.h>
#include <rtc_base/ssl_adapter.h>

#include <stdexcept>

namespace rtc
{


PeerConnection::PeerConnection(const Configuration &config)
    : m_mediaTopology(config.mediaTopology),
      m_acceptRemoteVideoSimulcast(config.mediaTopology == MediaTopology::Sfu)
{
    ensureInitialized();
    g_instanceCount.fetch_add(1, std::memory_order_relaxed);
    LOG_INFO("Creating Google WebRTC PeerConnection: iceServers={}, policy={}, iceTcp={}, mediaTopology={}, acceptRemoteSimulcast={}, audioDeviceModule={}",
             config.iceServers.size(),
             static_cast<int>(config.iceTransportPolicy),
             config.enableIceTcp,
             m_mediaTopology == MediaTopology::Sfu ? "sfu" : "p2p",
             m_acceptRemoteVideoSimulcast,
             config.enableAudioDeviceModule);

    m_networkThread = Thread::CreateWithSocketServer();
    m_workerThread = Thread::Create();
    m_signalingThread = Thread::Create();
    if (!m_networkThread || !m_workerThread || !m_signalingThread)
        throw std::runtime_error("failed to create WebRTC threads");
    m_networkThread->SetName("airan-webrtc-network", nullptr);
    m_workerThread->SetName("airan-webrtc-worker", nullptr);
    m_signalingThread->SetName("airan-webrtc-signaling", nullptr);
    m_networkThread->Start();
    m_workerThread->Start();
    m_signalingThread->Start();

    m_workerThread->BlockingCall([&]() {
        m_audioDeviceModule = createAudioDeviceModule(config.enableAudioDeviceModule);
    });

    m_factory = webrtc::CreatePeerConnectionFactory(
        m_networkThread.get(),
        m_workerThread.get(),
        m_signalingThread.get(),
        m_audioDeviceModule,
        webrtc::CreateBuiltinAudioEncoderFactory(),
        webrtc::CreateBuiltinAudioDecoderFactory(),
        createAiranVideoEncoderFactory(),
        createAiranVideoDecoderFactory(),
        nullptr,
        nullptr);
    if (!m_factory)
        throw std::runtime_error("failed to create Google WebRTC peer connection factory");

    webrtc::PeerConnectionDependencies deps(this);
    auto pcOrError = m_factory->CreatePeerConnectionOrError(toNativeConfiguration(config), std::move(deps));
    if (!pcOrError.ok())
        throw std::runtime_error("failed to create Google WebRTC peer connection: " + std::string(pcOrError.error().message()));
    m_pc = pcOrError.MoveValue();
    if (!m_pc)
        throw std::runtime_error("failed to create Google WebRTC peer connection");
    LOG_INFO("Google WebRTC PeerConnection created");
}


PeerConnection::~PeerConnection()
{
    close();
    m_factory = nullptr;
    if (m_audioDeviceModule && m_workerThread && !m_workerThread->IsQuitting())
    {
        m_workerThread->BlockingCall([this]() {
            m_audioDeviceModule = nullptr;
        });
    }
    else
    {
        m_audioDeviceModule = nullptr;
    }
    if (m_signalingThread)
        m_signalingThread->Stop();
    if (m_workerThread)
        m_workerThread->Stop();
    if (m_networkThread)
        m_networkThread->Stop();
    g_instanceCount.fetch_sub(1, std::memory_order_relaxed);
}


void Cleanup()
{
    if (g_instanceCount.load(std::memory_order_relaxed) <= 0)
        CleanupSSL();
}

} // namespace rtc
