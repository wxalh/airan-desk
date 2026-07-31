/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_WINDOW_FINDER_WIN_H_
#define AIRAN_DESKTOP_CAPTURE_WINDOW_FINDER_WIN_H_

#include "desktop_capture/desktop_capture_types.h"
#include "desktop_capture/desktop_geometry.h"
#include "desktop_capture/window_finder.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

// The implementation of WindowFinder for Windows.
class WindowFinderWin final : public WindowFinder {
 public:
  WindowFinderWin();
  ~WindowFinderWin() override;

  // WindowFinder implementation.
  WindowId GetWindowUnderPoint(DesktopVector point) override;
};

}  // namespace airan::desktop_capture

#endif  // AIRAN_DESKTOP_CAPTURE_WINDOW_FINDER_WIN_H_
