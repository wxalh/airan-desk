#include "rtc/media/codec/preferences/rtc_codec_preference_ranking_internal.h"

#include "media/codec/airan_video_codec_backend.h"

#include <cstdlib>
#include <thread>

namespace rtc
{
namespace
{

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

int codecNameRank(const std::string &codecName,
                  bool hardware,
                  int targetWidth,
                  int targetHeight,
                  int targetFps)
{
    const std::string codec = lowerAscii(codecName);
    if (hardware)
    {
        if (codec == "vp9")
            return 0;
        if (codec == "h264")
            return 10;
        if (codec == "vp8")
            return 20;
        if (codec == "h265" || codec == "hevc")
            return 30;
        if (codec == "av1")
            return 50;
        return 100;
    }
    if (codec == "vp9")
        return cpuLikelySupportsSoftwareVp9(targetWidth, targetHeight, targetFps) ? 0 : 90;
    if (codec == "vp8")
        return cpuLikelySupportsSoftwareVp9(targetWidth, targetHeight, targetFps) ? 10 : 0;
    if (codec == "h264")
        return cpuLikelySupportsSoftwareVp9(targetWidth, targetHeight, targetFps) ? 20 : 10;
    if (codec == "h265" || codec == "hevc")
        return cpuLikelySupportsSoftwareVp9(targetWidth, targetHeight, targetFps) ? 30 : 20;
    if (codec == "av1")
        return 90;
    return 100;
}


bool isAuxiliaryVideoCodec(const std::string &codecName)
{
    const std::string codec = lowerAscii(codecName);
    return codec == "rtx" ||
           codec == "red" ||
           codec == "ulpfec" ||
           codec == "flexfec-03";
}


int auxiliaryVideoCodecRank(const std::string &codecName)
{
    const std::string codec = lowerAscii(codecName);
    if (codec == "rtx")
        return 0;
    if (codec == "red")
        return 10;
    if (codec == "ulpfec")
        return 20;
    if (codec == "flexfec-03")
        return 30;
    return 100;
}

int h264Macroblocks(int pixels)
{
    return (pixels + 15) / 16;
}

int h264RequiredLevel(int width, int height, int fps)
{
    if (width <= 0 || height <= 0)
        return 0;

    const int normalizedFps = fps > 0 ? fps : 30;
    const int frameMb = h264Macroblocks(width) * h264Macroblocks(height);
    const int mbps = frameMb * normalizedFps;
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

int h264AdvertisedLevel(const webrtc::RtpCodecCapability &codec)
{
    const auto it = codec.parameters.find("profile-level-id");
    if (it == codec.parameters.end() || it->second.size() < 6)
        return 0;

    const std::string levelHex = it->second.substr(it->second.size() - 2);
    char *end = nullptr;
    const long level = std::strtol(levelHex.c_str(), &end, 16);
    if (!end || *end != '\0' || level <= 0)
        return 0;
    return static_cast<int>(level);
}

int h264TargetLevelRank(const webrtc::RtpCodecCapability &codec,
                        int targetWidth,
                        int targetHeight,
                        int targetFps)
{
    const int requiredLevel = h264RequiredLevel(targetWidth, targetHeight, targetFps);
    const int advertisedLevel = h264AdvertisedLevel(codec);
    if (requiredLevel <= 0 || advertisedLevel <= 0)
        return 0;
    if (advertisedLevel < requiredLevel)
        return 100 + (requiredLevel - advertisedLevel);
    return advertisedLevel - requiredLevel;
}


bool codecHasLocalHardware(const std::string &codecName, bool senderCapabilities)
{
    for (const auto &capability : airan::media::localVideoCodecCapabilities())
    {
        if (lowerAscii(capability.codec) != lowerAscii(codecName))
            continue;
        if (senderCapabilities && capability.canEncode && capability.hardware)
            return true;
        if (!senderCapabilities && capability.canDecode && capability.hardware)
            return true;
    }
    return false;
}
} // namespace

bool localVideoCodecHasHardware(const std::string &codecName, bool senderCapabilities)
{
    return codecHasLocalHardware(codecName, senderCapabilities);
}


int hardwareFirstVideoCodecRank(const webrtc::RtpCodecCapability &codec,
                                bool senderCapabilities,
                                int targetWidth,
                                int targetHeight,
                                int targetFps)
{
    std::string codecName = codecNameFromMime(codec.mime_type());
    if (isAuxiliaryVideoCodec(codecName))
        return 1000 + auxiliaryVideoCodecRank(codecName);

    const bool hardware = codecHasLocalHardware(codecName, senderCapabilities);
    int rank = hardware ? 0 : 500;
    rank += codecNameRank(codecName, hardware, targetWidth, targetHeight, targetFps);
    if (codecName == "h264")
    {
        const auto packetization = codec.parameters.find("packetization-mode");
        if (packetization == codec.parameters.end() || packetization->second != "1")
            rank += 5;
        rank += h264TargetLevelRank(codec, targetWidth, targetHeight, targetFps);
    }
    return rank;
}

} // namespace rtc
