# Windows 编译指南

简体中文 | [English](build_win.en.md)

## 当前构建模型

Windows 构建使用 MSVC Qt、Google WebRTC 静态包、可选 FFmpeg shared/dev 运行时包、静态 `spdlog` 和静态 `libvterm`。项目要求 C++20。

| 目标 | 架构 | WebRTC target | Preset |
| --- | --- | --- | --- |
| Windows 10/11 | x64 | `libwebrtc::win10_x64_m144_md` | `win10-x64-release` / `win10-x64-debug` |
| Windows 10/11 | arm64 | `libwebrtc::win10_arm64_m144_md` | `win10-arm64-release` / `win10-arm64-debug` |
| Windows 7 | x86 | `libwebrtc::win7_x86_m109_md` | `win7-x86-release` / `win7-x86-debug` |

说明：

- WebRTC Windows 包使用 MSVC ABI，必须搭配 MSVC 版 Qt。不要混用 MinGW Qt。
- 当前 preset 使用 VS2022、`WEBRTC_MSVC_RUNTIME=md`，并让 `CMAKE_MSVC_RUNTIME_LIBRARY` 随 Debug/Release 切换为 `MultiThreadedDebugDLL` / `MultiThreadedDLL`。
- Win10/11 目标启用 WGC/D3D11 原生纹理显示路径；Win7 目标不启用 D3D11 UI 渲染和 D3D11 frame 信号，远端视频走 CPU 图像路径。
- Win7 目标使用项目内 fork 的 Windows GDI/Magnifier 桌面采集路径，不编译 WGC/DXGI/DirectX capturer 源码。
- Qt 通过 `windeployqt` 动态部署。
- OpenSSL 不是强制构建依赖；找到运行时 DLL 时自动复制，找不到时跳过。
- Airan-Desk 原创代码适用 Mozilla Public License Version 2.0（MPL-2.0）；FFmpeg 不适用该许可，而是以可替换、动态加载的 GNU Lesser General Public License 共享库使用。
- `AIRAN_ENABLE_FFMPEG_RUNTIME` 默认为 `ON`；缺少 FFmpeg shared/dev 包时会明确警告并降级为 WebRTC 内部 codec。

## 环境要求

- Visual Studio 2022。
- CMake 3.21+。
- Git。
- Windows 10 x64 和 Windows 7 x86 使用 Qt 5 MSVC 包；Windows 10 arm64 使用 Qt 6.8.3 MSVC arm64 包。Windows 7 x86 可使用 Qt 5.9.x/5.15.x 的 MSVC x86 包。
- `third_party/webrtc` 已解压与目标平台、架构和运行库匹配的 Google WebRTC 静态包。
- 可选：`third_party/ffmpeg-builds/<package>` 和 OpenSSL 运行时。

## 准备源码

```cmd
git clone <repository-url>
cd airan-desk
git submodule update --init --recursive
```

## 准备 Qt

示例路径：

- x64：`C:/Qt/5.15.2/msvc2019_64`
- arm64：`C:/Qt/6.8.3/msvc2022_arm64`
- Windows 7 x86 preset：`C:/Qt/5.9.9/msvc2015`

对应 CMake cache variable：Qt 5 使用 `Qt5_DIR`，Qt 6 使用 `Qt6_DIR`。

当前 preset 写入了以下默认 Qt 路径：

- `win10-x64`：`C:/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5`
- `win10-arm64`：`C:/Qt/6.8.3/msvc2022_arm64/lib/cmake/Qt6`
- `win7-x86`：`C:/Qt/5.9.9/msvc2015/lib/cmake/Qt5`

如果本机 Qt 不在这些位置，请用 `CMakeUserPresets.json` 覆盖。

## 使用 CMake Presets

### Windows 10/11 x64

```cmd
cmake --preset win10-x64
cmake --build --preset win10-x64-release
```

Debug：

```cmd
cmake --build --preset win10-x64-debug
```

### Windows 10/11 arm64

```cmd
cmake --preset win10-arm64
cmake --build --preset win10-arm64-release
```

Debug：

```cmd
cmake --build --preset win10-arm64-debug
```

### Windows 7 x86

```cmd
cmake --preset win7-x86
cmake --build --preset win7-x86-release
```

Debug：

```cmd
cmake --build --preset win7-x86-debug
```

自定义 FFmpeg 或 OpenSSL 路径请通过 `CMakeUserPresets.json` 设置。

## 输出目录

可执行文件位于：

```text
out/build/<configure-preset>/release/airan-desk.exe
out/build/<configure-preset>/debug/airan-desk.exe
```

构建后会自动部署：

- Qt DLL 和插件，由 `windeployqt` 处理。
- `conf/`
- `locale/`，项目翻译。
- `translations/`，Qt 系统翻译，找到时复制。
- FFmpeg DLL，默认在依赖包存在时复制；正式打包要求后端启用并同时提供完整 LGPL 和对应源码材料。
- `licenses/Cisco-OpenH264-BINARY_LICENSE.txt` 许可证文本；Airan-Desk 不复制 Cisco OpenH264 二进制，用户必须另行从 Cisco 获取并自行启停。
- `licenses/WebRTC-args.gn` 和 `licenses/WebRTC-source-revision.txt` 来自实际选中的 slice。当前 m144 slice 记录 `rtc_use_h264=false`。
- OpenSSL DLL，找到时复制。

不会部署 WebRTC DLL，因为当前 WebRTC 静态链接。

## 常见问题

### 找不到 Qt

Qt 5 目标确认 `Qt5_DIR` 指向 `.../lib/cmake/Qt5`；Windows arm64 确认 `Qt6_DIR` 指向 `.../lib/cmake/Qt6`。Qt 架构必须与 WebRTC 包架构一致。

### `0xc000007b`

通常是 32/64 位依赖混用导致。检查 Qt、OpenSSL、FFmpeg 和目标程序架构。

### MinGW Qt 可以用吗

不可以。当前 WebRTC Windows 包是 MSVC ABI，必须使用 MSVC 版 Qt。

### FFmpeg backend 被禁用

确认 `AIRAN_FFMPEG_ROOT` 指向包含 `include/`、`lib/` 和 `bin/*.dll` 的 shared/dev 包。Windows 10/11 默认 FFmpeg 8.1，Windows 7 默认 FFmpeg 7.1。
