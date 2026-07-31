/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "desktop_capture/win/screen_capture_utils.h"

#include <windows.h>

#if !defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN7)
#include <shellscalingapi.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "desktop_capture/desktop_capture_types.h"
#include "desktop_capture/desktop_capturer.h"
#include "desktop_capture/desktop_geometry.h"
#include "rtc_base/checks.h"
#include "common/logger_manager.h"
#include "rtc_base/string_utils.h"
#include "rtc_base/win32.h"

#include "desktop_capture/airan_webrtc_compat.h"

namespace airan::desktop_capture {

namespace {

constexpr DesktopCapturer::SourceId kAiranMonitorSourceIdBase =
    static_cast<DesktopCapturer::SourceId>(0x40000000);

bool IsValidDisplayDeviceIndex(DesktopCapturer::SourceId screen) {
  return screen >= 0 &&
         screen <= static_cast<DesktopCapturer::SourceId>(
                       (std::numeric_limits<DWORD>::max)());
}

struct MonitorEnumerationState {
  DesktopCapturer::SourceList* screens = nullptr;
  int count = 0;
};

BOOL CALLBACK EnumerateMonitorSources(HMONITOR,
                                      HDC,
                                      LPRECT rect,
                                      LPARAM param) {
  auto* state = reinterpret_cast<MonitorEnumerationState*>(param);
  if (!state)
    return FALSE;
  if (rect && state->screens) {
    const std::string title =
        "monitor:" + std::to_string(state->count) + " " +
        std::to_string(rect->left) + "," + std::to_string(rect->top) + " " +
        std::to_string(rect->right - rect->left) + "x" +
        std::to_string(rect->bottom - rect->top);
    state->screens->push_back({MakeAiranMonitorSourceId(state->count), title});
  }
  ++state->count;
  return TRUE;
}

struct MonitorSelectionState {
  int target = 0;
  int current = 0;
  HMONITOR monitor = nullptr;
  RECT rect{};
  bool found = false;
};

BOOL CALLBACK SelectMonitorByIndex(HMONITOR monitor,
                                   HDC,
                                   LPRECT rect,
                                   LPARAM param) {
  auto* state = reinterpret_cast<MonitorSelectionState*>(param);
  if (!state)
    return FALSE;
  if (state->current == state->target) {
    state->monitor = monitor;
    if (rect)
      state->rect = *rect;
    state->found = true;
    return FALSE;
  }
  ++state->current;
  return TRUE;
}

}  // namespace

bool HasActiveDisplay() {
  DesktopCapturer::SourceList screens;

  return GetScreenList(&screens) && !screens.empty();
}

bool GetScreenList(DesktopCapturer::SourceList* screens,
                   std::vector<std::string>* device_names /* = nullptr */) {
  RTC_DCHECK(screens->empty());
  RTC_DCHECK(!device_names || device_names->empty());

  BOOL enum_result = TRUE;
  for (int device_index = 0;; ++device_index) {
    DISPLAY_DEVICEW device;
    device.cb = sizeof(device);
    enum_result = EnumDisplayDevicesW(NULL, device_index, &device, 0);

    // `enum_result` is 0 if we have enumerated all devices.
    if (!enum_result) {
      break;
    }

    // We only care about active displays.
    if (!(device.StateFlags & DISPLAY_DEVICE_ACTIVE)) {
      continue;
    }

    screens->push_back({device_index, std::string()});
    if (device_names) {
      device_names->push_back(ToUtf8(device.DeviceName));
    }
  }
  return true;
}

bool IsAiranMonitorSourceId(DesktopCapturer::SourceId screen) {
  return screen >= kAiranMonitorSourceIdBase;
}

DesktopCapturer::SourceId MakeAiranMonitorSourceId(int monitor_index) {
  return kAiranMonitorSourceIdBase +
         static_cast<DesktopCapturer::SourceId>((std::max)(0, monitor_index));
}

int AiranMonitorIndexFromSourceId(DesktopCapturer::SourceId screen) {
  if (!IsAiranMonitorSourceId(screen))
    return -1;
  const DesktopCapturer::SourceId index = screen - kAiranMonitorSourceIdBase;
  if (index > static_cast<DesktopCapturer::SourceId>(
                  (std::numeric_limits<int>::max)())) {
    return -1;
  }
  return static_cast<int>(index);
}

bool GetAiranMonitorList(DesktopCapturer::SourceList* screens) {
  RTC_DCHECK(screens->empty());
  MonitorEnumerationState state;
  state.screens = screens;
  EnumDisplayMonitors(nullptr, nullptr, EnumerateMonitorSources,
                      reinterpret_cast<LPARAM>(&state));
  return state.count > 0;
}

int GetAiranMonitorCount() {
  MonitorEnumerationState state;
  EnumDisplayMonitors(nullptr, nullptr, EnumerateMonitorSources,
                      reinterpret_cast<LPARAM>(&state));
  return state.count;
}

DesktopCapturer::SourceId GetPrimaryScreenId() {
  DesktopCapturer::SourceList screens;
  if (GetScreenList(&screens) && !screens.empty()) {
    for (const auto& screen : screens) {
      const DesktopRect rect = GetScreenRect(screen.id, std::nullopt);
      if (!rect.is_empty() && rect.left() == 0 && rect.top() == 0) {
        return screen.id;
      }
    }

    for (const auto& screen : screens) {
      const DesktopRect rect = GetScreenRect(screen.id, std::nullopt);
      if (!rect.is_empty()) {
        return screen.id;
      }
    }
  }

  DesktopCapturer::SourceList monitor_screens;
  if (GetAiranMonitorList(&monitor_screens) && !monitor_screens.empty()) {
    for (const auto& screen : monitor_screens) {
      const DesktopRect rect = GetScreenRect(screen.id, std::nullopt);
      if (!rect.is_empty() && rect.left() == 0 && rect.top() == 0) {
        return screen.id;
      }
    }

    for (const auto& screen : monitor_screens) {
      const DesktopRect rect = GetScreenRect(screen.id, std::nullopt);
      if (!rect.is_empty()) {
        return screen.id;
      }
    }
  }

  return kInvalidScreenId;
}

bool GetHmonitorFromDeviceIndex(const DesktopCapturer::SourceId device_index,
                                HMONITOR* hmonitor) {
  if (device_index == kFullDesktopScreenId) {
    LOG_WARN("Airan screen capture rejects full desktop source for per-screen capture.");
    return false;
  }

  if (IsAiranMonitorSourceId(device_index)) {
    MonitorSelectionState selection;
    selection.target = AiranMonitorIndexFromSourceId(device_index);
    EnumDisplayMonitors(nullptr, nullptr, SelectMonitorByIndex,
                        reinterpret_cast<LPARAM>(&selection));
    if (selection.found && selection.monitor) {
      *hmonitor = selection.monitor;
      return true;
    }
    return false;
  }

  DesktopRect screen_rect = GetScreenRect(device_index, std::nullopt);
  if (screen_rect.is_empty()) {
    return false;
  }

  RECT rect = {screen_rect.left(), screen_rect.top(), screen_rect.right(),
               screen_rect.bottom()};

  HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONULL);
  if (monitor == NULL) {
    LOG_WARN("No HMONITOR found for supplied device index.");
    return false;
  }

