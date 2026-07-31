/*
 *  Copyright (c) 2023 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_MAC_DESKTOP_FRAME_UTILS_H_
#define AIRAN_DESKTOP_CAPTURE_MAC_DESKTOP_FRAME_UTILS_H_

#include <memory>

#include "desktop_capture/desktop_frame.h"
#include "desktop_capture/mac/desktop_frame_cgimage.h"
#include "rtc_base/system/rtc_export.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

std::unique_ptr<DesktopFrame> RTC_EXPORT
CreateDesktopFrameFromCGImage(ScopedCFTypeRef<CGImageRef> cg_image);

}  // namespace airan::desktop_capture

#endif  // AIRAN_DESKTOP_CAPTURE_MAC_DESKTOP_FRAME_UTILS_H_
