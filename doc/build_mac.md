# macOS 编译指南

简体中文 | [English](build_mac.en.md)

## 当前构建模型

macOS 构建使用 Qt 5 UI、Google WebRTC 静态包、静态 `spdlog` 和静态 `libvterm`。项目要求 CMake 3.21+、C++20、Xcode Command Line Tools 和 Qt 5.15.x。

| 架构 | WebRTC target | Preset | 默认 Qt prefix |
| --- | --- | --- | --- |
| x64 | `libwebrtc::macos_x64_m144` | `macos-x64` | `/usr/local/opt/qt@5` |
| arm64 | `libwebrtc::macos_arm64_m144` | `macos-arm64` | `/opt/homebrew/opt/qt@5` |

macOS 开发构建仍可通过 `AIRAN_ENABLE_FFMPEG_RUNTIME=ON` 和 `AIRAN_FFMPEG_ROOT` 使用 shared/dev 包。
当前 macOS 打包流程没有对应的 FFmpeg 产物，因此使用 WebRTC 内置 codec。

## 依赖

推荐环境：

- macOS 12+
- Xcode Command Line Tools
- CMake 3.21+
- Qt 5.15.x
- `third_party/webrtc` 已包含 `macos/x64/m144` 和 `macos/arm64/m144` 包

Homebrew 基础工具：

```bash
xcode-select --install
brew install cmake
brew install qt@5
```

如果本机 Qt 不在默认路径，请通过 `CMakeUserPresets.json` 覆盖 `CMAKE_PREFIX_PATH`。

## 使用 CMake Presets

### macOS arm64

```bash
cmake --preset macos-arm64
cmake --build --preset macos-arm64
```

Debug：

```bash
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug
```

### macOS x64

```bash
cmake --preset macos-x64
cmake --build --preset macos-x64
```

Debug：

```bash
cmake --preset macos-x64-debug
cmake --build --preset macos-x64-debug
```

生成并校验带 Qt frameworks 的 `.app` bundle 和发布压缩包：

```bash
bash tools/build_package.sh --configure-preset macos-arm64 --build-preset macos-arm64 --package-name airan-desk-macos-arm64
```

## 首次运行权限

macOS 作为远程桌面工具，至少需要处理：

- 屏幕录制权限。
- 辅助功能权限。
- 部分系统版本还可能需要输入监控权限。
- 未签名应用的 Gatekeeper 放行。

这些权限必须在真实设备上验证，不能只依赖编译通过。