  *hmonitor = monitor;
  return true;
}

bool IsMonitorValid(const HMONITOR monitor) {
  if (monitor == 0) {
    return false;
  }

  MONITORINFO monitor_info;
  monitor_info.cbSize = sizeof(MONITORINFO);
  return GetMonitorInfoA(monitor, &monitor_info);
}

DesktopRect GetMonitorRect(const HMONITOR monitor) {
  MONITORINFO monitor_info;
  monitor_info.cbSize = sizeof(MONITORINFO);
  if (!GetMonitorInfoA(monitor, &monitor_info)) {
    return DesktopRect();
  }

  return DesktopRect::MakeLTRB(
      monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
      monitor_info.rcMonitor.right, monitor_info.rcMonitor.bottom);
}

bool IsScreenValid(const DesktopCapturer::SourceId screen,
                   std::wstring* device_key) {
  if (IsAiranMonitorSourceId(screen)) {
    MonitorSelectionState selection;
    selection.target = AiranMonitorIndexFromSourceId(screen);
    EnumDisplayMonitors(nullptr, nullptr, SelectMonitorByIndex,
                        reinterpret_cast<LPARAM>(&selection));
    if (device_key)
      device_key->clear();
    return selection.found;
  }

  if (!IsValidDisplayDeviceIndex(screen)) {
    return false;
  }

  DISPLAY_DEVICEW device;
  device.cb = sizeof(device);
  BOOL enum_result =
      EnumDisplayDevicesW(NULL, static_cast<DWORD>(screen), &device, 0);
  if (enum_result) {
    *device_key = device.DeviceKey;
  }

  return enum_result && (device.StateFlags & DISPLAY_DEVICE_ACTIVE);
}

