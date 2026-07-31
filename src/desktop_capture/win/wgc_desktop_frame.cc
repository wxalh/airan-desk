/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "desktop_capture/win/wgc_desktop_frame.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "desktop_capture/desktop_frame.h"
#include "desktop_capture/desktop_geometry.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

WgcDesktopFrame::WgcDesktopFrame(DesktopSize size,
                                 int stride,
                                 std::vector<uint8_t>&& image_data)
    : DesktopFrame(size, stride, image_data.data(), nullptr),
      image_data_(std::move(image_data)) {}

WgcDesktopFrame::~WgcDesktopFrame() = default;

}  // namespace airan::desktop_capture
