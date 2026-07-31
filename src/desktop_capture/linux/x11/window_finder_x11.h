/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_LINUX_X11_WINDOW_FINDER_X11_H_
#define AIRAN_DESKTOP_CAPTURE_LINUX_X11_WINDOW_FINDER_X11_H_

#include "desktop_capture/desktop_capture_types.h"
#include "desktop_capture/desktop_geometry.h"
#include "desktop_capture/window_finder.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

class XAtomCache;

// The implementation of WindowFinder for X11.
class WindowFinderX11 final : public WindowFinder {
 public:
  explicit WindowFinderX11(XAtomCache* cache);
  ~WindowFinderX11() override;

  // WindowFinder implementation.
  WindowId GetWindowUnderPoint(DesktopVector point) override;

 private:
  XAtomCache* const cache_;
};

}  // namespace airan::desktop_capture

#endif  // AIRAN_DESKTOP_CAPTURE_LINUX_X11_WINDOW_FINDER_X11_H_
