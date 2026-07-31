#pragma once

#include "api/scoped_refptr.h"
#include "media/base/video_common.h"

// GCC accepts the `no_sanitize` attribute but does not support Clang's
// "cfi-icall" sanitizer name, so WebRTC-derived annotations become noisy
// ignored-attribute warnings. Keep the annotation only for Clang builds.
#if defined(__GNUC__) && !defined(__clang__)
#ifdef RTC_NO_SANITIZE
#undef RTC_NO_SANITIZE
#endif
#define RTC_NO_SANITIZE(what)
#endif

namespace webrtc {}

/*
 * Airan modification:
 * The desktop_capture fork lives in airan::desktop_capture to avoid ODR and
 * link-symbol collisions with the libwebrtc package. The fork still reuses
 * WebRTC base/API types, so make those names visible inside the fork namespace.
 * m109 keeps some of these types in rtc/cricket while m144 moved them under
 * webrtc.
 */

namespace airan::desktop_capture
{
using namespace ::webrtc;
#if AIRAN_WEBRTC_MILESTONE < 144
using namespace ::rtc;
using ::cricket::FourCC;
using ::cricket::FOURCC_ARGB;
#endif
}
