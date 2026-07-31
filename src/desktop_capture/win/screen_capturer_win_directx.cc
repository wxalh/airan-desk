/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "desktop_capture/win/screen_capturer_win_directx.h"

#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "desktop_capture/desktop_capture_metrics_helper.h"
#include "desktop_capture/desktop_capture_options.h"
#include "desktop_capture/desktop_capture_types.h"
#include "desktop_capture/desktop_capturer.h"
#include "desktop_capture/desktop_frame.h"
#include "desktop_capture/screen_capture_frame_queue.h"
#include "desktop_capture/shared_memory.h"
#include "desktop_capture/win/dxgi_duplicator_controller.h"
#include "desktop_capture/win/dxgi_frame.h"
#include "desktop_capture/win/screen_capture_utils.h"
#include "rtc_base/checks.h"
#include "common/logger_manager.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/trace_event.h"
#include "system_wrappers/include/metrics.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

using Microsoft::WRL::ComPtr;

// static
bool ScreenCapturerWinDirectx::IsSupported() {
  // Forwards IsSupported() function call to DxgiDuplicatorController.
  return DxgiDuplicatorController::Instance()->IsSupported();
}

// static
bool ScreenCapturerWinDirectx::RetrieveD3dInfo(D3dInfo* info) {
  // Forwards SupportedFeatureLevels() function call to
  // DxgiDuplicatorController.
  return DxgiDuplicatorController::Instance()->RetrieveD3dInfo(info);
}

// static
bool ScreenCapturerWinDirectx::IsCurrentSessionSupported() {
  return DxgiDuplicatorController::IsCurrentSessionSupported();
}

// static
bool ScreenCapturerWinDirectx::GetScreenListFromDeviceNames(
    const std::vector<std::string>& device_names,
    DesktopCapturer::SourceList* screens) {
  RTC_DCHECK(screens->empty());

  DesktopCapturer::SourceList gdi_screens;
  std::vector<std::string> gdi_names;
  if (!GetScreenList(&gdi_screens, &gdi_names)) {
    return false;
  }

  RTC_DCHECK_EQ(gdi_screens.size(), gdi_names.size());

  ScreenId max_screen_id = -1;
  for (const DesktopCapturer::Source& screen : gdi_screens) {
    max_screen_id = std::max(max_screen_id, screen.id);
  }

  for (const auto& device_name : device_names) {
    const auto it = std::find(gdi_names.begin(), gdi_names.end(), device_name);
    if (it == gdi_names.end()) {
      // devices_names[i] has not been found in gdi_names, so use max_screen_id.
      max_screen_id++;
      screens->push_back({max_screen_id});
    } else {
      screens->push_back({gdi_screens[it - gdi_names.begin()]});
    }
  }

  return true;
}

// static
int ScreenCapturerWinDirectx::GetIndexFromScreenId(
    ScreenId id,
    const std::vector<std::string>& device_names) {
  DesktopCapturer::SourceList screens;
  if (!GetScreenListFromDeviceNames(device_names, &screens)) {
    return -1;
  }

  RTC_DCHECK_EQ(device_names.size(), screens.size());

  for (size_t i = 0; i < screens.size(); i++) {
    if (screens[i].id == id) {
      return static_cast<int>(i);
    }
  }

  return -1;
}

ScreenCapturerWinDirectx::ScreenCapturerWinDirectx()
    : controller_(DxgiDuplicatorController::Instance()) {}

ScreenCapturerWinDirectx::ScreenCapturerWinDirectx(
    const DesktopCaptureOptions& options)
    : ScreenCapturerWinDirectx() {
  options_ = options;
}

ScreenCapturerWinDirectx::~ScreenCapturerWinDirectx() = default;

void ScreenCapturerWinDirectx::Start(Callback* callback) {
  RTC_DCHECK(!callback_);
  RTC_DCHECK(callback);
  RecordCapturerImpl(DesktopCapturerId::kScreenCapturerWinDirectx);

  callback_ = callback;
}

void ScreenCapturerWinDirectx::SetSharedMemoryFactory(
    std::unique_ptr<SharedMemoryFactory> shared_memory_factory) {
  shared_memory_factory_ = std::move(shared_memory_factory);
}

void ScreenCapturerWinDirectx::SetAiranCaptureCallback(
    airan::media::AiranCaptureCallback* callback) {
  airan_capture_callback_ = callback;
}

