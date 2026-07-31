/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "desktop_capture/mac/desktop_configuration_monitor.h"

#include "desktop_capture/mac/desktop_configuration.h"
#include "rtc_base/logging.h"
#include "rtc_base/trace_event.h"
#include "common/logger_manager.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

DesktopConfigurationMonitor::DesktopConfigurationMonitor() {
  CGError err = CGDisplayRegisterReconfigurationCallback(
      DesktopConfigurationMonitor::DisplaysReconfiguredCallback, this);
  if (err != kCGErrorSuccess)
    LOG_ERROR("CGDisplayRegisterReconfigurationCallback {}",
              static_cast<int>(err));
  MutexLock lock(&desktop_configuration_lock_);
  desktop_configuration_ = MacDesktopConfiguration::GetCurrent(
      MacDesktopConfiguration::TopLeftOrigin);
}

DesktopConfigurationMonitor::~DesktopConfigurationMonitor() {
  CGError err = CGDisplayRemoveReconfigurationCallback(
      DesktopConfigurationMonitor::DisplaysReconfiguredCallback, this);
  if (err != kCGErrorSuccess)
    LOG_ERROR("CGDisplayRemoveReconfigurationCallback {}",
              static_cast<int>(err));
}

MacDesktopConfiguration DesktopConfigurationMonitor::desktop_configuration() {
  MutexLock lock(&desktop_configuration_lock_);
  return desktop_configuration_;
}

// static
// This method may be called on any system thread.
void DesktopConfigurationMonitor::DisplaysReconfiguredCallback(
    CGDirectDisplayID display,
    CGDisplayChangeSummaryFlags flags,
    void* user_parameter) {
  DesktopConfigurationMonitor* monitor =
      reinterpret_cast<DesktopConfigurationMonitor*>(user_parameter);
  monitor->DisplaysReconfigured(display, flags);
}

void DesktopConfigurationMonitor::DisplaysReconfigured(
    CGDirectDisplayID display,
    CGDisplayChangeSummaryFlags flags) {
  TRACE_EVENT0("webrtc", "DesktopConfigurationMonitor::DisplaysReconfigured");
  LOG_INFO("DisplaysReconfigured: DisplayID {}; ChangeSummaryFlags {}", display, flags);

  if (flags & kCGDisplayBeginConfigurationFlag) {
    reconfiguring_displays_.insert(display);
    return;
  }

  reconfiguring_displays_.erase(display);
  if (reconfiguring_displays_.empty()) {
    MutexLock lock(&desktop_configuration_lock_);
    desktop_configuration_ = MacDesktopConfiguration::GetCurrent(
        MacDesktopConfiguration::TopLeftOrigin);
  }
}

}  // namespace airan::desktop_capture