DesktopRect GetFullscreenRect() {
  return DesktopRect::MakeXYWH(GetSystemMetrics(SM_XVIRTUALSCREEN),
                               GetSystemMetrics(SM_YVIRTUALSCREEN),
                               GetSystemMetrics(SM_CXVIRTUALSCREEN),
                               GetSystemMetrics(SM_CYVIRTUALSCREEN));
}

DesktopVector GetDpiForMonitor(HMONITOR monitor) {
#if defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN7)
  (void)monitor;
#else
  UINT dpi_x, dpi_y;
  // MDT_EFFECTIVE_DPI includes the scale factor as well as the system DPI.
  HRESULT hr = ::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);
  if (SUCCEEDED(hr)) {
    return {static_cast<INT>(dpi_x), static_cast<INT>(dpi_y)};
  }
  LOG_WARN("GetDpiForMonitor() failed: {}", hr);
#endif

  // If we can't get the per-monitor DPI, then return the system DPI.
  HDC hdc = GetDC(nullptr);
  if (hdc) {
    DesktopVector dpi{GetDeviceCaps(hdc, LOGPIXELSX),
                      GetDeviceCaps(hdc, LOGPIXELSY)};
    ReleaseDC(nullptr, hdc);
    return dpi;
  }

  // If everything fails, then return the default DPI for Windows.
  return {96, 96};
}

DesktopRect GetScreenRect(const DesktopCapturer::SourceId screen,
                          const std::optional<std::wstring>& device_key) {
  if (IsAiranMonitorSourceId(screen)) {
    MonitorSelectionState selection;
    selection.target = AiranMonitorIndexFromSourceId(screen);
    EnumDisplayMonitors(nullptr, nullptr, SelectMonitorByIndex,
                        reinterpret_cast<LPARAM>(&selection));
    if (!selection.found)
      return DesktopRect();
    return DesktopRect::MakeLTRB(selection.rect.left, selection.rect.top,
                                 selection.rect.right, selection.rect.bottom);
  }

  if (!IsValidDisplayDeviceIndex(screen)) {
    return DesktopRect();
  }

  DISPLAY_DEVICEW device;
  device.cb = sizeof(device);
  BOOL result =
      EnumDisplayDevicesW(NULL, static_cast<DWORD>(screen), &device, 0);
  if (!result || !(device.StateFlags & DISPLAY_DEVICE_ACTIVE)) {
    return DesktopRect();
  }

  // Verifies the device index still maps to the same display device, to make
  // sure we are capturing the same device when devices are added or removed.
  // DeviceKey is documented as reserved, but it actually contains the registry
  // key for the device and is unique for each monitor, while DeviceID is not.
  if (device_key.has_value() && *device_key != device.DeviceKey) {
    return DesktopRect();
  }

  DEVMODEW device_mode;
  device_mode.dmSize = sizeof(device_mode);
  device_mode.dmDriverExtra = 0;
  result = EnumDisplaySettingsExW(device.DeviceName, ENUM_CURRENT_SETTINGS,
                                  &device_mode, 0);
  if (!result) {
    return DesktopRect();
  }

  return DesktopRect::MakeXYWH(
      device_mode.dmPosition.x, device_mode.dmPosition.y,
      device_mode.dmPelsWidth, device_mode.dmPelsHeight);
}

}  // namespace airan::desktop_capture
