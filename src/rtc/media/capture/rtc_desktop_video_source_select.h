#ifndef AIRAN_RTC_DESKTOP_VIDEO_SOURCE_SELECT_H
#define AIRAN_RTC_DESKTOP_VIDEO_SOURCE_SELECT_H

#include "rtc/core/rtc_internal.h"

namespace rtc
{

/*
 * Creates the business desktop video source.
 *
 * WebRTC DesktopCapturer is intentionally not used as a business fallback.
 * During migration it may remain compiled for baseline tests, but this selector
 * only returns Airan/native capture sources and returns null when none is available.
 */
scoped_refptr<DesktopVideoSource> createDesktopVideoSourceForTrack(const Description::Video &desc);

} // namespace rtc

#endif /* AIRAN_RTC_DESKTOP_VIDEO_SOURCE_SELECT_H */
