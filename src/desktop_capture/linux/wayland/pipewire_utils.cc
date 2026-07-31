/*
 *  Copyright 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "desktop_capture/linux/wayland/pipewire_utils.h"

#include <pipewire/pipewire.h>

#include "rtc_base/sanitizer.h"

namespace airan::desktop_capture {

RTC_NO_SANITIZE("cfi-icall")
bool InitializePipeWire() {
  return true;
}

PipeWireThreadLoopLock::PipeWireThreadLoopLock(pw_thread_loop* loop)
    : loop_(loop) {
  pw_thread_loop_lock(loop_);
}

PipeWireThreadLoopLock::~PipeWireThreadLoopLock() {
  pw_thread_loop_unlock(loop_);
}

RTC_NO_SANITIZE("cfi-icall")
PipeWireInitializer::PipeWireInitializer() {
  pw_init(/*argc=*/nullptr, /*argv=*/nullptr);
}

RTC_NO_SANITIZE("cfi-icall")
PipeWireInitializer::~PipeWireInitializer() {
  pw_deinit();
}

}  // namespace airan::desktop_capture
