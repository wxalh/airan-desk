# airan-desk

## 法律声明

Airan-Desk 是跨平台远程桌面管理工具，仅供设备所有者或经其明确授权的管理员使用。

- 严禁将本软件安装于他人设备上进行未经授权的远程访问。
- 严禁利用本软件窃取他人数据、侵犯他人隐私。
- 未经设备使用者知情同意的远程控制行为，可能违反《中华人民共和国刑法》第 285 条、《中华人民共和国个人信息保护法》、《中华人民共和国网络安全法》第 29 条、欧盟 GDPR、美国 CFAA 及所在地区的其他法律法规。
- 使用者因违反上述规定产生的一切法律责任由使用者自行承担。
- 本软件按“原样”提供；在适用法律允许的最大范围内，作者不承担因违法、未经授权或不当使用产生的直接或间接责任。

### 不可关闭的安全机制

- **强制密码认证：** 所有远程连接必须通过非空密码认证，不存在空密码、跳过认证或免密开关。
- **审计日志：** 连接、桌面会话、文件操作与传输、普通 shell 中可识别的命令、断开和会话汇总写入被控端固定审计目录。日志按天生成、逐条持久化、至少保留六个自然月，远程端不能关闭或删除。
- **被控提示：** 有图形界面且尚未确认设备所有者授权时，程序启动会先显示 10 秒审阅倒计时；该倒计时不是连接提示。远程会话建立后，程序通过系统托盘显示约 5 秒通知，并在连接期间持续显示活动状态和会话菜单；本地用户可断开单个会话、某个设备的全部会话或全部连接。程序不提供关闭托盘状态和会话控制项的配置；通知气泡是否显示仍受操作系统通知设置影响。
- **无界面运行：** 被控端没有图形界面时，必须由本地管理员配置可执行且能在 10 秒内成功完成的通知脚本；脚本缺失、启动失败、超时或返回非零状态都会拒绝连接。
- **本地控制：** 被控开关、密码、通知脚本和日志管理只能由被控端本地用户修改。

简体中文 | [English](README.en.md)

airan-desk 是一个基于 Qt 5/6 和 Google WebRTC Native API 的远程桌面控制应用，支持桌面画面传输、远程键鼠控制、文件传输和远程终端。当前面向 Windows、Linux 和 macOS 桌面环境。

## 安全与合规声明

