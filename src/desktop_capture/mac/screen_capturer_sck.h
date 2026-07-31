/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_MAC_SCREEN_CAPTURER_SCK_H_
#define AIRAN_DESKTOP_CAPTURE_MAC_SCREEN_CAPTURER_SCK_H_

#include <memory>

#include "desktop_capture/desktop_capture_options.h"
#include "desktop_capture/desktop_capturer.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

// Returns true if the ScreenCaptureKit capturer is available.
bool ScreenCapturerSckAvailable();

// Returns true if the ScreenCaptureKit capturer is available using
// SCContentSharingPicker for picking a generic source.
bool GenericCapturerSckWithPickerAvailable();

// A DesktopCapturer implementation that uses ScreenCaptureKit.
std::unique_ptr<DesktopCapturer> CreateScreenCapturerSck(
    const DesktopCaptureOptions& options);

std::unique_ptr<DesktopCapturer> CreateGenericCapturerSck(
    const DesktopCaptureOptions& options);

}  // namespace airan::desktop_capture

#endif  // AIRAN_DESKTOP_CAPTURE_MAC_SCREEN_CAPTURER_SCK_H_
