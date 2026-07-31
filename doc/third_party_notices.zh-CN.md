# 第三方声明

简体中文 | [English](third_party_notices.md)

## 派生自 WebRTC 的桌面采集源码

`src/desktop_capture/` 下的文件派生自 Google WebRTC
`modules/desktop_capture` 源码树，用于 Airan 自有采集后端和基础兼容工作。
复制的文件保留原始 WebRTC 版权头和 BSD-style 许可证来源。

二进制包包含 WebRTC 版权和许可证声明，以及源码头引用的附加专利授权声明。

## FFmpeg 运行库

Airan 使用可替换的 LGPL FFmpeg 共享运行库。产品包使用符合 LGPL 且未启用
`--enable-gpl` 或 `--enable-nonfree` 的 FFmpeg shared build。

发布包标明精确 FFmpeg 构建，并包含适用的 FFmpeg LGPL 许可证和版权声明、
对应源码位置及构建配置。Airan 对 FFmpeg 的修改按适用 LGPL 条款提供。

## 可选 Cisco OpenH264 二进制

OpenH264 Video Codec provided by Cisco Systems, Inc.

Airan-Desk 不单独分发、镜像或缓存可选 FFmpeg 运行路径所使用的用户导入 Cisco
OpenH264 二进制。用户可自行将 Cisco 官方二进制下载到设备，在安装后导入，并独立
启用、禁用或重新启用。此说明不描述 monolithic WebRTC static slice 的内容；随包的
`WebRTC-args.gn` 记录该 slice 的构建参数。应用只随包提供 Cisco 许可证文本，不提供
独立 Cisco codec 二进制。完整权威声明位于源码的
`third_party/licenses/Cisco-OpenH264-BINARY_LICENSE.txt` 和二进制包的
`licenses/Cisco-OpenH264-BINARY_LICENSE.txt`。

Cisco 的 AVC 专利许可受该声明条款限制，并不授予或暗示涵盖其他所有用途，包括内容
提供商和广播机构的所有有偿或商业用途。专利范围和其他义务取决于具体用途和司法辖区；
本声明不保证绝对合规或零专利风险。

## Qt 运行库

Airan 动态链接所选 Qt 5 或 Qt 6 发行版。适用 Qt 许可证取决于发布包实际使用的 Qt
发行版。Airan 发布包保持 Qt 可替换，允许为调试 LGPL 库修改而进行逆向工程，并包含
仓库中的 `Qt-LGPLv3.txt` 和 `Qt-GPLv3.txt` 许可证文本。

## OpenSSL 运行库

Windows 包可能包含供 Qt TLS 使用的动态加载 OpenSSL 运行库。包含这些 DLL 时，
发布包同时包含所配置 OpenSSL 安装中的许可证文件。

## spdlog、fmt 和 libvterm

spdlog 及其内置 fmt 依赖按 MIT 许可证分发。libvterm 也按 MIT 许可证分发。
每个常规发布包都会在 `licenses/` 下包含完整版权和许可文本。

## libva 运行库

Linux 发布包可能包含从官方 `intel/libva` 源码树构建的 libva 共享运行库。随包
libva 二进制作为 `/opt/airan-desk/lib` 下的私有运行时加载库，使 FFmpeg VAAPI/QSV
探测可以使用满足随包 FFmpeg 构建符号要求的 libva 版本。

包含该运行时时，发布包会标明 libva 2.24.0 提交
`add80723247b8031fb8de14d8f599923d3759242`，并包含其 MIT `COPYING`、源码归档
URL、构建配置和 SHA-256 清单。

## PipeWire 运行库

Linux portable 包包含从上游 PipeWire 源码归档构建的 PipeWire 0.3.65 客户端库、
模块、配置和 SPA 支持。PipeWire 按 MIT 许可证分发。发布包包含上游 `COPYING`，
对应源码见 <https://github.com/PipeWire/pipewire/tree/0.3.65>。