> [!IMPORTANT]
> **本项目不提供公共信令、STUN、TURN、SFU 或其他中继服务。** 使用者必须自行部署私有信令服务器（可参考 [wxalh/signal-server](https://github.com/wxalh/signal-server)），并自行选择、部署和管理所需的 ICE 基础设施。

- 新安装不会连接作者或预设第三方服务器；信令和 ICE 地址默认为空。只有本地用户明确配置服务器后，程序才会尝试建立网络连接。
- 程序支持 `ws://` 和 `wss://`。公网或其他不受信任网络应使用 `wss://`；`ws://` 仅适合可信局域网、VPN 或其他已经提供传输加密的环境。
- **切勿盲目信任或使用网上流传的其他信令服务器地址。** 除非您能够核实服务器运营主体并完全信任其安全措施，否则不要使用来自搜索引擎、论坛、群聊、教程或其他第三方渠道的服务器地址。使用来源不明或不受信任的服务器可能导致连接信息泄露、错误连接、未授权访问，以及设备内资料、账号或其他资产损失。
- 本软件仅可用于您拥有或已经获得明确授权的设备和系统。**严禁使用本软件从事任何违法犯罪活动**，包括但不限于未经授权访问或控制他人设备、侵犯隐私、窃取或破坏数据、传播恶意程序及其他违反适用法律法规的行为。
- 使用者有责任确认连接授权、遵守所在地法律法规，并自行承担其服务器选择、部署配置、连接对象、操作行为及使用后果。在适用法律允许的最大范围内，作者不对因违法或未经授权使用、使用第三方服务器、配置不当或未遵守本声明造成的任何直接或间接损失承担责任。
- 配置、身份、日志、缓存和许可证文件使用操作系统的普通文件权限；程序目录不可写时，普通配置和日志回退到用户数据目录。远程协议不提供删除被控端本地审计文件的接口。

本声明不构成法律意见，也不能替代使用者应履行的合规义务。

隐私和本地数据处理说明见 [PRIVACY.md](PRIVACY.md)。

## 项目简介

- 基于 **Qt 5/6 + Google WebRTC 静态库 + spdlog + libvterm** 实现。
- 媒体、数据通道、带宽估计、丢包恢复、关键帧请求和音频设备由 libwebrtc 接管。
- 文件传输和键鼠控制通过 WebRTC DataChannel 传输。
- 远程终端使用 Windows ConPTY / Linux 和 macOS forkpty，并使用原生 `libvterm + Qt Widgets` 渲染全屏 TUI。
- spdlog、libvterm、Google WebRTC 都按静态依赖接入；Qt 仍按官方动态库部署。
- Windows OpenSSL 运行时为可选依赖：CMake 默认探测，找到 `libssl-*.dll` / `libcrypto-*.dll` 就复制到输出目录，找不到也不会阻断构建。

> 项目要求 **CMake 3.21+**，构建说明统一使用 `CMakePresets.json`。本地路径差异请通过 `CMakeUserPresets.json` 覆盖。

## 当前 WebRTC 架构

使用 `third_party/webrtc` 下的 Google WebRTC 静态预编译包。

大型二进制依赖不入库：`third_party/webrtc` 和 `third_party/ffmpeg-builds` 需要本地下载保留。下载位置与目录约定见 `doc/dependencies.md`。

Windows 包虽然使用 `is_clang=true` 构建，但使用的是 `clang-cl` 的 MSVC ABI 和 MSVC STL/CRT，可以和 MSVC 版 Qt 官方动态库链接。必须保证架构一致：`x86` 对 `x86`，`x64` 对 `x64`，`arm64` 对 `arm64`。不要混用 MinGW Qt 和 MSVC/clang-cl WebRTC 产物。

Linux 包默认使用 GNU libstdc++ ABI。CI 发布 x64 `GLIBC_2.17` 包，以及 x64/arm64/armhf `GLIBC_2.27` 包；glibc 2.17-2.26 的 x64 系统使用 2.17 包，glibc 2.27 及以上系统优先使用 2.27 包。详细发行版对应关系和限制见 `doc/build_linux.md`。

## 平台能力矩阵

每个 WebRTC slice 都随包提供实际 `args.gn`、包校验值和精确 source revision。
这些元数据与随包静态库共同记录 monolithic WebRTC slice 的内置能力。
Airan-Desk 不另行捆绑用户导入的 Cisco OpenH264 二进制。

| 平台 | WebRTC 包 | 桌面采集 | 编解码状态 |
| --- | --- | --- | --- |
| Windows 10/11 x64/arm64 | `win10_x64_m144_md` / `win10_arm64_m144_md` | libwebrtc 自动选择 WGC/DXGI/GDI；程序通过 `DesktopFrame::capturer_id()` 显示实际后端 | 无 FFmpeg 时使用 WebRTC 内置 VP8/VP9/AV1 软件编解码；FFmpeg shared runtime 探测成功后增加 H.264/VP8/VP9/AV1，NVENC/QSV/AMF/D3D11 等硬件路径只在对应驱动和运行库可用时出现 |
| Windows 7 x86 | `win7_x86_m109_md` | **仅 GDI；不编译、不支持 DXGI 和 WGC** | 无 FFmpeg 时使用 WebRTC 内置 VP8/VP9/AV1 软件编解码；FFmpeg 7.1 runtime 可用且探测成功时增加其可用的 H.264/VP8/VP9/AV1 软件路径，用户另行提供的 OpenH264 也必须通过运行时探测 |
| Linux x64/arm64/armhf | `linux_*_m144_gnu`；CentOS 7 x64 使用 `linux_centos7_x64_m144_libcxx` | libwebrtc DesktopCapturer，X11/Wayland 能力取决于 WebRTC 包和运行环境 | glibc 2.17 包不捆绑 FFmpeg，使用 WebRTC 内置 VP8/VP9/AV1 软件编解码；glibc 2.27 包在 FFmpeg runtime 探测成功后增加 H.264/VP8/VP9/AV1，VAAPI/V4L2/CUDA 等硬件路径取决于驱动和设备 |
| macOS x64/arm64 | `macos_x64_m144` / `macos_arm64_m144` | libwebrtc DesktopCapturer；ScreenCaptureKit 行为取决于系统权限和运行时会话 | 无 FFmpeg 时使用 WebRTC 内置 VP8/VP9/AV1 软件编解码；FFmpeg runtime 探测成功后增加其可用的 H.264/VP8/VP9/AV1，硬件路径取决于实际运行库和设备 |

说明：

- Windows 10/11 包启用了 `RTC_ENABLE_WIN_WGC=1`，最终是否使用 WGC 或 DXGI 由 libwebrtc 的 capturer 选择逻辑和系统环境决定；Windows 7 构建不包含这两条路径，只使用 GDI。
- UI 中的采集方式来自 WebRTC 帧回调里的 `capturer_id()`，不是解析 WebRTC 日志。
- UI 中的实际编码器/解码器来自 WebRTC RTCStats 的 `encoder_implementation` / `decoder_implementation`，不是解析日志。
- `HardwareFirstVideoEncoderFactory` / `HardwareFirstVideoDecoderFactory` 保留动态 FFmpeg 后端。`AIRAN_ENABLE_FFMPEG_RUNTIME` 默认为 `ON`；只有对应 FFmpeg shared/dev 文件、编解码器和硬件探测均成功时才会报告该能力，否则降级为 WebRTC 内置 VP8/VP9/AV1。Cisco OpenH264 二进制只能由用户另行从 Cisco 获取并自行启停。

## 构建文档

- [Windows 编译指南](doc/build_win.md)
- [Linux 编译指南](doc/build_linux.md)
- [macOS 编译说明](doc/build_mac.md)

## 使用说明

### 启动前准备

1. 进入程序输出目录。
2. 确认以下资源目录存在：
   - `conf/`
   - `locale/`
3. 编辑 `conf/common.ini`，至少配置：
   - `signal_server.wsUrl`

示例：

```ini
[signal_server]
wsUrl = wss://your-signal-server.example/ws
```

### 多语言

可在 `conf/common.ini` 中配置界面语言：

```ini
[local]
language = auto
```

支持：

- `auto`：跟随系统语言，中文系统使用简体中文，其他系统使用英文。
- `zh_CN`：简体中文。
- `en_US`：英文。

### 启动程序

- Windows：运行 `release\airan-desk.exe`
- Linux：在输出目录运行 `./airan-desk`
- macOS：优先运行 `airan-desk.app`

### 基本使用流程

1. 启动被控端和控制端程序。
2. 确保双方都可以访问同一个信令服务器。
3. 在控制端输入目标连接信息并发起连接。
4. 连接成功后即可查看远程桌面并控制键鼠。
5. 如需传输文件，打开文件传输窗口发送。
6. 如需远程命令行，选择“终端”模式连接；`vim`、`top`、`htop` 等全屏 TUI 通过 PTY/ConPTY 和原生终端模拟器渲染。

## 依赖说明

主要依赖如下：

- [Qt](https://www.qt.io/)：跨平台 GUI、网络、WebSocket 和本地化基础库。
- Google WebRTC：PeerConnection、DataChannel、音视频采集传输、拥塞控制、音频设备和 RTP/RTCP 能力。
- [spdlog](https://github.com/gabime/spdlog)：日志库，当前静态链接。
- [libvterm](https://github.com/neovim/libvterm)：终端控制序列解析与屏幕状态维护。

远程终端前端是 Qt Widgets 原生控件，不依赖 `QtWebEngine`。

第三方源码依赖通过 Git submodule 管理。首次克隆或更新依赖时执行：

```bash
git submodule update --init --recursive
```

## 许可证

作者原创代码按 [Mozilla Public License Version 2.0 (MPL-2.0)](LICENSE) 提供。发行配置中的 FFmpeg 是可替换的动态 GNU Lesser General Public License 共享库；Airan-Desk 不另行捆绑用户导入的 Cisco OpenH264 二进制。monolithic WebRTC slice 的实际构建参数记录在随包的 `WebRTC-args.gn` 中。第三方许可、精确来源及动态库替换说明见 [第三方源码与重新链接说明](THIRD_PARTY_SOURCE_OFFER.zh-CN.md) 和发布包的 `licenses/` 目录。H.264 专利范围取决于具体用途和司法辖区，本说明不构成绝对合规或零风险承诺。
