#pragma once

#include "rtc/core/rtc_base_types.h"

#include <string>
#include <vector>

namespace rtc
{

class MediaHandler
{
public:
    virtual ~MediaHandler() = default;
};

class Track;
class PeerConnection;
class DesktopVideoSource;

struct MediaStats
{
    std::string videoCodec;
    std::string encoderImplementation;
    std::string decoderImplementation;
    double availableOutgoingBitrateBps{-1.0};
    double targetBitrateBps{-1.0};
    double fractionLost{-1.0};
    double rttMs{-1.0};
    std::string qualityLimitationReason;
};

struct IceCandidateInfo
{
    std::string candidateType;
    std::string protocol;
    std::string relayProtocol;
};

struct SelectedCandidatePair
{
    IceCandidateInfo local;
    IceCandidateInfo remote;
};

struct VideoCodecCapability
{
    std::string codec;
    std::string backend;
    bool canEncode{false};
    bool canDecode{false};
    bool hardware{false};
    std::string zeroCopyPath;
    int maxSpatialLayers{1};
    int maxTemporalLayers{1};
    bool simulcast{false};
    bool svc{false};
    std::vector<std::string> scalabilityModes;
    std::string notes;
};

std::string currentDesktopCaptureBackend();

} // namespace rtc
