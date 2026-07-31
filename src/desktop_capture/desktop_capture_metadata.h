/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_DESKTOP_CAPTURE_METADATA_H_
#define AIRAN_DESKTOP_CAPTURE_DESKTOP_CAPTURE_METADATA_H_

#if defined(WEBRTC_USE_GIO)
#include "desktop_capture/linux/wayland/xdg_session_details.h"
#endif  // defined(WEBRTC_USE_GIO)

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

// Container for the metadata associated with a desktop capturer.
struct DesktopCaptureMetadata {
#if defined(WEBRTC_USE_GIO)
  // Details about the XDG desktop session handle (used by wayland
  // implementation in remoting)
  xdg_portal::SessionDetails session_details;
#endif  // defined(WEBRTC_USE_GIO)
};

}  // namespace airan::desktop_capture

#endif  // AIRAN_DESKTOP_CAPTURE_DESKTOP_CAPTURE_METADATA_H_
