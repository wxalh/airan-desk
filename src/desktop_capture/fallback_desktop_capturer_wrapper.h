/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_FALLBACK_DESKTOP_CAPTURER_WRAPPER_H_
#define AIRAN_DESKTOP_CAPTURE_FALLBACK_DESKTOP_CAPTURER_WRAPPER_H_

#include <chrono>
#include <memory>

#include "desktop_capture/desktop_capture_types.h"
#include "desktop_capture/desktop_capturer.h"
#include "desktop_capture/desktop_frame.h"
#include "desktop_capture/desktop_geometry.h"
#include "desktop_capture/shared_memory.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

// A DesktopCapturer wrapper owns two DesktopCapturer implementations. If the
// main DesktopCapturer fails, it uses the secondary one instead. Two capturers
// are expected to return same SourceList, and the meaning of each SourceId is
// identical, otherwise FallbackDesktopCapturerWrapper may return frames from
// different sources. Using asynchronized DesktopCapturer implementations with
// SharedMemoryFactory is not supported, and may result crash or assertion
// failure.
class FallbackDesktopCapturerWrapper final : public DesktopCapturer,
                                             public DesktopCapturer::Callback {
 public:
  FallbackDesktopCapturerWrapper(
      std::unique_ptr<DesktopCapturer> main_capturer,
      std::unique_ptr<DesktopCapturer> secondary_capturer);
  ~FallbackDesktopCapturerWrapper() override;

  // DesktopCapturer interface.
  void Start(DesktopCapturer::Callback* callback) override;
  void SetSharedMemoryFactory(
      std::unique_ptr<SharedMemoryFactory> shared_memory_factory) override;
  void SetAiranCaptureCallback(
      airan::media::AiranCaptureCallback* callback) override;
  void CaptureFrame() override;
  void SetExcludedWindow(WindowId window) override;
  bool GetSourceList(SourceList* sources) override;
  bool SelectSource(SourceId id) override;
  bool FocusOnSelectedSource() override;
  bool IsOccluded(const DesktopVector& pos) override;

 private:
  // DesktopCapturer::Callback interface.
  void OnCaptureResult(Result result,
                       std::unique_ptr<DesktopFrame> frame) override;

  bool mainCapturerAvailableForProbe() const;
  void openMainCapturerBreaker(Result result);

  const std::unique_ptr<DesktopCapturer> main_capturer_;
  const std::unique_ptr<DesktopCapturer> secondary_capturer_;
  std::unique_ptr<SharedMemoryFactory> shared_memory_factory_;
  bool main_capturer_permanent_error_ = false;
  int main_capturer_failure_count_ = 0;
  bool main_capturer_breaker_open_ = false;
  std::chrono::steady_clock::time_point main_capturer_breaker_opened_at_;
  DesktopCapturer::Callback* callback_ = nullptr;
};

}  // namespace airan::desktop_capture

#endif  // AIRAN_DESKTOP_CAPTURE_FALLBACK_DESKTOP_CAPTURER_WRAPPER_H_