void ScreenCapturerWinDirectx::CaptureFrame() {
  RTC_DCHECK(callback_);
  TRACE_EVENT0("webrtc", "ScreenCapturerWinDirectx::CaptureFrame");

  int64_t capture_start_time_nanos = TimeNanos();

  if (current_screen_id_ == kInvalidScreenId) {
    if (!SelectSource(kFullDesktopScreenId)) {
      callback_->OnCaptureResult(Result::ERROR_TEMPORARY, nullptr);
      return;
    }
  }

  // Note that the [] operator will create the ScreenCaptureFrameQueue if it
  // doesn't exist, so this is safe.
  ScreenCaptureFrameQueue<DxgiFrame>& frames =
      frame_queue_map_[current_screen_id_];

  frames.MoveToNextFrame();

  if (!frames.current_frame()) {
    frames.ReplaceCurrentFrame(
        std::make_unique<DxgiFrame>(shared_memory_factory_.get()));
  }

  DxgiDuplicatorController::Result result;
  airan::media::AiranCaptureCallback* airan_capture_callback =
      options_.allow_dxgi_native_gpu_frames() ? airan_capture_callback_ : nullptr;
  if (airan_capture_callback_ && !airan_capture_callback &&
      !dxgi_native_gpu_disabled_logged_) {
    LOG_INFO("DXGI native GPU frame emission is disabled; DirectX capture will use DesktopFrame output.");
    dxgi_native_gpu_disabled_logged_ = true;
  }
  result =
      controller_->DuplicateMonitor(frames.current_frame(),
                                    static_cast<int>(current_screen_id_),
                                    airan_capture_callback);

  using DuplicateResult = DxgiDuplicatorController::Result;
  if (result != DuplicateResult::SUCCEEDED) {
    LOG_ERROR("DxgiDuplicatorController failed to capture desktop, error code {}",
              DxgiDuplicatorController::ResultName(result));
  }
  RTC_HISTOGRAM_ENUMERATION(
      "WebRTC.DesktopCapture.Win.DirectXCapturerResult",
      static_cast<int>(result),
      static_cast<int>(DxgiDuplicatorController::Result::MAX_VALUE));
  switch (result) {
    case DuplicateResult::UNSUPPORTED_SESSION: {
      LOG_ERROR("Current binary is running on a session not supported by DirectX screen capturer.");
      callback_->OnCaptureResult(Result::ERROR_PERMANENT, nullptr);
      break;
    }
    case DuplicateResult::FRAME_PREPARE_FAILED: {
      LOG_ERROR("Failed to allocate a new DesktopFrame.");
      // This usually means we do not have enough memory or SharedMemoryFactory
      // cannot work correctly.
      callback_->OnCaptureResult(Result::ERROR_PERMANENT, nullptr);
      break;
    }
    case DuplicateResult::INVALID_MONITOR_ID: {
      LOG_ERROR("Invalid monitor id {}", current_screen_id_);
      callback_->OnCaptureResult(Result::ERROR_PERMANENT, nullptr);
      break;
    }
    case DuplicateResult::INITIALIZATION_FAILED:
    case DuplicateResult::DUPLICATION_FAILED: {
      callback_->OnCaptureResult(Result::ERROR_TEMPORARY, nullptr);
      break;
    }
    case DuplicateResult::SUCCEEDED: {
      std::unique_ptr<DesktopFrame> frame =
          frames.current_frame()->frame()->Share();

      int64_t capture_time_ms64 =
          (TimeNanos() - capture_start_time_nanos) / kNumNanosecsPerMillisec;
      int capture_time_ms = static_cast<int>(
          (std::min)(capture_time_ms64,
                     static_cast<int64_t>((std::numeric_limits<int>::max)())));
      RTC_HISTOGRAM_COUNTS_1000(
          "WebRTC.DesktopCapture.Win.DirectXCapturerFrameTime",
          capture_time_ms);
      frame->set_capture_time_ms(capture_time_ms);
      frame->set_capturer_id(DesktopCapturerId::kScreenCapturerWinDirectx);
      // The DXGI Output Duplicator supports embedding the cursor but it is
      // only supported on very few display adapters. This switch allows us
      // to exclude an integrated cursor for all captured frames.
      if (!options_.prefer_cursor_embedded()) {
        frame->set_may_contain_cursor(false);
      }

      // TODO(julien.isorce): http://crbug.com/945468. Set the icc profile on
      // the frame, see WindowCapturerMac::CaptureFrame.

      callback_->OnCaptureResult(Result::SUCCESS, std::move(frame));
      break;
    }
  }
}

bool ScreenCapturerWinDirectx::GetSourceList(SourceList* sources) {
  std::vector<std::string> device_names;
  if (!controller_->GetDeviceNames(&device_names)) {
    return false;
  }

  return GetScreenListFromDeviceNames(device_names, sources);
}

bool ScreenCapturerWinDirectx::SelectSource(SourceId id) {
  std::vector<std::string> device_names;
  if (!controller_->GetDeviceNames(&device_names)) {
    return false;
  }

  DesktopCapturer::SourceList screens;
  if (!GetScreenListFromDeviceNames(device_names, &screens) ||
      screens.empty()) {
    return false;
  }
  if (id == kFullDesktopScreenId) {
    id = GetPrimaryScreenId();
    if (id == kInvalidScreenId) {
      return false;
    }
    LOG_INFO("Airan DXGI screen capturer maps full desktop source to primary screen id {}", id);
  }

  int index;
  index = GetIndexFromScreenId(id, device_names);
  if (index == -1) {
    return false;
  }

  current_screen_id_ = index;
  return true;
}

}  // namespace airan::desktop_capture
