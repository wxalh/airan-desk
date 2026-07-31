/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_MAC_DESKTOP_FRAME_PROVIDER_H_
#define AIRAN_DESKTOP_CAPTURE_MAC_DESKTOP_FRAME_PROVIDER_H_

#include <CoreGraphics/CoreGraphics.h>
#include <IOSurface/IOSurface.h>

#include <map>
#include <memory>

#include "api/sequence_checker.h"
#include "desktop_capture/shared_desktop_frame.h"
#include "sdk/objc/helpers/scoped_cftyperef.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

class DesktopFrameProvider {
 public:
  explicit DesktopFrameProvider(bool allow_iosurface);
  ~DesktopFrameProvider();

  DesktopFrameProvider(const DesktopFrameProvider&) = delete;
  DesktopFrameProvider& operator=(const DesktopFrameProvider&) = delete;

  // The caller takes ownership of the returned desktop frame. Otherwise
  // returns null if `display_id` is invalid or not ready. Note that this
  // function does not remove the frame from the internal container. Caller
  // has to call the Release function.
  std::unique_ptr<DesktopFrame> TakeLatestFrameForDisplay(
      CGDirectDisplayID display_id);

  // OS sends the latest IOSurfaceRef through
  // CGDisplayStreamFrameAvailableHandler callback; we store it here.
  void InvalidateIOSurface(CGDirectDisplayID display_id,
                           ScopedCFTypeRef<IOSurfaceRef> io_surface);

  // Expected to be called before stopping the CGDisplayStreamRef streams.
  void Release();

  bool allow_iosurface() const { return allow_iosurface_; }

 private:
  SequenceChecker thread_checker_;
  const bool allow_iosurface_;

  // Most recent IOSurface that contains a capture of matching display.
  std::map<CGDirectDisplayID, std::unique_ptr<SharedDesktopFrame>> io_surfaces_;
};

}  // namespace airan::desktop_capture

#endif  // AIRAN_DESKTOP_CAPTURE_MAC_DESKTOP_FRAME_PROVIDER_H_
