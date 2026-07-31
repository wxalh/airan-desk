# macOS Build Guide

[简体中文](build_mac.md) | English

## Current Build Model

macOS builds use Qt 5 UI, a Google WebRTC static package, static `spdlog`, and static `libvterm`. The project requires CMake 3.21+, C++20, Xcode Command Line Tools, and Qt 5.15.x.

| Architecture | WebRTC target | Preset | Default Qt prefix |
| --- | --- | --- | --- |
| x64 | `libwebrtc::macos_x64_m144` | `macos-x64` | `/usr/local/opt/qt@5` |
| arm64 | `libwebrtc::macos_arm64_m144` | `macos-arm64` | `/opt/homebrew/opt/qt@5` |

macOS development builds may still use a shared/dev package by setting
`AIRAN_ENABLE_FFMPEG_RUNTIME=ON` and `AIRAN_FFMPEG_ROOT`. The official macOS
package currently uses WebRTC's internal codecs because no matching FFmpeg
artifact is provided by the macOS workflow.

## Dependencies

Recommended environment:

- macOS 12+
- Xcode Command Line Tools
- CMake 3.21+
- Qt 5.15.x
- `third_party/webrtc` with `macos/x64/m144` and `macos/arm64/m144` packages

Homebrew setup:

```bash
xcode-select --install
brew install cmake
brew install qt@5
```

If your Qt install lives elsewhere, override `CMAKE_PREFIX_PATH` from `CMakeUserPresets.json`.

## Build with CMake Presets

### macOS arm64

```bash
cmake --preset macos-arm64
cmake --build --preset macos-arm64
```

Debug:

```bash
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug
```

### macOS x64

```bash
cmake --preset macos-x64
cmake --build --preset macos-x64
```

Debug:

```bash
cmake --preset macos-x64-debug
cmake --build --preset macos-x64-debug
```

Build and validate the `.app` bundle with Qt frameworks and create the release archive:

```bash
bash tools/build_package.sh --configure-preset macos-arm64 --build-preset macos-arm64 --package-name airan-desk-macos-arm64
```

## First Launch Permissions

A macOS remote desktop tool needs at least:

- Screen Recording permission.
- Accessibility permission.
- Input Monitoring permission on some macOS versions.
- Gatekeeper approval for unsigned builds.

These permissions must be tested on real hardware; a successful build alone is not enough.
