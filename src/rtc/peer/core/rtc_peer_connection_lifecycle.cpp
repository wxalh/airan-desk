#include "rtc/core/rtc_internal.h"
#include "rtc/peer/async/rtc_peer_connection_async_helpers.h"
#include "common/logger_manager.h"

#include <functional>
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace rtc
{


void PeerConnection::close()
{
    const auto closeStarted = std::chrono::steady_clock::now();
    if (m_signalingThread && !m_signalingThread->IsQuitting() && !m_signalingThread->IsCurrent())
    {
        m_signalingThread->BlockingCall([this]() { close(); });
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - closeStarted)
                                   .count();
        if (elapsedMs >= 100)
            LOG_WARN("PeerConnection close signaling handoff took {} ms", elapsedMs);
        return;
    }
    if (m_closed.exchange(true))
        return;
    resetCallbacks();
    for (auto &track : m_tracks)
        if (track)
            track->close();
    m_tracks.clear();
    m_remoteTracksById.clear();
    m_notifiedRemoteTrackIds.clear();
    for (auto &channel : m_channels)
        if (channel)
            channel->close();
    m_channels.clear();
    if (m_pc)
        m_pc->Close();
    m_pc = nullptr;
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - closeStarted)
                               .count();
    if (elapsedMs >= 100)
        LOG_WARN("PeerConnection close took {} ms", elapsedMs);
    else
        LOG_DEBUG("PeerConnection close completed in {} ms", elapsedMs);
}


void PeerConnection::queryMediaStats(std::function<void(MediaStats)> cb)
{
    if (!m_pc || m_closed.load())
        return;
    auto collector = createMediaStatsCollector(std::move(cb));
    m_pc->GetStats(collector.get());
}


void PeerConnection::querySelectedCandidatePair(std::function<void(bool, SelectedCandidatePair)> cb)
{
    if (!m_pc || m_closed.load())
    {
        if (cb)
            cb(false, SelectedCandidatePair());
        return;
    }
    auto collector = createSelectedCandidatePairCollector(std::move(cb));
    m_pc->GetStats(collector.get());
}


void PeerConnection::resetCallbacks()
{
    std::lock_guard<std::mutex> locker(m_callbackMutex);
    m_onStateChange = nullptr;
    m_onIceStateChange = nullptr;
    m_onGatheringStateChange = nullptr;
    m_onLocalDescription = nullptr;
    m_onLocalCandidate = nullptr;
    m_onTrack = nullptr;
    m_onDataChannel = nullptr;
}

void PeerConnection::pruneClosedDataChannels()
{
    m_channels.erase(std::remove_if(m_channels.begin(), m_channels.end(),
                                    [](const std::shared_ptr<DataChannel> &channel) {
                                        return !channel || channel->isClosed();
                                    }),
                     m_channels.end());
}

void PeerConnection::onStateChange(std::function<void(State)> cb)
{
    std::lock_guard<std::mutex> locker(m_callbackMutex);
    m_onStateChange = std::move(cb);
}

void PeerConnection::onIceStateChange(std::function<void(IceState)> cb)
{
    std::lock_guard<std::mutex> locker(m_callbackMutex);
    m_onIceStateChange = std::move(cb);
}

void PeerConnection::onGatheringStateChange(std::function<void(GatheringState)> cb)
{
    std::lock_guard<std::mutex> locker(m_callbackMutex);
    m_onGatheringStateChange = std::move(cb);
}

void PeerConnection::onLocalDescription(std::function<void(Description)> cb)
{
    std::lock_guard<std::mutex> locker(m_callbackMutex);
    m_onLocalDescription = std::move(cb);
}

void PeerConnection::onLocalCandidate(std::function<void(Candidate)> cb)
{
    std::lock_guard<std::mutex> locker(m_callbackMutex);
    m_onLocalCandidate = std::move(cb);
}

void PeerConnection::onTrack(std::function<void(std::shared_ptr<Track>)> cb)
{
    std::lock_guard<std::mutex> locker(m_callbackMutex);
    m_onTrack = std::move(cb);
}

void PeerConnection::onDataChannel(std::function<void(std::shared_ptr<DataChannel>)> cb)
{
    std::lock_guard<std::mutex> locker(m_callbackMutex);
    m_onDataChannel = std::move(cb);
}


std::shared_ptr<Track> PeerConnection::wrapRemoteTrack(scoped_refptr<webrtc::MediaStreamTrackInterface> track, const std::string &mid)
{
    if (!track)
        return nullptr;
    const std::string key = track->id().empty() ? mid : track->id();
    if (auto existing = m_remoteTracksById[key].lock())
    {
        LOG_DEBUG("Remote track already wrapped: key={}, mid={}", key, mid);
        return existing;
    }
    auto wrapped = std::make_shared<Track>(track, mid);
    m_remoteTracksById[key] = wrapped;
    m_tracks.push_back(wrapped);
    return wrapped;
}

} // namespace rtc
