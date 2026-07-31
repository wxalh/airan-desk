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
#include <utility>

#include "desktop_capture/blank_detector_desktop_capturer_wrapper.h"
#include "desktop_capture/desktop_capture_options.h"
#include "desktop_capture/desktop_capturer.h"
#include "desktop_capture/fallback_desktop_capturer_wrapper.h"
#include "desktop_capture/rgba_color.h"
#if defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
#include "desktop_capture/win/dxgi_duplicator_controller.h"
#include "desktop_capture/win/screen_capturer_win_directx.h"
#endif
#include "desktop_capture/win/screen_capturer_win_gdi.h"
#include "rtc_base/logging.h"
#include "common/logger_manager.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

#if defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
namespace {

std::unique_ptr<DesktopCapturer> CreateScreenCapturerWinDirectx(
    const DesktopCaptureOptions& options) {
  LOG_INFO("DesktopCapturer::CreateRawScreenCapturer creates DesktopCapturer of type ScreenCapturerWinDirectx");
  std::unique_ptr<DesktopCapturer> capturer(
      new ScreenCapturerWinDirectx(options));
  capturer.reset(new BlankDetectorDesktopCapturerWrapper(
      std::move(capturer), RgbaColor(0, 0, 0, 0)));
  return capturer;
}

}  // namespace
#endif

// static
std::unique_ptr<DesktopCapturer> DesktopCapturer::CreateRawScreenCapturer(
    const DesktopCaptureOptions& options) {
  // Default capturer if no options are enabled is GDI.
  LOG_INFO("DesktopCapturer::CreateRawScreenCapturer creates DesktopCapturer of type ScreenCapturerWinGdi");
  std::unique_ptr<DesktopCapturer> capturer(new ScreenCapturerWinGdi(options));

#if defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
  // If DirectX is enabled use it as main capturer with GDI as fallback.
  if (options.allow_directx_capturer()) {
    // `dxgi_duplicator_controller` should be alive in this scope to ensure it
    // won't unload DxgiDuplicatorController.
    auto dxgi_duplicator_controller = DxgiDuplicatorController::Instance();
    if (ScreenCapturerWinDirectx::IsSupported()) {
      capturer.reset(new FallbackDesktopCapturerWrapper(
          CreateScreenCapturerWinDirectx(options), std::move(capturer)));
      return capturer;
    }
  }
#endif

  // Use GDI as default capturer without any fallback solution.
  return capturer;
}

}  // namespace airan::desktop_capture
