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

#if AIRAN_WEBRTC_USE_PIPEWIRE
#include "modules/portal/pipewire_utils.h"
#endif

#include "rtc_base/sanitizer.h"

namespace airan::desktop_capture {

RTC_NO_SANITIZE("cfi-icall")
bool InitializePipeWire() {
#if AIRAN_WEBRTC_USE_PIPEWIRE
  // Linking libpipewire does not initialize the weak pw_* stubs exported by
  // WebRTC. Prepare them before any caller reaches pw_init or another pw_* API.
  return webrtc::InitializePipeWire();
#else
  // Packages built without WebRTC's PipeWire backend use the linked DSO.
  return true;
#endif
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
