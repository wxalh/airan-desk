# Windows Build Guide

[简体中文](build_win.md) | English

## Current Build Model

Windows builds use MSVC Qt, a Google WebRTC static package, an optional FFmpeg shared/dev runtime package, static `spdlog`, and static `libvterm`. The project requires C++20.

| Target | Architecture | WebRTC target | Preset |
| --- | --- | --- | --- |
| Windows 10/11 | x64 | `libwebrtc::win10_x64_m144_md` | `win10-x64-release` / `win10-x64-debug` |
| Windows 10/11 | arm64 | `libwebrtc::win10_arm64_m144_md` | `win10-arm64-release` / `win10-arm64-debug` |
| Windows 7 | x86 | `libwebrtc::win7_x86_m109_md` | `win7-x86-release` / `win7-x86-debug` |

Notes:

- The Windows WebRTC package uses the MSVC ABI, so MSVC Qt is required. Do not use MinGW Qt.
- Current presets use VS2022, `WEBRTC_MSVC_RUNTIME=md`, and switch `CMAKE_MSVC_RUNTIME_LIBRARY` between `MultiThreadedDebugDLL` and `MultiThreadedDLL` for Debug/Release.
- Windows 10/11 targets enable the WGC/D3D11 native texture display path. Windows 7 targets do not expose D3D11 UI rendering or D3D11 frame signals; remote video uses the CPU image path.
- Windows 7 targets use this project's forked Windows GDI/Magnifier desktop capture path and do not compile WGC/DXGI/DirectX capturer sources.
- Qt is deployed dynamically through `windeployqt`.
- OpenSSL is not a hard build dependency. Runtime DLLs are copied when detected and skipped when missing.
- Original Airan-Desk code is licensed under the Mozilla Public License 2.0 (MPL-2.0). FFmpeg is not covered by that license; it is used as replaceable, dynamically loaded GNU Lesser General Public License shared libraries.
- `AIRAN_ENABLE_FFMPEG_RUNTIME` defaults to `ON`; a build emits an explicit warning and falls back to WebRTC's internal codecs when the FFmpeg shared/dev package is absent.

## Requirements

- Visual Studio 2022.
- CMake 3.21+.
- Git.
- Windows 10 x64 and Windows 7 x86 use Qt 5 MSVC packages. Windows 10 arm64 uses the Qt 6.8.3 MSVC arm64 package. Windows 7 x86 can use a Qt 5.9.x/5.15.x MSVC x86 package.
- `third_party/webrtc` contains the Google WebRTC static package matching the
  target platform, architecture, and runtime.
- Optional: `third_party/ffmpeg-builds/<package>` and OpenSSL runtime.

## Get the Source

```cmd
git clone <repository-url>
cd airan-desk
git submodule update --init --recursive
```

## Prepare Qt

Example paths:

- x64: `C:/Qt/5.15.2/msvc2019_64`
- arm64: `C:/Qt/6.8.3/msvc2022_arm64`
- Windows 7 x86 preset: `C:/Qt/5.9.9/msvc2015`

Corresponding CMake cache variables: `Qt5_DIR` for Qt 5 and `Qt6_DIR` for Qt 6.

Current presets set these default Qt paths:

- `win10-x64`: `C:/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5`
- `win10-arm64`: `C:/Qt/6.8.3/msvc2022_arm64/lib/cmake/Qt6`
- `win7-x86`: `C:/Qt/5.9.9/msvc2015/lib/cmake/Qt5`

If your Qt install lives elsewhere, override it with `CMakeUserPresets.json`.

## Build with CMake Presets

### Windows 10/11 x64

```cmd
cmake --preset win10-x64
cmake --build --preset win10-x64-release
```

Debug:

```cmd
cmake --build --preset win10-x64-debug
```

### Windows 10/11 arm64

```cmd
cmake --preset win10-arm64
cmake --build --preset win10-arm64-release
```

Debug:

```cmd
cmake --build --preset win10-arm64-debug
```

### Windows 7 x86

```cmd
cmake --preset win7-x86
cmake --build --preset win7-x86-release
```

Debug:

```cmd
cmake --build --preset win7-x86-debug
```

Custom FFmpeg or OpenSSL paths should be set from `CMakeUserPresets.json`.

## Output Directory

The executable is located at:

```text
out/build/<configure-preset>/release/airan-desk.exe
out/build/<configure-preset>/debug/airan-desk.exe
```

The build deploys:

- Qt DLLs and plugins through `windeployqt`.
- `conf/`
- `locale/`, project translations.
- `translations/`, Qt system translations when found.
- FFmpeg DLLs by default when the dependency package exists; official packaging forces the backend on and also requires complete LGPL and source materials.
- The `licenses/Cisco-OpenH264-BINARY_LICENSE.txt` license text. Airan-Desk does not copy a Cisco OpenH264 binary; the user obtains it separately from Cisco and controls whether it is enabled.
- `licenses/WebRTC-args.gn` and `licenses/WebRTC-source-revision.txt` from the selected slice. The selected m144 slice records `rtc_use_h264=false`.
- OpenSSL DLLs when found.

WebRTC DLLs are not deployed because WebRTC is linked statically.

## FAQ

### Qt Not Found

For Qt 5 targets, check that `Qt5_DIR` points to `.../lib/cmake/Qt5`. For Windows arm64, check that `Qt6_DIR` points to `.../lib/cmake/Qt6`. The Qt architecture must match the WebRTC package architecture.

### `0xc000007b`

This is usually caused by mixing 32-bit and 64-bit dependencies. Check Qt, OpenSSL, FFmpeg, and target program architecture.

### Can I Use MinGW Qt?

No. The current Windows WebRTC package uses the MSVC ABI, so MSVC Qt is required.

### FFmpeg Backend Is Disabled

Make sure `AIRAN_FFMPEG_ROOT` points to a shared/dev package containing `include/`, `lib/`, and `bin/*.dll`. Windows 10/11 defaults to FFmpeg 8.1; Windows 7 defaults to FFmpeg 7.1.
