#include "rtc/media/codec/preferences/rtc_codec_preference_ranking_internal.h"

#include <algorithm>
#include <thread>

namespace rtc
{
namespace
{
bool remoteHasCapability(const std::vector<VideoCodecCapability> *remoteCapabilities,
                         const std::string &codecName,
                         bool requireEncode,
                         bool requireHardware)
{
    if (!remoteCapabilities || remoteCapabilities->empty())
        return false;

    for (const auto &capability : *remoteCapabilities)
    {
        if (lowerAscii(capability.codec) != codecName)
            continue;
        if (requireEncode && !capability.canEncode)
            continue;
        if (!requireEncode && !capability.canDecode)
            continue;
        if (requireHardware && !capability.hardware)
            continue;
        return true;
    }
    return false;
}

bool cpuLikelySupportsSoftwareVp9(int targetWidth, int targetHeight, int targetFps)
{
    const unsigned int cores = std::thread::hardware_concurrency();
    if (cores >= 12)
        return true;
    const int width = targetWidth > 0 ? targetWidth : 1920;
    const int height = targetHeight > 0 ? targetHeight : 1080;
    const int fps = targetFps > 0 ? targetFps : 30;
    const int64_t pixelRate = static_cast<int64_t>(width) * static_cast<int64_t>(height) * fps;
    if (cores >= 8 && pixelRate <= 1920LL * 1080LL * 30LL)
        return true;
    if (cores >= 6 && pixelRate <= 1280LL * 720LL * 30LL)
        return true;
    return false;
}
} // namespace


int remoteVideoCodecScore(const webrtc::RtpCodecCapability &codec,
                          bool senderCapabilities,
                          const std::vector<VideoCodecCapability> *remoteCapabilities,
                          int targetWidth,
                          int targetHeight,
                          int targetFps)
{
    if (!remoteCapabilities || remoteCapabilities->empty())
        return 0;

    const std::string codecName = codecNameFromMime(codec.mime_type());
    const bool localHardware = localVideoCodecHasHardware(codecName, senderCapabilities);
    const bool remoteHardware = remoteHasCapability(remoteCapabilities, codecName, !senderCapabilities, true);
    const bool remoteDirection = remoteHasCapability(remoteCapabilities, codecName, !senderCapabilities, false);
    const bool preferHardwarePair = codecName == "h264" || codecName == "vp8" || codecName == "vp9";
    int best = 0;
    for (const auto &capability : *remoteCapabilities)
    {
        if (lowerAscii(capability.codec) != codecName)
            continue;

        const bool directionMatches = senderCapabilities ? capability.canDecode : capability.canEncode;
        if (!directionMatches)
            continue;

        int score = 10;
        if (capability.hardware)
            score += 100;
        if (!capability.zeroCopyPath.empty())
            score += 25;
        if (localHardware && remoteHardware)
            score += codecName == "vp9" ? 1200 : 1000;
        else if (localHardware && remoteDirection)
            score += preferHardwarePair ? 250 : 40;
        else if (codecName == "vp9" && cpuLikelySupportsSoftwareVp9(targetWidth, targetHeight, targetFps))
            score += 180;
        if (codecName == "vp9" && (!localHardware || !remoteHardware) &&
            !cpuLikelySupportsSoftwareVp9(targetWidth, targetHeight, targetFps))
            score -= 200;
        best = (std::max)(best, score);
    }
    return best;
}

} // namespace rtc
