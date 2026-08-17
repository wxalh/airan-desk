# 第三方依赖总览

本仓库不提交大型二进制依赖目录。开发机和构建机需要在本地保留这些目录，`.gitignore` 会忽略它们。

许可证边界：Airan-Desk 原创代码适用 Mozilla Public License Version 2.0（MPL-2.0）；第三方组件适用各自许可。FFmpeg 不适用 MPL-2.0，而是以可替换、动态加载的 GNU Lesser General Public License 共享库使用。

## 源码子目录

这些依赖随仓库源码或子模块构建：

- `third_party/spdlog`：静态库，测试、示例和 benchmark 默认关闭。
- `third_party/libvterm`：静态库，CMake 会生成必要的 encoding include 文件。

获取源码后请执行：

```bash
git submodule update --init --recursive
```

## WebRTC 静态包

必须选择与构建目标完全匹配的自包含包。准备脚本会从最新不可变 Release 下载
`libwebrtc-manifest.json`，解析唯一资产，并在刷新共享的 `third_party/webrtc` 目录前
校验清单中的文件大小和 SHA-256：

```powershell
./tools/prepare_third_party.ps1 -PackageSet windows -WebrtcPackage windows-win10-x64-m144-md
```

```bash
bash ./tools/prepare_third_party.sh "" linux linux-ubuntu18-x64-m144-gnu
```

当前 CMake 会按平台、架构、运行库和构建配置选择 target：

| 平台 | target |
| --- | --- |
| Windows 10/11 x64 | `libwebrtc::win10_x64_m144_md` |
| Windows 10/11 arm64 | `libwebrtc::win10_arm64_m144_md` |
| Windows 7 x86 | `libwebrtc::win7_x86_m109_md` |
| Linux x64 | `libwebrtc::linux_x64_m144_gnu` |
| Linux arm64 | `libwebrtc::linux_arm64_m144_gnu` |
| Linux armhf | `libwebrtc::linux_armhf_m144_gnu` |
| macOS x64 | `libwebrtc::macos_x64_m144` |
| macOS arm64 | `libwebrtc::macos_arm64_m144` |
| Android | `libwebrtc::android_*_m144` |

说明：

- Windows 可通过 `WEBRTC_MSVC_RUNTIME=md|mt` 选择 `/MD` 或 `/MT` 包，默认 `md`。
- Windows 可通过 `WEBRTC_WINDOWS_PLATFORM=win10|win7` 选择 Windows 包族。
- Linux 可通过 `WEBRTC_LINUX_STL=gnu|libcxx` 选择 STL ABI，默认 `gnu`。
- Linux 可通过 `WEBRTC_LINUX_COMPAT=ubuntu18|centos7` 选择兼容基线；CentOS 7 包仅提供 x64/libc++。
- Debug 配置会优先链接同名 `*_debug` target；没有 debug 包时回退到 release target。

WebRTC m144 的上游来源固定到 Chromium branch-head 7559：
<https://webrtc.googlesource.com/src/+/refs/branch-heads/7559>。Windows 7 x86
使用 m109 branch-head 5414：
<https://webrtc.googlesource.com/src/+/refs/branch-heads/5414>。实际选择的平台包必须
用同一 Release 的 `libwebrtc-manifest.json` 条目校验，且发布包必须带有
`WebRTC-LICENSE.txt`、`WebRTC-PATENTS.txt` 和由该平台 slice 生成的
`WebRTC-Third-Party-Licenses.txt`；构建脚本来源为
<https://github.com/wxalh/libwebrtc_build>。

每个所选 slice 都提供实际 `args.gn`、精确 `source_revision.txt` 和
`PACKAGE-METADATA.json`。准备脚本只在下载归档时依据 `libwebrtc-manifest.json` 校验
文件大小和 SHA-256；解压目录中的缓存标记不属于 WebRTC 包接口，也不是 CMake 配置或
正式打包的前提。构建会把所选 slice 的构建参数和源码修订复制到发布目录。

## FFmpeg shared/dev 包

下载 LGPL shared/dev 包：

https://github.com/wxalh/FFmpeg-Builds/releases/tag/latest

CMake 默认查找以下目录：

| 平台 | 默认目录 |
| --- | --- |
| Windows 10/11 x64 | `third_party/ffmpeg-builds/ffmpeg-n8.1-latest-win64-lgpl-shared-8.1` |
| Windows 10/11 arm64 | `third_party/ffmpeg-builds/ffmpeg-n8.1-latest-winarm64-lgpl-shared-8.1` |
| Windows 7 x86 | `third_party/ffmpeg-builds/ffmpeg-n7.1-latest-win32-lgpl-shared-7.1` |
| Linux x64 | `third_party/ffmpeg-builds/ffmpeg-n8.1-latest-linux64-lgpl-shared-8.1` |
| Linux arm64 | `third_party/ffmpeg-builds/ffmpeg-n8.1-latest-linuxarm64-lgpl-shared-8.1` |
| Linux armhf | `third_party/ffmpeg-builds/ffmpeg-n8.1-latest-linuxarmhf-lgpl-shared-8.1` |

也可以通过 `CMakeUserPresets.json` 中的 `AIRAN_FFMPEG_ROOT` cache variable 显式指定。

包目录需要包含：

- `include/`
- `lib/`
- Windows 还需要 `bin/*.dll`
- Linux 可包含 `bin/ffmpeg` 供命令行使用

