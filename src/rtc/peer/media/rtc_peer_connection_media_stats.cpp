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
} // namespace


scoped_refptr<webrtc::RTCStatsCollectorCallback>
createMediaStatsCollector(std::function<void(MediaStats)> cb)
{
    return make_ref_counted<MediaStatsCollector>(std::move(cb));
}

} // namespace rtc
