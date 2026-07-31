/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>

#include "desktop_capture/desktop_capturer.h"
#include "rtc_base/logging.h"
#include "common/logger_manager.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

// static
std::unique_ptr<DesktopCapturer> DesktopCapturer::CreateRawScreenCapturer(
    const DesktopCaptureOptions& options) {
  LOG_INFO("DesktopCapturer::CreateRawScreenCapturer creates null DesktopCapturer");
  return nullptr;
}

}  // namespace airan::desktop_capture
