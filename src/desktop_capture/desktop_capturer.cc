/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "desktop_capture/desktop_capturer.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#include "desktop_capture/delegated_source_list_controller.h"
#include "desktop_capture/desktop_capture_options.h"
#include "desktop_capture/desktop_capture_types.h"
#include "desktop_capture/desktop_capturer_differ_wrapper.h"
#include "desktop_capture/desktop_geometry.h"
#include "desktop_capture/fallback_desktop_capturer_wrapper.h"
#include "desktop_capture/shared_memory.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/metrics.h"
#include "common/logger_manager.h"

#if defined(WEBRTC_WIN)
#include "desktop_capture/cropping_window_capturer.h"
#endif  // defined(WEBRTC_WIN)

#if defined(RTC_ENABLE_WIN_WGC)
#include "desktop_capture/win/wgc_capturer_win.h"
#include "rtc_base/win/windows_version.h"
#endif  // defined(RTC_ENABLE_WIN_WGC)

#if defined(WEBRTC_USE_PIPEWIRE)
#include "desktop_capture/linux/wayland/base_capturer_pipewire.h"
#endif  // defined(WEBRTC_USE_PIPEWIRE)

#if defined(WEBRTC_MAC) && !defined(WEBRTC_IOS)
#include "desktop_capture/mac/screen_capturer_sck.h"
#endif  // defined(WEBRTC_MAC) && !defined(WEBRTC_IOS)

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

void LogDesktopCapturerFullscreenDetectorUsage() {
  RTC_HISTOGRAM_BOOLEAN("WebRTC.Screenshare.DesktopCapturerFullscreenDetector",
                        true);
}

DesktopCapturer::~DesktopCapturer() = default;

DelegatedSourceListController*
DesktopCapturer::GetDelegatedSourceListController() {
  return nullptr;
}

void DesktopCapturer::SetSharedMemoryFactory(
    std::unique_ptr<SharedMemoryFactory> /* shared_memory_factory */) {}

void DesktopCapturer::SetAiranCaptureCallback(
    airan::media::AiranCaptureCallback* /* callback */) {}

void DesktopCapturer::SetExcludedWindow(WindowId /* window */) {}

bool DesktopCapturer::GetSourceList(SourceList* /* sources */) {
  return true;
}

bool DesktopCapturer::SelectSource(SourceId /* id */) {
  return false;
}

bool DesktopCapturer::FocusOnSelectedSource() {
  return false;
}

bool DesktopCapturer::IsOccluded(const DesktopVector& /* pos */) {
  return false;
}

// static
std::unique_ptr<DesktopCapturer> DesktopCapturer::CreateWindowCapturer(
    const DesktopCaptureOptions& options) {
#if defined(RTC_ENABLE_WIN_WGC)
  if (options.allow_wgc_window_capturer() &&
      IsWgcSupported(CaptureType::kWindow)) {
    LOG_INFO("DesktopCapturer::CreateWindowCapturer creates DesktopCapturer of type WgcCapturerWin");
    return WgcCapturerWin::CreateRawWindowCapturer(options);
  }
#endif  // defined(RTC_ENABLE_WIN_WGC)

#if defined(WEBRTC_WIN)
  if (options.allow_cropping_window_capturer()) {
    LOG_INFO("DesktopCapturer::CreateWindowCapturer creates DesktopCapturer of type CroppingWindowCapturerWin");
    return CroppingWindowCapturer::CreateCapturer(options);
  }
#endif  // defined(WEBRTC_WIN)

  std::unique_ptr<DesktopCapturer> capturer = CreateRawWindowCapturer(options);
  if (capturer && options.detect_updated_region()) {
    LOG_INFO("DesktopCapturer::CreateWindowCapturer creates DesktopCapturer of type DesktopCapturerDifferWrapper over a base capturer");
    capturer.reset(new DesktopCapturerDifferWrapper(std::move(capturer)));
  }

  return capturer;
}

// static
std::unique_ptr<DesktopCapturer> DesktopCapturer::CreateScreenCapturer(
    const DesktopCaptureOptions& options) {
#if defined(RTC_ENABLE_WIN_WGC)
  if (options.allow_wgc_screen_capturer() &&
      IsWgcSupported(CaptureType::kScreen)) {
    LOG_INFO("DesktopCapturer::CreateScreenCapturer creates DesktopCapturer of type WgcCapturerWin");
    std::unique_ptr<DesktopCapturer> wgc_capturer =
        WgcCapturerWin::CreateRawScreenCapturer(options);
    if (wgc_capturer && options.allow_wgc_capturer_fallback()) {
      DesktopCaptureOptions fallback_options(options);
      fallback_options.set_allow_wgc_screen_capturer(false);
      fallback_options.set_allow_wgc_window_capturer(false);
      fallback_options.set_allow_wgc_capturer_fallback(false);
#if defined(WEBRTC_WIN)
      fallback_options.set_allow_directx_capturer(false);
#endif
      LOG_INFO("DesktopCapturer::CreateScreenCapturer creates WGC fallback without DXGI to avoid blocking the WGC primary startup path");
      std::unique_ptr<DesktopCapturer> cpu_capturer =
          CreateRawScreenCapturer(fallback_options);
      if (cpu_capturer) {
        LOG_INFO("DesktopCapturer::CreateScreenCapturer creates FallbackDesktopCapturerWrapper with WGC primary and WebRTC-derived CPU screen fallback");
        return std::make_unique<FallbackDesktopCapturerWrapper>(
            std::move(wgc_capturer), std::move(cpu_capturer));
      }
    }
    return wgc_capturer;
  }
#endif  // defined(RTC_ENABLE_WIN_WGC)

  std::unique_ptr<DesktopCapturer> capturer = CreateRawScreenCapturer(options);
  if (capturer && options.detect_updated_region()) {
    LOG_INFO("DesktopCapturer::CreateScreenCapturer creates DesktopCapturer of type DesktopCapturerDifferWrapper over a base capturer");
    capturer.reset(new DesktopCapturerDifferWrapper(std::move(capturer)));
  }

  return capturer;
}

// static
std::unique_ptr<DesktopCapturer> DesktopCapturer::CreateGenericCapturer(
    [[maybe_unused]] const DesktopCaptureOptions& options) {
  std::unique_ptr<DesktopCapturer> capturer;

#if defined(WEBRTC_USE_PIPEWIRE)
  if (options.allow_pipewire() && DesktopCapturer::IsRunningUnderWayland()) {
    capturer = std::make_unique<BaseCapturerPipeWire>(
        options, CaptureType::kAnyScreenContent);
  }
#elif defined(WEBRTC_MAC) && !defined(WEBRTC_IOS)
  capturer = CreateGenericCapturerSck(options);
#endif

  if (capturer && options.detect_updated_region()) {
    capturer.reset(new DesktopCapturerDifferWrapper(std::move(capturer)));
  }

  return capturer;
}

#if defined(WEBRTC_USE_PIPEWIRE) || defined(WEBRTC_USE_X11)
bool DesktopCapturer::IsRunningUnderWayland() {
  const char* xdg_session_type = getenv("XDG_SESSION_TYPE");
  if (!xdg_session_type || strncmp(xdg_session_type, "wayland", 7) != 0)
    return false;

  if (!(getenv("WAYLAND_DISPLAY")))
    return false;

  return true;
}
#endif  // defined(WEBRTC_USE_PIPEWIRE) || defined(WEBRTC_USE_X11)

}  // namespace airan::desktop_capture
