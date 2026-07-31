#include "rtc/signaling/rtc_sdp_video_util.h"

#include "common/logger_manager.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rtc
{
namespace
{
struct SdpVideoCodecInfo
{
    std::string payloadType;
    std::string codec;
    std::string fmtp;
};

struct SdpVideoLayerInfo
{
    std::vector<std::string> rids;
    std::vector<std::string> simulcast;
};

std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
    }
    return lines;
}

std::vector<SdpVideoCodecInfo> parseVideoCodecsFromSdp(const std::string &sdp)
{
    std::vector<SdpVideoCodecInfo> codecs;
    std::vector<std::string> payloadOrder;
    std::unordered_map<std::string, std::string> codecByPayload;
    std::unordered_map<std::string, std::string> fmtpByPayload;
    bool inVideo = false;

    for (const auto &line : splitLines(sdp))
    {
        if (line.rfind("m=", 0) == 0)
        {
            inVideo = line.rfind("m=video ", 0) == 0;
            if (inVideo)
            {
                std::istringstream parts(line);
                std::string token;
                int index = 0;
                while (parts >> token)
                {
                    if (index++ >= 3)
                        payloadOrder.push_back(token);
                }
            }
            continue;
        }

        if (!inVideo)
            continue;

        if (line.rfind("a=rtpmap:", 0) == 0)
        {
            const auto space = line.find(' ');
            if (space == std::string::npos)
                continue;
            const std::string payload = line.substr(9, space - 9);
            std::string codec = line.substr(space + 1);
            const auto slash = codec.find('/');
            if (slash != std::string::npos)
                codec.resize(slash);
            codecByPayload[payload] = codec;
        }
        else if (line.rfind("a=fmtp:", 0) == 0)
        {
            const auto space = line.find(' ');
            if (space == std::string::npos)
                continue;
            fmtpByPayload[line.substr(7, space - 7)] = line.substr(space + 1);
        }
    }

    std::unordered_set<std::string> seen;
    for (const auto &payload : payloadOrder)
    {
        if (!seen.insert(payload).second)
            continue;
        const auto codecIt = codecByPayload.find(payload);
        if (codecIt == codecByPayload.end())
            continue;
        SdpVideoCodecInfo info;
        info.payloadType = payload;
        info.codec = codecIt->second;
        const auto fmtpIt = fmtpByPayload.find(payload);
        if (fmtpIt != fmtpByPayload.end())
            info.fmtp = fmtpIt->second;
        codecs.push_back(std::move(info));
    }
    return codecs;
}

std::string summarizeSdpVideoCodecs(const std::vector<SdpVideoCodecInfo> &codecs)
{
    std::string summary;
    for (const auto &codec : codecs)
    {
        if (!summary.empty())
            summary += ",";
        summary += codec.payloadType + ":" + codec.codec;
        if (!codec.fmtp.empty())
            summary += "[" + codec.fmtp + "]";
    }
    return summary.empty() ? "none" : summary;
}

SdpVideoLayerInfo parseVideoLayersFromSdp(const std::string &sdp)
{
    SdpVideoLayerInfo info;
    bool inVideo = false;
    for (const auto &line : splitLines(sdp))
    {
        if (line.rfind("m=", 0) == 0)
        {
            inVideo = line.rfind("m=video ", 0) == 0;
            continue;
        }
        if (!inVideo)
            continue;
        if (line.rfind("a=rid:", 0) == 0)
            info.rids.push_back(line.substr(6));
        else if (line.rfind("a=simulcast:", 0) == 0)
            info.simulcast.push_back(line.substr(12));
    }
    return info;
}

std::string joinStrings(const std::vector<std::string> &values)
{
    std::string summary;
    for (const auto &value : values)
    {
        if (!summary.empty())
            summary += ",";
        summary += value;
    }
    return summary.empty() ? "none" : summary;
}

} // namespace

void logSdpVideoCodecs(const char *label, const std::string &type, const std::string &sdp)
{
    const auto codecs = parseVideoCodecsFromSdp(sdp);
    const auto layers = parseVideoLayersFromSdp(sdp);
    LOG_INFO("{} SDP video codecs: type={}, codecs={}, rid={}, simulcast={}",
             label,
             type,
             summarizeSdpVideoCodecs(codecs),
             joinStrings(layers.rids),
             joinStrings(layers.simulcast));
}

std::string normalizeVideoCodec(std::string mimeType)
{
    const std::string prefix = "video/";
    std::transform(mimeType.begin(), mimeType.end(), mimeType.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (mimeType.rfind(prefix, 0) == 0)
        mimeType = mimeType.substr(prefix.size());
    std::transform(mimeType.begin(), mimeType.end(), mimeType.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return mimeType;
}

} // namespace rtc
