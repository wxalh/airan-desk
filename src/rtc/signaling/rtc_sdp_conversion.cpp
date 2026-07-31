#include "rtc/core/rtc_internal.h"

#include <cstring>
#include <stdexcept>

namespace rtc
{


int parseMLineIndex(const std::string &mid)
{
    try
    {
        return mid.empty() ? 0 : std::stoi(mid);
    }
    catch (...)
    {
        return 0;
    }
}


binary bytesToBinary(const uint8_t *data, size_t size)
{
    binary out(size);
    if (size > 0)
        std::memcpy(out.data(), data, size);
    return out;
}


webrtc::RtpTransceiverDirection toNativeDirection(Description::Direction direction)
{
    switch (direction)
    {
    case Description::Direction::SendOnly:
        return webrtc::RtpTransceiverDirection::kSendOnly;
    case Description::Direction::RecvOnly:
        return webrtc::RtpTransceiverDirection::kRecvOnly;
    case Description::Direction::SendRecv:
    default:
        return webrtc::RtpTransceiverDirection::kSendRecv;
    }
}


webrtc::SdpType toNativeSdpType(Description::Type type)
{
    return type == Description::Type::Answer ? webrtc::SdpType::kAnswer : webrtc::SdpType::kOffer;
}

} // namespace rtc
