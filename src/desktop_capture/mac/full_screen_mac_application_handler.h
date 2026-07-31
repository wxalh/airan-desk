/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_MAC_FULL_SCREEN_MAC_APPLICATION_HANDLER_H_
#define AIRAN_DESKTOP_CAPTURE_MAC_FULL_SCREEN_MAC_APPLICATION_HANDLER_H_

#include <memory>

#include "desktop_capture/desktop_capturer.h"
#include "desktop_capture/full_screen_application_handler.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

std::unique_ptr<FullScreenApplicationHandler>
CreateFullScreenMacApplicationHandler(DesktopCapturer::SourceId sourceId);

}  // namespace airan::desktop_capture

#endif  // AIRAN_DESKTOP_CAPTURE_MAC_FULL_SCREEN_MAC_APPLICATION_HANDLER_H_
