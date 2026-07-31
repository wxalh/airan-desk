/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_WIN_DESKTOP_CAPTURE_UTILS_H_
#define AIRAN_DESKTOP_CAPTURE_WIN_DESKTOP_CAPTURE_UTILS_H_

#include <comdef.h>

#include <string>

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {
namespace desktop_capture {
namespace utils {

// Generates a human-readable string from a COM error.
std::string ComErrorToString(const _com_error& error);

}  // namespace utils
}  // namespace desktop_capture
}  // namespace airan::desktop_capture

#endif  // AIRAN_DESKTOP_CAPTURE_WIN_DESKTOP_CAPTURE_UTILS_H_
