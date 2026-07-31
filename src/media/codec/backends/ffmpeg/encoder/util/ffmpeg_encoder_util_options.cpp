#include "media/codec/backends/ffmpeg/encoder/util/ffmpeg_encoder_util.h"

#if defined(AIRAN_HAVE_FFMPEG)

#include <algorithm>
#include <cstring>

namespace airan::media::ffmpeg
{
namespace
{
int h264Macroblocks(int pixels)
{
    return (pixels + 15) / 16;
}

int h264LevelForContext(const AVCodecContext *ctx)
{
    if (!ctx || ctx->width <= 0 || ctx->height <= 0)
        return 40;

    const int fps = ctx->framerate.num > 0 && ctx->framerate.den > 0
                        ? (ctx->framerate.num + ctx->framerate.den - 1) / ctx->framerate.den
                        : 30;
    const int mbWidth = h264Macroblocks(ctx->width);
    const int mbHeight = h264Macroblocks(ctx->height);
    const int frameMb = mbWidth * mbHeight;
    const int mbps = frameMb * (std::max)(1, fps);

    struct Limit
    {
        int level;
        int maxFrameMb;
        int maxMbps;
    };
    constexpr Limit kLimits[] = {
        {31, 3600, 108000},
        {40, 8192, 245760},
        {42, 8704, 522240},
        {51, 36864, 983040},
        {52, 36864, 2073600},
    };
    for (const auto &limit : kLimits)
        if (frameMb <= limit.maxFrameMb && mbps <= limit.maxMbps)
            return limit.level;
    return 52;
}

bool backendContains(const CodecProbe *probe, const char *token)
{
    return probe && probe->backend && token && std::strstr(probe->backend, token) != nullptr;
}
} // namespace

void configureH264CodecContext(AVCodecContext *ctx)
{
    if (!ctx || ctx->codec_id != AV_CODEC_ID_H264)
        return;
    ctx->profile = AV_PROFILE_H264_CONSTRAINED_BASELINE;
    ctx->level = h264LevelForContext(ctx);
}


void configureRealtimeEncoderOptions(AVCodecContext *ctx, const CodecProbe *probe)
{
    if (!ctx || !ctx->priv_data || !probe)
        return;

    if ((probe->codec == CodecKind::VP8 || probe->codec == CodecKind::VP9) &&
        probe->deviceType == AV_HWDEVICE_TYPE_NONE &&
        probe->hardwarePixelFormat == AV_PIX_FMT_NONE)
    {
        av_opt_set(ctx->priv_data, "deadline", "realtime", 0);
        av_opt_set(ctx->priv_data, "cpu-used", "8", 0);
        av_opt_set(ctx->priv_data, "lag-in-frames", "0", 0);
        av_opt_set(ctx->priv_data, "row-mt", "1", 0);
        av_opt_set(ctx->priv_data, "error-resilient", "1", 0);
        return;
    }

    if (probe->deviceType == AV_HWDEVICE_TYPE_QSV)
    {
        av_opt_set(ctx->priv_data, "async_depth", "1", 0);
        av_opt_set(ctx->priv_data, "preset", "slow", 0);
        av_opt_set(ctx->priv_data, "look_ahead", "0", 0);
        av_opt_set(ctx->priv_data, "bf", "0", 0);
        av_opt_set(ctx->priv_data, "profile", "baseline", 0);
        av_opt_set(ctx->priv_data, "repeat_pps", "1", 0);
        av_opt_set(ctx->priv_data, "aud", "1", 0);
        av_opt_set(ctx->priv_data, "low_delay_brc", "0", 0);
        av_opt_set(ctx->priv_data, "mbbrc", "1", 0);
        av_opt_set(ctx->priv_data, "extbrc", "1", 0);
        av_opt_set(ctx->priv_data, "rdo", "1", 0);
        av_opt_set(ctx->priv_data, "max_qp_i", "34", 0);
        av_opt_set(ctx->priv_data, "max_qp_p", "38", 0);
        av_opt_set(ctx->priv_data, "skip_frame", "no_skip", 0);
        av_opt_set(ctx->priv_data, "scenario", "displayremoting", 0);
        return;
    }

    if (probe->deviceType == AV_HWDEVICE_TYPE_VAAPI)
    {
        av_opt_set(ctx->priv_data, "async_depth", "1", 0);
        av_opt_set(ctx->priv_data, "low_power", "0", 0);
        av_opt_set(ctx->priv_data, "rc_mode", "CBR", 0);
        av_opt_set(ctx->priv_data, "blbrc", "1", 0);
        av_opt_set(ctx->priv_data, "quality", "1", 0);
        av_opt_set(ctx->priv_data, "bf", "0", 0);
        if (probe->codec == CodecKind::H264)
        {
            av_opt_set(ctx->priv_data, "profile", "constrained_baseline", 0);
            av_opt_set(ctx->priv_data, "aud", "1", 0);
        }
        return;
    }

    if (backendContains(probe, "_amf"))
    {
        av_opt_set(ctx->priv_data, "usage", "lowlatency_high_quality", 0);
        av_opt_set(ctx->priv_data, "quality", "quality", 0);
        av_opt_set(ctx->priv_data, "preset", "quality", 0);
        av_opt_set(ctx->priv_data, "rc", "hqcbr", 0);
        av_opt_set(ctx->priv_data, "enforce_hrd", "1", 0);
        av_opt_set(ctx->priv_data, "filler_data", "1", 0);
        av_opt_set(ctx->priv_data, "vbaq", "1", 0);
        av_opt_set(ctx->priv_data, "async_depth", "1", 0);
        av_opt_set(ctx->priv_data, "bf", "0", 0);
        av_opt_set(ctx->priv_data, "bf_ref", "0", 0);
        av_opt_set(ctx->priv_data, "high_motion_quality_boost_enable", "1", 0);
        av_opt_set(ctx->priv_data, "forced_idr", "1", 0);
        if (probe->codec == CodecKind::H264)
        {
            av_opt_set(ctx->priv_data, "profile", "constrained_baseline", 0);
            av_opt_set(ctx->priv_data, "aud", "1", 0);
        }
        return;
    }

    if (probe->deviceType == AV_HWDEVICE_TYPE_D3D12VA)
    {
        av_opt_set(ctx->priv_data, "async_depth", "1", 0);
        av_opt_set(ctx->priv_data, "rc_mode", "CBR", 0);
        if (probe->codec == CodecKind::H264)
            av_opt_set(ctx->priv_data, "profile", "constrained_baseline", 0);
        return;
    }

    if (probe->deviceType == AV_HWDEVICE_TYPE_VULKAN)
    {
        av_opt_set(ctx->priv_data, "async_depth", "1", 0);
        av_opt_set(ctx->priv_data, "rc_mode", "cbr", 0);
        av_opt_set(ctx->priv_data, "tune", "hq", 0);
        av_opt_set(ctx->priv_data, "quality", "1", 0);
        if (probe->codec == CodecKind::H264)
            av_opt_set(ctx->priv_data, "profile", "constrained_baseline", 0);
        return;
    }

    if (backendContains(probe, "_mf"))
    {
        av_opt_set(ctx->priv_data, "rate_control", "cbr", 0);
        av_opt_set(ctx->priv_data, "scenario", "displayremoting", 0);
        av_opt_set(ctx->priv_data, "quality", "100", 0);
        return;
    }

    if (backendContains(probe, "_v4l2m2m") || backendContains(probe, "_rkmpp"))
        return;

    if (backendContains(probe, "libopenh264"))
    {
        av_opt_set(ctx->priv_data, "profile", "constrained_baseline", 0);
        av_opt_set(ctx->priv_data, "rc_mode", "bitrate", 0);
        av_opt_set(ctx->priv_data, "allow_skip_frames", "0", 0);
        av_opt_set(ctx->priv_data, "loopfilter", "1", 0);
        av_opt_set(ctx->priv_data, "coder", "cavlc", 0);
        return;
    }

    if (backendContains(probe, "_nvenc"))
    {
        av_opt_set(ctx->priv_data, "preset", "p4", 0);
        av_opt_set(ctx->priv_data, "tune", "ull", 0);
        av_opt_set(ctx->priv_data, "rc", "cbr_ld_hq", 0);
        av_opt_set(ctx->priv_data, "cbr", "1", 0);
        av_opt_set(ctx->priv_data, "zerolatency", "1", 0);
        av_opt_set(ctx->priv_data, "strict_gop", "1", 0);
        av_opt_set(ctx->priv_data, "forced_idr", "1", 0);
        av_opt_set(ctx->priv_data, "b_ref_mode", "disabled", 0);
        av_opt_set(ctx->priv_data, "profile", "baseline", 0);
        av_opt_set(ctx->priv_data, "aud", "1", 0);
        av_opt_set(ctx->priv_data, "bf", "0", 0);
        av_opt_set(ctx->priv_data, "delay", "0", 0);
        av_opt_set(ctx->priv_data, "rc-lookahead", "0", 0);
        av_opt_set(ctx->priv_data, "spatial_aq", "1", 0);
        av_opt_set(ctx->priv_data, "temporal_aq", "1", 0);
        av_opt_set(ctx->priv_data, "aq-strength", "8", 0);
        return;
    }

    av_opt_set(ctx->priv_data, "preset", "p1", 0);
    av_opt_set(ctx->priv_data, "tune", "ull", 0);
    av_opt_set(ctx->priv_data, "zerolatency", "1", 0);
    av_opt_set(ctx->priv_data, "profile", "baseline", 0);
    av_opt_set(ctx->priv_data, "forced-idr", "1", 0);
    av_opt_set(ctx->priv_data, "repeat-headers", "1", 0);
    av_opt_set(ctx->priv_data, "aud", "1", 0);
    av_opt_set(ctx->priv_data, "annexb", "1", 0);
    av_opt_set(ctx->priv_data, "bf", "0", 0);
}
} /* namespace airan::media::ffmpeg */
#endif
