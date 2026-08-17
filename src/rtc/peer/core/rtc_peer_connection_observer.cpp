#include "rtc/core/rtc_internal.h"
#include "common/logger_manager.h"

#include <memory>
#include <string>
#include <vector>

namespace rtc
{

void PeerConnection::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) {}

void PeerConnection::OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState state)
{
    std::function<void(State)> callback;
    {
        std::lock_guard<std::mutex> locker(m_callbackMutex);
        callback = m_onStateChange;
    }
    if (!callback)
        return;
    switch (state)
    {
    case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:
        callback(State::Connected);
        break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting:
        callback(State::Connecting);
        break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected:
        callback(State::Disconnected);
        break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed:
        callback(State::Failed);
        break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:
        callback(State::Closed);
        break;
    default:
        callback(State::New);
        break;
    }
}

void PeerConnection::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState state)
{
    std::function<void(GatheringState)> callback;
    {
        std::lock_guard<std::mutex> locker(m_callbackMutex);
        callback = m_onGatheringStateChange;
    }
    if (!callback)
        return;
    if (state == webrtc::PeerConnectionInterface::kIceGatheringGathering)
        callback(GatheringState::InProgress);
    else if (state == webrtc::PeerConnectionInterface::kIceGatheringComplete)
        callback(GatheringState::Complete);
    else
        callback(GatheringState::New);
}

void PeerConnection::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState state)
{
    std::function<void(IceState)> callback;
    {
        std::lock_guard<std::mutex> locker(m_callbackMutex);
        callback = m_onIceStateChange;
    }
    if (!callback)
        return;
    switch (state)
    {
    case webrtc::PeerConnectionInterface::kIceConnectionChecking:
        callback(IceState::Checking);
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionConnected:
        callback(IceState::Connected);
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
        callback(IceState::Completed);
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionFailed:
        callback(IceState::Failed);
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
        callback(IceState::Disconnected);
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionClosed:
        callback(IceState::Closed);
        break;
    default:
        callback(IceState::New);
        break;
    }
}

void PeerConnection::OnIceCandidate(const NativeIceCandidate *candidate)
{
    if (!candidate)
        return;
    std::function<void(Candidate)> callback;
    {
        std::lock_guard<std::mutex> locker(m_callbackMutex);
        callback = m_onLocalCandidate;
    }
    if (!callback)
        return;
    std::string text;
#if AIRAN_WEBRTC_MILESTONE >= 144
    text = candidate->ToString();
#else
    candidate->ToString(&text);
#endif
    callback(Candidate(text, candidate->sdp_mid()));
}

void PeerConnection::OnDataChannel(scoped_refptr<webrtc::DataChannelInterface> data_channel)
{
    if (!data_channel)
        return;
    auto channel = std::make_shared<DataChannel>(data_channel);
    pruneClosedDataChannels();
    m_channels.push_back(channel);
    std::function<void(std::shared_ptr<DataChannel>)> callback;
    {
        std::lock_guard<std::mutex> locker(m_callbackMutex);
        callback = m_onDataChannel;
    }
    if (callback)
        callback(channel);
}

void PeerConnection::OnRenegotiationNeeded() {}

void PeerConnection::OnTrack(scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
{
    if (!transceiver || !transceiver->receiver())
        return;
    std::function<void(std::shared_ptr<Track>)> callback;
    {
        std::lock_guard<std::mutex> locker(m_callbackMutex);
        callback = m_onTrack;
    }
    if (!callback)
        return;
    const auto nativeTrack = transceiver->receiver()->track();
    if (!nativeTrack)
        return;
    const std::string key = nativeTrack->id().empty()
                                ? (transceiver->mid() ? *transceiver->mid() : transceiver->receiver()->id())
                                : nativeTrack->id();
    if (!m_notifiedRemoteTrackIds.insert(key).second)
    {
        LOG_DEBUG("Ignoring duplicate remote track callback: key={}", key);
        return;
    }
    auto mid = transceiver->mid();
    auto track = wrapRemoteTrack(nativeTrack, mid ? *mid : transceiver->receiver()->id());
    if (track)
        callback(track);
}

void PeerConnection::OnAddTrack(scoped_refptr<webrtc::RtpReceiverInterface> receiver,
                                const std::vector<scoped_refptr<webrtc::MediaStreamInterface>> &)
{
    if (!receiver)
        return;
    std::function<void(std::shared_ptr<Track>)> callback;
    {
        std::lock_guard<std::mutex> locker(m_callbackMutex);
        callback = m_onTrack;
    }
    if (!callback)
        return;
    const auto nativeTrack = receiver->track();
    if (!nativeTrack)
        return;
    const std::string key = nativeTrack->id().empty() ? receiver->id() : nativeTrack->id();
    if (!m_notifiedRemoteTrackIds.insert(key).second)
    {
        LOG_DEBUG("Ignoring duplicate legacy remote track callback: key={}", key);
        return;
    }
    auto track = wrapRemoteTrack(nativeTrack, receiver->id());
    if (track)
        callback(track);
}

void PeerConnection::OnRemoveTrack(scoped_refptr<webrtc::RtpReceiverInterface>) {}

} // namespace rtc
