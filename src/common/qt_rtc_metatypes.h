#ifndef QT_RTC_METATYPES_H
#define QT_RTC_METATYPES_H

#include <QMetaType>
#include <memory>

#include "rtc/core/rtc.hpp"

Q_DECLARE_METATYPE(rtc::PeerConnection::GatheringState);
Q_DECLARE_METATYPE(rtc::PeerConnection::State);
Q_DECLARE_METATYPE(rtc::PeerConnection::IceState);
Q_DECLARE_METATYPE(rtc::message_variant);
Q_DECLARE_METATYPE(rtc::binary);
Q_DECLARE_METATYPE(std::shared_ptr<rtc::binary>);
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
Q_DECLARE_METATYPE(rtc::D3D11VideoFrame);
#endif

#endif /* QT_RTC_METATYPES_H */
