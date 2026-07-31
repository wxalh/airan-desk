/*
 *  Copyright 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_LINUX_WAYLAND_MOUSE_CURSOR_MONITOR_PIPEWIRE_H_
#define AIRAN_DESKTOP_CAPTURE_LINUX_WAYLAND_MOUSE_CURSOR_MONITOR_PIPEWIRE_H_

#include "api/sequence_checker.h"
#include "desktop_capture/desktop_capture_options.h"
#include "desktop_capture/mouse_cursor_monitor.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/thread_annotations.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

class MouseCursorMonitorPipeWire : public MouseCursorMonitor {
 public:
  explicit MouseCursorMonitorPipeWire(const DesktopCaptureOptions& options);
  ~MouseCursorMonitorPipeWire() override;

  // MouseCursorMonitor:
  void Init(Callback* callback, Mode mode) override;
  void Capture() override;

  DesktopCaptureOptions options_ RTC_GUARDED_BY(sequence_checker_);
  Callback* callback_ RTC_GUARDED_BY(sequence_checker_) = nullptr;
  Mode mode_ RTC_GUARDED_BY(sequence_checker_) = SHAPE_AND_POSITION;
  RTC_NO_UNIQUE_ADDRESS SequenceChecker sequence_checker_;
};

}  // namespace airan::desktop_capture

#endif  // AIRAN_DESKTOP_CAPTURE_LINUX_WAYLAND_MOUSE_CURSOR_MONITOR_PIPEWIRE_H_
