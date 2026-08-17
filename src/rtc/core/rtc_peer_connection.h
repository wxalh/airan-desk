#pragma once

#include "rtc/core/rtc_data_channel.h"
#include "rtc/core/rtc_track.h"

#include <api/jsep.h>
#include <api/media_stream_interface.h>
#include <api/peer_connection_interface.h>
#include <api/rtp_receiver_interface.h>
#include <api/rtp_transceiver_interface.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace webrtc
{
class AudioDeviceModule;
}

namespace rtc
{


class PeerConnection final : public std::enable_shared_from_this<PeerConnection>, private webrtc::PeerConnectionObserver
{
public:
    enum class State
    {
        New,
        Connecting,
        Connected,
        Disconnected,
        Failed,
        Closed
    };
    enum class IceState
    {
        New,
        Checking,
        Connected,
        Completed,
        Failed,
        Disconnected,
        Closed
    };
    enum class GatheringState
    {
        New,
        InProgress,
        Complete
    };

    explicit PeerConnection(const Configuration &config);
    ~PeerConnection() override;

    std::shared_ptr<Track> addTrack(const Description::Video &desc);
    std::shared_ptr<Track> addTrack(const Description::Audio &desc);
    void setAudioRecordingMode(bool systemLoopback);
    std::shared_ptr<DataChannel> createDataChannel(const std::string &label);
    std::shared_ptr<DataChannel> createDataChannel(const std::string &label, std::initializer_list<Reliability> reliability);
    void setLocalDescription(Description::Type type);
    void setRemoteDescription(const Description &description,
                              std::function<void()> onSuccess = nullptr,
                              std::function<void(std::string)> onFailure = nullptr);
    void setRemoteVideoCodecCapabilities(std::vector<VideoCodecCapability> capabilities);
    void addRemoteCandidate(const Candidate &candidate);
    void createOffer() { setLocalDescription(Description::Type::Offer); }
    void createAnswer();
    void close();
    void resetCallbacks();
    void querySelectedCandidatePair(std::function<void(bool, SelectedCandidatePair)> cb);
    void queryMediaStats(std::function<void(MediaStats)> cb);

    void onStateChange(std::function<void(State)> cb);
    void onIceStateChange(std::function<void(IceState)> cb);
    void onGatheringStateChange(std::function<void(GatheringState)> cb);
    void onLocalDescription(std::function<void(Description)> cb);
    void onLocalCandidate(std::function<void(Candidate)> cb);
    void onTrack(std::function<void(std::shared_ptr<Track>)> cb);
    void onDataChannel(std::function<void(std::shared_ptr<DataChannel>)> cb);

private:
    static webrtc::PeerConnectionInterface::RTCConfiguration toNativeConfiguration(const Configuration &config);
    void cleanupConstructionFailure();
    std::shared_ptr<Track> wrapRemoteTrack(scoped_refptr<webrtc::MediaStreamTrackInterface> track, const std::string &mid);
    void reapplyVideoCodecPreferences();
    bool remoteAcceptsVideoSimulcast() const;
    void configureAudioRecordingDevice(bool systemLoopback);
    void pruneClosedDataChannels();

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState state) override;
    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState state) override;
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState state) override;
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState state) override;
    void OnIceCandidate(const NativeIceCandidate *candidate) override;
    void OnDataChannel(scoped_refptr<webrtc::DataChannelInterface> data_channel) override;
    void OnRenegotiationNeeded() override;
    void OnTrack(scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
    void OnAddTrack(scoped_refptr<webrtc::RtpReceiverInterface> receiver,
                    const std::vector<scoped_refptr<webrtc::MediaStreamInterface>> &streams) override;
    void OnRemoveTrack(scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;

    std::unique_ptr<Thread> m_networkThread;
    std::unique_ptr<Thread> m_workerThread;
    std::unique_ptr<Thread> m_signalingThread;
    bool m_networkThreadStarted{false};
    bool m_workerThreadStarted{false};
    bool m_signalingThreadStarted{false};
    scoped_refptr<webrtc::AudioDeviceModule> m_audioDeviceModule;
    scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_factory;
    scoped_refptr<webrtc::PeerConnectionInterface> m_pc;
    std::vector<std::shared_ptr<Track>> m_tracks;
    std::vector<std::shared_ptr<DataChannel>> m_channels;
    std::unordered_map<std::string, std::weak_ptr<Track>> m_remoteTracksById;
    std::unordered_set<std::string> m_notifiedRemoteTrackIds;
    std::vector<VideoCodecCapability> m_remoteVideoCodecCapabilities;
    MediaTopology m_mediaTopology{MediaTopology::PeerToPeer};
    bool m_acceptRemoteVideoSimulcast{false};
    std::atomic_bool m_closed{false};
    bool m_instanceCounted{false};
    mutable std::mutex m_callbackMutex;
    std::function<void(State)> m_onStateChange;
    std::function<void(IceState)> m_onIceStateChange;
    std::function<void(GatheringState)> m_onGatheringStateChange;
    std::function<void(Description)> m_onLocalDescription;
    std::function<void(Candidate)> m_onLocalCandidate;
    std::function<void(std::shared_ptr<Track>)> m_onTrack;
    std::function<void(std::shared_ptr<DataChannel>)> m_onDataChannel;
};

void Cleanup();

} // namespace rtc
