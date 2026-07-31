#include "media/codec/airan_video_bitrate_profile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>

namespace airan::media
{
namespace
{
std::string normalizedToken(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
                    return ch == '_' || ch == '-' || ch == ' ' || ch == '.';
                }),
                value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

int roundToKbps(double bps)
{
    if (!std::isfinite(bps) || bps >= static_cast<double>((std::numeric_limits<int>::max)() - 500))
        return (std::numeric_limits<int>::max)();
    if (bps <= 1000.0)
        return 1000;
    const int64_t rounded = static_cast<int64_t>((bps + 500.0) / 1000.0) * 1000;
    return static_cast<int>((std::max<int64_t>)(1000, rounded));
}

int clampInt(int value, int lower, int upper)
{
    return (std::min)((std::max)(value, lower), upper);
}
} // namespace

std::string normalizeDesktopVideoQualityProfile(const std::string &profile)
{
    const std::string token = normalizedToken(profile);
    if (token == "lanhd" || token == "lan" || token == "local" ||
        token == "hd" || token == "high" || token == "lossless")
    {
        return "lan_hd";
    }
    if (token == "weakclear" || token == "weak" || token == "lowbandwidth" ||
        token == "poor" || token == "clear")
    {
        return "weak_clear";
    }
    return "balanced";
}

DesktopVideoQualityProfile desktopVideoQualityProfileFromName(const std::string &profile)
{
    const std::string normalized = normalizeDesktopVideoQualityProfile(profile);
    if (normalized == "lan_hd")
        return DesktopVideoQualityProfile::LanHd;
    if (normalized == "weak_clear")
        return DesktopVideoQualityProfile::WeakClear;
    return DesktopVideoQualityProfile::Balanced;
}

const char *toString(DesktopVideoQualityProfile profile)
{
    switch (profile)
    {
    case DesktopVideoQualityProfile::LanHd:
        return "lan_hd";
    case DesktopVideoQualityProfile::WeakClear:
        return "weak_clear";
    case DesktopVideoQualityProfile::Balanced:
    default:
        return "balanced";
    }
}

DesktopVideoBitrateLimits desktopVideoBitrateLimits(int width,
                                                    int height,
                                                    int fps,
                                                    DesktopVideoQualityProfile profile)
{
    const int normalizedWidth = (std::max)(2, width);
    const int normalizedHeight = (std::max)(2, height);
    const int normalizedFps = clampInt(fps, 1, 120);
    const int64_t pixels = (std::max<int64_t>)(320LL * 180LL,
                                               static_cast<int64_t>(normalizedWidth) *
                                                   static_cast<int64_t>(normalizedHeight));

    double minBpp = 0.015;
    double startBpp = 0.050;
    double maxBpp = 0.120;
    int minFloor = 300000;
    int startFloor = 1200000;
    int maxFloor = 4500000;
    int maxCeiling = 90000000;

    if (profile == DesktopVideoQualityProfile::LanHd)
    {
        minBpp = 0.070;
        startBpp = 0.180;
        maxBpp = 0.360;
        minFloor = 3000000;
        startFloor = 8000000;
        maxFloor = 18000000;
        maxCeiling = 220000000;
    }
    else if (profile == DesktopVideoQualityProfile::WeakClear)
    {
        minBpp = 0.040;
        startBpp = 0.120;
        maxBpp = 0.240;
        minFloor = 500000;
        startFloor = 1200000;
        maxFloor = 3000000;
        maxCeiling = 25000000;
    }

    const double pixelFrames = static_cast<double>(pixels) * static_cast<double>(normalizedFps);
    int minBps = (std::max)(roundToKbps(pixelFrames * minBpp), minFloor);
    int startBps = (std::max)(roundToKbps(pixelFrames * startBpp), startFloor);
    int maxBps = (std::max)(roundToKbps(pixelFrames * maxBpp), maxFloor);

    maxBps = (std::min)(maxBps, maxCeiling);
    minBps = (std::min)(minBps, maxBps);
    startBps = clampInt(startBps, minBps, maxBps);
    return {minBps, startBps, maxBps};
}

} // namespace airan::media
