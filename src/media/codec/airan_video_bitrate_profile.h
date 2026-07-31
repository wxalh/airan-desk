#pragma once

#include <string>

namespace airan::media
{

enum class DesktopVideoQualityProfile
{
    Balanced,
    LanHd,
    WeakClear,
};

struct DesktopVideoBitrateLimits
{
    int min_bps = 0;
    int start_bps = 0;
    int max_bps = 0;
};

std::string normalizeDesktopVideoQualityProfile(const std::string &profile);
DesktopVideoQualityProfile desktopVideoQualityProfileFromName(const std::string &profile);
const char *toString(DesktopVideoQualityProfile profile);
DesktopVideoBitrateLimits desktopVideoBitrateLimits(int width,
                                                    int height,
                                                    int fps,
                                                    DesktopVideoQualityProfile profile);

} // namespace airan::media
