#include "rtc/peer/async/rtc_peer_connection_async_helpers.h"

#include "rtc/signaling/rtc_sdp_video_util.h"

#include <api/stats/rtc_stats_report.h>
#include <api/stats/rtcstats_objects.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace rtc
{
namespace
{
#if AIRAN_WEBRTC_MILESTONE >= 144
using NativeInboundRtpStats = webrtc::RTCInboundRtpStreamStats;
using NativeOutboundRtpStats = webrtc::RTCOutboundRtpStreamStats;
using NativeRemoteInboundRtpStats = webrtc::RTCRemoteInboundRtpStreamStats;
#else
using NativeInboundRtpStats = webrtc::RTCInboundRTPStreamStats;
using NativeOutboundRtpStats = webrtc::RTCOutboundRTPStreamStats;
using NativeRemoteInboundRtpStats = webrtc::RTCRemoteInboundRtpStreamStats;
#endif


template <typename Member>
bool hasStatsValue(const Member &member)
{
#if AIRAN_WEBRTC_MILESTONE >= 144
    return member.has_value();
#else
    return member.is_defined();
#endif
}


template <typename Member>
const auto &statsValue(const Member &member)
{
    return *member;
}


class MediaStatsCollector : public webrtc::RTCStatsCollectorCallback
{
public:
    
    explicit MediaStatsCollector(std::function<void(MediaStats)> cb) : m_cb(std::move(cb)) {}

    
    void OnStatsDelivered(const scoped_refptr<const webrtc::RTCStatsReport> &report) override
    {
        MediaStats result;
        std::unordered_map<std::string, std::string> codecById;
        std::unordered_set<std::string> videoOutboundIds;
        if (report)
        {
            for (const webrtc::RTCStats &stats : *report)
            {
                if (stats.type() != webrtc::RTCCodecStats::kType)
                    continue;
                const auto &codec = stats.cast_to<const webrtc::RTCCodecStats>();
                if (hasStatsValue(codec.mime_type))
                    codecById[std::string(codec.id())] = statsValue(codec.mime_type);
            }

            for (const webrtc::RTCStats &stats : *report)
            {
                if (stats.type() == NativeOutboundRtpStats::kType)
                {
                    const auto &outbound = stats.cast_to<const NativeOutboundRtpStats>();
                    if (hasStatsValue(outbound.kind) && statsValue(outbound.kind) != "video")
                        continue;
                    videoOutboundIds.insert(std::string(outbound.id()));
                    if (hasStatsValue(outbound.codec_id))
                    {
                        const auto it = codecById.find(statsValue(outbound.codec_id));
                        if (it != codecById.end())
                            result.videoCodec = normalizeVideoCodec(it->second);
                    }
                    if (hasStatsValue(outbound.encoder_implementation))
                        result.encoderImplementation = statsValue(outbound.encoder_implementation);
                    if (hasStatsValue(outbound.target_bitrate))
                        result.targetBitrateBps = statsValue(outbound.target_bitrate);
                    if (hasStatsValue(outbound.quality_limitation_reason))
                        result.qualityLimitationReason = statsValue(outbound.quality_limitation_reason);
                }
                else if (stats.type() == NativeInboundRtpStats::kType)
                {
                    const auto &inbound = stats.cast_to<const NativeInboundRtpStats>();
                    if (hasStatsValue(inbound.kind) && statsValue(inbound.kind) != "video")
                        continue;
                    if (hasStatsValue(inbound.codec_id))
                    {
                        const auto it = codecById.find(statsValue(inbound.codec_id));
                        if (it != codecById.end())
                            result.videoCodec = normalizeVideoCodec(it->second);
                    }
                    if (hasStatsValue(inbound.decoder_implementation))
                        result.decoderImplementation = statsValue(inbound.decoder_implementation);
                }
            }

            for (const webrtc::RTCStats &stats : *report)
            {
                if (stats.type() == NativeRemoteInboundRtpStats::kType)
                {
                    const auto &remoteInbound = stats.cast_to<const NativeRemoteInboundRtpStats>();
                    if (hasStatsValue(remoteInbound.local_id) &&
                        !videoOutboundIds.empty() &&
                        videoOutboundIds.find(statsValue(remoteInbound.local_id)) == videoOutboundIds.end())
                    {
                        continue;
                    }
                    if (hasStatsValue(remoteInbound.fraction_lost))
                        result.fractionLost = statsValue(remoteInbound.fraction_lost);
                    if (hasStatsValue(remoteInbound.round_trip_time))
                        result.rttMs = statsValue(remoteInbound.round_trip_time) * 1000.0;
                }
                else if (stats.type() == webrtc::RTCIceCandidatePairStats::kType)
                {
                    const auto &pair = stats.cast_to<const webrtc::RTCIceCandidatePairStats>();
                    const bool selected =
                        (hasStatsValue(pair.nominated) && statsValue(pair.nominated)) ||
                        (hasStatsValue(pair.state) && statsValue(pair.state) == "succeeded");
                    if (!selected)
                        continue;
                    if (hasStatsValue(pair.available_outgoing_bitrate))
                        result.availableOutgoingBitrateBps = statsValue(pair.available_outgoing_bitrate);
                    if (result.rttMs < 0.0 && hasStatsValue(pair.current_round_trip_time))
                        result.rttMs = statsValue(pair.current_round_trip_time) * 1000.0;
                }
            }
        }
        if (m_cb)
            m_cb(result);
    }

private:
    std::function<void(MediaStats)> m_cb;
};


bool readCandidateInfo(const webrtc::RTCStatsReport &report,
                       const std::string &candidateId,
                       bool local,
                       IceCandidateInfo *result)
{
    if (!result || candidateId.empty())
        return false;

    const webrtc::RTCIceCandidateStats *candidate = local
        ? static_cast<const webrtc::RTCIceCandidateStats *>(report.GetAs<webrtc::RTCLocalIceCandidateStats>(candidateId))
        : static_cast<const webrtc::RTCIceCandidateStats *>(report.GetAs<webrtc::RTCRemoteIceCandidateStats>(candidateId));
    if (!candidate)
        return false;

    if (hasStatsValue(candidate->candidate_type))
        result->candidateType = statsValue(candidate->candidate_type);
    if (hasStatsValue(candidate->protocol))
        result->protocol = statsValue(candidate->protocol);
    if (hasStatsValue(candidate->relay_protocol))
        result->relayProtocol = statsValue(candidate->relay_protocol);
    return !result->candidateType.empty();
}


bool readCandidatePair(const webrtc::RTCStatsReport &report,
                       const std::string &pairId,
                       SelectedCandidatePair *result)
{
    if (!result || pairId.empty())
        return false;

    const auto *pair = report.GetAs<webrtc::RTCIceCandidatePairStats>(pairId);
    if (!pair || !hasStatsValue(pair->local_candidate_id) || !hasStatsValue(pair->remote_candidate_id))
        return false;

    SelectedCandidatePair candidatePair;
    if (!readCandidateInfo(report, statsValue(pair->local_candidate_id), true, &candidatePair.local) ||
        !readCandidateInfo(report, statsValue(pair->remote_candidate_id), false, &candidatePair.remote))
    {
        return false;
    }

    *result = std::move(candidatePair);
    return true;
}


class SelectedCandidatePairCollector : public webrtc::RTCStatsCollectorCallback
{
public:
    explicit SelectedCandidatePairCollector(std::function<void(bool, SelectedCandidatePair)> cb)
        : m_cb(std::move(cb))
    {
    }

    void OnStatsDelivered(const scoped_refptr<const webrtc::RTCStatsReport> &report) override
    {
        SelectedCandidatePair result;
        bool found = false;
        if (report)
        {
            for (const webrtc::RTCStats &stats : *report)
            {
                if (stats.type() != webrtc::RTCTransportStats::kType)
                    continue;
                const auto &transport = stats.cast_to<const webrtc::RTCTransportStats>();
                if (hasStatsValue(transport.selected_candidate_pair_id) &&
                    readCandidatePair(*report, statsValue(transport.selected_candidate_pair_id), &result))
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                for (const webrtc::RTCStats &stats : *report)
                {
                    if (stats.type() != webrtc::RTCIceCandidatePairStats::kType)
                        continue;
                    const auto &pair = stats.cast_to<const webrtc::RTCIceCandidatePairStats>();
                    const bool nominated = hasStatsValue(pair.nominated) && statsValue(pair.nominated);
                    const bool succeeded = !hasStatsValue(pair.state) || statsValue(pair.state) == "succeeded";
                    if (nominated && succeeded && readCandidatePair(*report, std::string(pair.id()), &result))
                    {
                        found = true;
                        break;
                    }
                }
            }
        }

        if (m_cb)
            m_cb(found, std::move(result));
    }

private:
    std::function<void(bool, SelectedCandidatePair)> m_cb;
};
} // namespace


scoped_refptr<webrtc::RTCStatsCollectorCallback>
createMediaStatsCollector(std::function<void(MediaStats)> cb)
{
    return make_ref_counted<MediaStatsCollector>(std::move(cb));
}


scoped_refptr<webrtc::RTCStatsCollectorCallback>
createSelectedCandidatePairCollector(std::function<void(bool, SelectedCandidatePair)> cb)
{
    return make_ref_counted<SelectedCandidatePairCollector>(std::move(cb));
}

} // namespace rtc
