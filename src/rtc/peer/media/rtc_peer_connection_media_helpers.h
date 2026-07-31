#ifndef AIRAN_RTC_PEER_CONNECTION_MEDIA_HELPERS_H
#define AIRAN_RTC_PEER_CONNECTION_MEDIA_HELPERS_H

#include "rtc/core/rtc_internal.h"

#include <api/audio_options.h>

namespace rtc::PeerConnectionMedia
{
#if AIRAN_WEBRTC_MILESTONE >= 144
using NativeAudioOptions = webrtc::AudioOptions;


inline webrtc::MediaType nativeVideoMediaType()
{
    return webrtc::MediaType::VIDEO;
}


inline webrtc::MediaType nativeAudioMediaType()
{
    return webrtc::MediaType::AUDIO;
}
#else
using NativeAudioOptions = cricket::AudioOptions;


inline cricket::MediaType nativeVideoMediaType()
{
    return cricket::MEDIA_TYPE_VIDEO;
}


inline cricket::MediaType nativeAudioMediaType()
{
    return cricket::MEDIA_TYPE_AUDIO;
}
#endif
}

#endif /* AIRAN_RTC_PEER_CONNECTION_MEDIA_HELPERS_H */
