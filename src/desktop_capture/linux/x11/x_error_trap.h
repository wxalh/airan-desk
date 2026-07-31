/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_LINUX_X11_X_ERROR_TRAP_H_
#define AIRAN_DESKTOP_CAPTURE_LINUX_X11_X_ERROR_TRAP_H_

#include <X11/Xlib.h>

#include "rtc_base/synchronization/mutex.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

// Helper class that registers an X Window error handler. Caller can use
// GetLastErrorAndDisable() to get the last error that was caught, if any.
class XErrorTrap {
 public:
  explicit XErrorTrap(Display* display);

  XErrorTrap(const XErrorTrap&) = delete;
  XErrorTrap& operator=(const XErrorTrap&) = delete;

  ~XErrorTrap();

  // Returns the last error if one was caught, otherwise 0. Also unregisters the
  // error handler and replaces it with `original_error_handler_`.
  int GetLastErrorAndDisable();

 private:
  MutexLock mutex_lock_;
  XErrorHandler original_error_handler_ = nullptr;
};

}  // namespace airan::desktop_capture

#endif  // AIRAN_DESKTOP_CAPTURE_LINUX_X11_X_ERROR_TRAP_H_
