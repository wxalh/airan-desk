# 第三方源码与重新链接说明

简体中文 | [English](THIRD_PARTY_SOURCE_OFFER.md)

Airan-Desk 原创代码按 Mozilla Public License Version 2.0（MPL-2.0）授权，
完整文本见 `LICENSE`。本文记录本仓库构建的二进制包所使用第三方组件的源码、
许可证和重新链接信息。每个第三方组件仍适用其版权持有者声明的条款。

## Qt 运行库与对应源码

Airan-Desk 按 GNU Lesser General Public License version 3 动态链接 Qt 共享库。
官方 Windows x64 和 Windows 7 x86 workflow 使用 Qt 5.15.2，Windows arm64
workflow 使用 Qt 6.8.3；本地 Windows 7 x86 preset 也支持 Qt 5.9.9。Linux
workflow 从目标发行版的 `apt` 或 `yum` 仓库安装 Qt，macOS workflow 使用
Homebrew `qt@5`，因此其精确包版本和下游补丁由实际构建环境决定。发布包将 Qt
库保留为独立、可替换文件，也不禁止为调试 Qt 修改而进行逆向工程。

Windows 固定版本对应的上游 Qt 源码：

Qt 官方下载和源码入口：<https://download.qt.io/>

<https://download.qt.io/archive/qt/5.15/5.15.2/single/qt-everywhere-src-5.15.2.tar.xz>

<https://download.qt.io/archive/qt/5.9/5.9.9/single/qt-everywhere-opensource-src-5.9.9.tar.xz>

<https://download.qt.io/official_releases/qt/6.8/6.8.3/single/qt-everywhere-src-6.8.3.tar.xz>

适用的 LGPLv3 和 GPLv3 文本包含在 `licenses/` 中。Qt 官方二进制的源码见上述
Qt 官方链接。Linux 发行版包对应的源码包和补丁由其 `apt` 或 `yum` 仓库提供；
Homebrew `qt@5` 的配方和源码地址见
<https://formulae.brew.sh/formula/qt@5>。

## WebRTC m109 和 m144

Airan-Desk 使用由 `wxalh/libwebrtc_build` 构建的里程碑专用 Google WebRTC
静态库。每个随包 slice 都包含实际 `args.gn`、包校验值和精确
`source_revision.txt`。这些元数据与静态库共同记录该 slice 的 H.264、FFmpeg 或
OpenH264 内容。WebRTC 及其链接的第三方组件适用 BSD-style、MIT、ISC 和其他
许可证，文本包含在：

- `licenses/WebRTC-LICENSE.txt`
- `licenses/WebRTC-PATENTS.txt`
- `licenses/WebRTC-Third-Party-Licenses.txt`

源码来源：

- WebRTC m109：<https://webrtc.googlesource.com/src/+/refs/branch-heads/5414>
- WebRTC m144：<https://webrtc.googlesource.com/src/+/refs/branch-heads/7559>
- 构建脚本：<https://github.com/wxalh/libwebrtc_build>

发布包将精确 GN 参数记录为 `WebRTC-args.gn`，将归档校验值记录为
`WebRTC-package.sha256`，并将精确源码修订记录为
`WebRTC-source-revision.txt`。三个文件均位于许可证目录。

## OpenSSL

Windows x64/x86 preset 在探测到兼容动态安装时，可能包含供 Qt TLS 使用的可替换
OpenSSL 共享库；Windows arm64 官方工作流关闭这项可选部署。包含 OpenSSL 运行时
DLL 时，其许可证位于 `licenses/OpenSSL-LICENSE.txt`。配置的
`OPENSSL_ROOT_DIR` 对应实际 OpenSSL 版本；上游版本见
<https://www.openssl.org/source/>。

## FFmpeg shared runtime

FFmpeg 不适用 Airan-Desk 的 MPL-2.0。发布包包含 FFmpeg 时，使用按 GNU Lesser
General Public License 构建、可单独替换且动态加载的共享库。随包构建不启用
`--enable-gpl` 或 `--enable-nonfree`；接收者可替换共享库，发布包也不限制为调试
LGPL 库修改而进行的逆向工程。

Windows 10/11 和当前 Linux 包输入使用 FFmpeg `release/8.1`，Windows 7 x86
使用 `release/7.1`。上游源码是 <https://github.com/FFmpeg/FFmpeg.git>；构建链
来自 <https://github.com/wxalh/FFmpeg-Builds>，其中包含 Airan 的 OpenH264
runtime-loader 补丁。移动的 `latest` 下载地址不是精确源码记录。包含 FFmpeg 的发布包
同时包含精确 FFmpeg 和 FFmpeg-Builds 源码修订、完整构建配置、依赖修订和许可证、
校验清单，以及两个仓库的持久源码 URL。

## 可选 Cisco OpenH264 二进制

FFmpeg-Builds 只使用 Cisco OpenH264 2.6.0 源码修订
`e3f5b10438e2bacc155cf54578222bd4236c9f06` 中的 `wels/` 头文件，不链接或
打包 OpenH264 实现。源码版本见
<https://github.com/cisco/openh264/releases/tag/v2.6.0>，官方二进制来自
<https://ciscobinary.openh264.org/>。应用清单固定每个允许的文件名、大小、URL
和解压后 SHA-256；例如 Windows x64 2.6.0 的清单哈希是
`2076cb5675ec6c1a4c70e7a2a322552f547b6eeed649d6dfcd9e02a543b24691`。

Airan-Desk 不分发、镜像或缓存 Cisco OpenH264 二进制。最终用户需自行下载和导入，
并自行控制启用、禁用或重新启用。Cisco 完整条款在发布包的
`licenses/Cisco-OpenH264-BINARY_LICENSE.txt` 和源码的
`third_party/licenses/Cisco-OpenH264-BINARY_LICENSE.txt` 中。Cisco 的 AVC
专利许可受这些条款限制，并不涵盖所有商业、有偿、内容提供、广播或特定司法辖区用途。

## 其他组件

spdlog、fmt、libvterm、PipeWire、libva 和其他随包运行时组件列在
`licenses/Third-Party-Notices.md`。包含 libva 时，发布包会附带精确 2.24.0
源码提交、固定源码归档 URL、MIT `COPYING`、构建配置和 SHA-256 清单。对应组件
随包提供时，也会包含其完整许可证文本。
