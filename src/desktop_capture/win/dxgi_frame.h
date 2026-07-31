/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_WIN_DXGI_FRAME_H_
#define AIRAN_DESKTOP_CAPTURE_WIN_DXGI_FRAME_H_

#include <memory>

#include "desktop_capture/desktop_capture_types.h"
#include "desktop_capture/desktop_capturer.h"
#include "desktop_capture/desktop_geometry.h"
#include "desktop_capture/resolution_tracker.h"
#include "desktop_capture/shared_desktop_frame.h"
#include "desktop_capture/shared_memory.h"
#include "desktop_capture/win/dxgi_context.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

class DxgiDuplicatorController;

// A pair of a SharedDesktopFrame and a DxgiDuplicatorController::Context for
// the client of DxgiDuplicatorController.
class DxgiFrame final {
 public:
  using Context = DxgiFrameContext;

  // DxgiFrame does not take ownership of `factory`, consumers should ensure it
  // outlives this instance. nullptr is acceptable.
  explicit DxgiFrame(SharedMemoryFactory* factory);
  ~DxgiFrame();

  // Should not be called if Prepare() is not executed or returns false.
  SharedDesktopFrame* frame() const;

 private:
  // Allows DxgiDuplicatorController to access Prepare() and context() function
  // as well as Context class.
  friend class DxgiDuplicatorController;

  // Prepares current instance with desktop size and source id.
  bool Prepare(DesktopSize size, DesktopCapturer::SourceId source_id);

  // Should not be called if Prepare() is not executed or returns false.
  Context* context();

  SharedMemoryFactory* const factory_;
  ResolutionTracker resolution_tracker_;
  DesktopCapturer::SourceId source_id_ = kFullDesktopScreenId;
  std::unique_ptr<SharedDesktopFrame> frame_;
  Context context_;
};

}  // namespace airan::desktop_capture

#endif  // AIRAN_DESKTOP_CAPTURE_WIN_DXGI_FRAME_H_
