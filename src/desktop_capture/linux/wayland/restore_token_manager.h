/*
 *  Copyright 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AIRAN_DESKTOP_CAPTURE_LINUX_WAYLAND_RESTORE_TOKEN_MANAGER_H_
#define AIRAN_DESKTOP_CAPTURE_LINUX_WAYLAND_RESTORE_TOKEN_MANAGER_H_

#include <string>
#include <unordered_map>

#include "desktop_capture/desktop_capturer.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

class RestoreTokenManager {
 public:
  RestoreTokenManager(const RestoreTokenManager& manager) = delete;
  RestoreTokenManager& operator=(const RestoreTokenManager& manager) = delete;

  static RestoreTokenManager& GetInstance();

  void AddToken(DesktopCapturer::SourceId id, const std::string& token);
  std::string GetToken(DesktopCapturer::SourceId id);

  // Returns a source ID which does not have any token associated with it yet.
  DesktopCapturer::SourceId GetUnusedId();

 private:
  RestoreTokenManager() = default;
  ~RestoreTokenManager() = default;

  DesktopCapturer::SourceId last_source_id_ = 0;

  std::unordered_map<DesktopCapturer::SourceId, std::string> restore_tokens_;
};

}  // namespace airan::desktop_capture

#endif  // AIRAN_DESKTOP_CAPTURE_LINUX_WAYLAND_RESTORE_TOKEN_MANAGER_H_
