/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_WINDOW_FINDER_MAC_H_
#define AIRAN_DESKTOP_CAPTURE_WINDOW_FINDER_MAC_H_

#include "api/scoped_refptr.h"
#include "desktop_capture/desktop_capture_types.h"
#include "desktop_capture/desktop_geometry.h"
#include "desktop_capture/window_finder.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

class DesktopConfigurationMonitor;

// The implementation of WindowFinder for Mac OSX.
class WindowFinderMac final : public WindowFinder {
 public:
  explicit WindowFinderMac(
      scoped_refptr<DesktopConfigurationMonitor> configuration_monitor);
  ~WindowFinderMac() override;

  // WindowFinder implementation.
  WindowId GetWindowUnderPoint(DesktopVector point) override;

 private:
  const scoped_refptr<DesktopConfigurationMonitor> configuration_monitor_;
};

}  // namespace airan::desktop_capture

#endif  // AIRAN_DESKTOP_CAPTURE_WINDOW_FINDER_MAC_H_
