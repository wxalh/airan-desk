#ifndef AIRAN_RTC_PEER_CONNECTION_ASYNC_HELPERS_H
#define AIRAN_RTC_PEER_CONNECTION_ASYNC_HELPERS_H

#include "rtc/core/rtc.hpp"

#include <api/peer_connection_interface.h>
#include <api/stats/rtc_stats_collector_callback.h>

#include <functional>
#include <string>

namespace rtc
{


scoped_refptr<webrtc::CreateSessionDescriptionObserver>
createLocalDescriptionObserver(scoped_refptr<webrtc::PeerConnectionInterface> pc,
                               std::function<void(Description)> localDescription,
                               bool acceptRemoteVideoSimulcast = false);


scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface>
createSetRemoteDescriptionObserver(std::function<void()> success,
                                   std::function<void(std::string)> failure);


scoped_refptr<webrtc::RTCStatsCollectorCallback>
createMediaStatsCollector(std::function<void(MediaStats)> cb);

scoped_refptr<webrtc::RTCStatsCollectorCallback>
createSelectedCandidatePairCollector(std::function<void(bool, SelectedCandidatePair)> cb);

} // namespace rtc

#endif /* AIRAN_RTC_PEER_CONNECTION_ASYNC_HELPERS_H */
