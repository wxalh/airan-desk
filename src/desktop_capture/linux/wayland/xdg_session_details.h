/*
 *  Copyright 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_LINUX_WAYLAND_XDG_SESSION_DETAILS_H_
#define AIRAN_DESKTOP_CAPTURE_LINUX_WAYLAND_XDG_SESSION_DETAILS_H_

// GLib headers use 'signals' as a struct field name which conflicts with
// Qt's 'signals' keyword macro. Push/undef before including GLib, then restore.
#pragma push_macro("signals")
#pragma push_macro("slots")
#undef signals
#undef slots
#include <gio/gio.h>
#pragma pop_macro("slots")
#pragma pop_macro("signals")

#include <stdint.h>

#include <string>

namespace airan::desktop_capture::xdg_portal {

struct SessionDetails {
  GDBusProxy* proxy = nullptr;
  GCancellable* cancellable = nullptr;
  std::string session_handle;
  uint32_t pipewire_stream_node_id = 0;
};

}  // namespace airan::desktop_capture::xdg_portal

#endif  // AIRAN_DESKTOP_CAPTURE_LINUX_WAYLAND_XDG_SESSION_DETAILS_H_