FFmpeg codec backend 运行时动态加载 `avutil`、`avcodec`、`avfilter` 和 `swscale`。
`AIRAN_ENABLE_FFMPEG_RUNTIME` 默认为 `ON`；开发构建找不到完整 shared/dev 包时会
明确警告并降级为 WebRTC 内部 codec。Windows 和 Linux 正式打包会强制启用 FFmpeg
backend，并要求完整 shared runtime、许可证和对应源码元数据；macOS 正式包不包含
FFmpeg。发行配置使用可替换的动态 LGPL FFmpeg，不得使用
`--enable-gpl`、`--enable-nonfree` 或普通 linked `--enable-libopenh264`。

Windows 10/11 和当前 Linux 包使用 FFmpeg `release/8.1` 分支，Windows 7 x86 使用
`release/7.1`。上游仓库为 <https://github.com/FFmpeg/FFmpeg.git>；构建链来自
<https://github.com/wxalh/FFmpeg-Builds>，并包含 Airan 的 OpenH264 runtime-loader
补丁。`latest` URL 会移动，因此不能作为精确来源记录。每一个实际发布的 FFmpeg
二进制必须随包记录精确 FFmpeg source revision、精确 FFmpeg-Builds source
revision、完整 build configuration（包括 configure 命令/输出与目标 variant）、
依赖修订和许可证、产物 checksum manifest，以及两个仓库的持久 source URL。
这些元数据会在存在时随包复制；只有 `LICENSE.txt` 的目录不满足正式发布要求。

## 可选 Cisco OpenH264

FFmpeg-Builds 只从 Cisco OpenH264 2.6.0 源码修订
`e3f5b10438e2bacc155cf54578222bd4236c9f06` 安装 `wels/` 头文件，不链接或打包
OpenH264 实现。源码仓库是 <https://github.com/cisco/openh264.git>，官方版本页是
<https://github.com/cisco/openh264/releases/tag/v2.6.0>，Cisco 二进制下载源是
<https://ciscobinary.openh264.org/>。

Airan 的 `src/media/codec/openh264/openh264_release_manifest.h` 固定每个平台的官方
文件名、大小、URL 和解压后二进制 SHA-256。例如 Windows x64 2.6.0 的 manifest
hash 是 `2076cb5675ec6c1a4c70e7a2a322552f547b6eeed649d6dfcd9e02a543b24691`；
其他平台必须按同一清单中的对应条目校验，不能套用该值。Airan-Desk 不随包提供、镜像
或缓存 Cisco 二进制。用户必须另行从 Cisco 下载、解压、导入并明确启用；用户可以随时
禁用或重新启用。仅与清单匹配且安装到应用管理的用户数据目录中的文件会用于
`libopenh264`，用户仍可替换 FFmpeg/Qt shared libraries；任意未列入清单的 OpenH264
实现都不会被信任。
完整 Cisco 条款见 `third_party/licenses/Cisco-OpenH264-BINARY_LICENSE.txt`。

## Qt

多数当前桌面 preset 使用 Qt 5，Windows arm64 preset 使用 Qt 6.8.3：

- 必需组件：`Core`、`Gui`、`Svg`、`Widgets`、`WebSockets`、`Network`
- 可选组件：`LinguistTools`，用于构建项目 `.qm` 翻译
- Linux 可选组件：`DBus`，Wayland/portal 相关路径可能需要

Qt 系统翻译目录会自动探测：

- `qmake -query QT_INSTALL_TRANSLATIONS`
- Qt 安装前缀下的 `translations`
- Qt 安装前缀下的 `share/qt5/translations`
- Qt 安装前缀下的 `share/qt6/translations`
- `/usr/share/qt5/translations`
- `/usr/share/qt6/translations`
- `/usr/share/qt/translations`
- `/usr/lib/qt5/translations`
- `/usr/lib/qt6/translations`

必要时可通过 `CMakeUserPresets.json` 中的 `AIRAN_QT_TRANSLATIONS_DIR` cache variable 覆盖。

## Linux 编译器

Linux 上项目要求 CMake 3.21+，且当前 WebRTC m144 头文件需要 GCC/G++ 11+。Ubuntu 18.04 默认 `g++` 7.5 会在 WebRTC C++20 头文件处失败。默认编译器过旧时，请通过 `CMakeUserPresets.json` 覆盖 `CMAKE_C_COMPILER` 和 `CMAKE_CXX_COMPILER`。

Ubuntu 18.04 可通过 Ubuntu Toolchain PPA 安装：

```bash
sudo apt install software-properties-common
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt update
sudo apt install gcc-11 g++-11
```

## OpenSSL

OpenSSL 只在 Windows 部署阶段自动探测，用于 Qt TLS/HTTPS/WSS 运行时支持。它不是强制构建依赖。

默认行为：

- `AIRAN_DEPLOY_OPENSSL_RUNTIME=ON`
- 查找 `libssl-*.dll` 和 `libcrypto-*.dll`
- 找到后复制到 `airan-desk.exe` 同级目录
- 找不到只输出状态信息，不阻塞构建

可通过 `CMakeUserPresets.json` 中的 `OPENSSL_ROOT_DIR` cache variable 显式指定路径。

## Codec SDK 策略

当前主线使用 FFmpeg 统一承接 NVENC、QSV/oneVPL、AMF、VAAPI、V4L2 mem2mem 等后端。运行机器仍然需要对应 GPU 驱动和系统运行时。
