#include "rtc/core/rtc_internal.h"

#include "common/logger_manager.h"
#include "rtc/peer/factory/rtc_peer_connection_factories.h"

#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/create_peerconnection_factory.h>
#include <rtc_base/ssl_adapter.h>

#include <stdexcept>
#include <thread>

namespace rtc
{

namespace
{
std::atomic<int> g_deferredThreadStops{0};

void stopThreadSafely(std::unique_ptr<Thread> &thread, bool &started)
{
    if (!thread)
        return;
    if (!started)
    {
        thread.reset();
        return;
    }
    started = false;
    if (!thread->IsCurrent())
    {
        thread->Stop();
        return;
    }

    Thread *const currentThread = thread.release();
    g_deferredThreadStops.fetch_add(1, std::memory_order_acq_rel);
    currentThread->Quit();
    std::thread([currentThread]() {
        currentThread->Stop();
        delete currentThread;
        g_deferredThreadStops.fetch_sub(1, std::memory_order_acq_rel);
    }).detach();
}
} // namespace


PeerConnection::PeerConnection(const Configuration &config)
    : m_mediaTopology(config.mediaTopology),
      m_acceptRemoteVideoSimulcast(config.mediaTopology == MediaTopology::Sfu)
{
    try
    {
    ensureInitialized();
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
    if (!m_networkThread->Start())
        throw std::runtime_error("failed to start WebRTC threads");
    m_networkThreadStarted = true;
    if (!m_workerThread->Start())
        throw std::runtime_error("failed to start WebRTC threads");
    m_workerThreadStarted = true;
    if (!m_signalingThread->Start())
        throw std::runtime_error("failed to start WebRTC threads");
    m_signalingThreadStarted = true;

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
    g_instanceCount.fetch_add(1, std::memory_order_relaxed);
    m_instanceCounted = true;
    LOG_INFO("Google WebRTC PeerConnection created");
    }
    catch (...)
    {
        cleanupConstructionFailure();
        throw;
    }
}


PeerConnection::~PeerConnection()
{
    close();
    auto releaseFactory = [this]() {
        m_factory = nullptr;
    };
    if (m_signalingThread && !m_signalingThread->IsQuitting() && !m_signalingThread->IsCurrent())
        m_signalingThread->BlockingCall(releaseFactory);
    else
        releaseFactory();
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
    stopThreadSafely(m_signalingThread, m_signalingThreadStarted);
    stopThreadSafely(m_workerThread, m_workerThreadStarted);
    stopThreadSafely(m_networkThread, m_networkThreadStarted);
    if (m_instanceCounted)
    {
        const int remaining = g_instanceCount.fetch_sub(1, std::memory_order_relaxed) - 1;
        LOG_INFO("Google WebRTC PeerConnection destroyed: remaining={}", remaining);
    }
}


void PeerConnection::cleanupConstructionFailure()
{
    auto releaseWebRtc = [this]() {
        m_pc = nullptr;
        m_factory = nullptr;
    };
    if (m_signalingThread && m_signalingThreadStarted &&
        !m_signalingThread->IsQuitting() && !m_signalingThread->IsCurrent())
        m_signalingThread->BlockingCall(releaseWebRtc);
    else
        releaseWebRtc();

    if (m_audioDeviceModule && m_workerThread && m_workerThreadStarted &&
        !m_workerThread->IsQuitting() && !m_workerThread->IsCurrent())
    {
        m_workerThread->BlockingCall([this]() {
            m_audioDeviceModule = nullptr;
        });
    }
    else
    {
        m_audioDeviceModule = nullptr;
    }

    stopThreadSafely(m_signalingThread, m_signalingThreadStarted);
    stopThreadSafely(m_workerThread, m_workerThreadStarted);
    stopThreadSafely(m_networkThread, m_networkThreadStarted);
}


void Cleanup()
{
    while (g_deferredThreadStops.load(std::memory_order_acquire) > 0)
        std::this_thread::yield();
    if (g_instanceCount.load(std::memory_order_relaxed) <= 0)
        CleanupSSL();
}

} // namespace rtc
