# airan-desk

## Legal Notice

Airan-Desk is a cross-platform remote desktop administration tool intended only for the device owner or an administrator explicitly authorized by the owner.

- Do not install or use this software on another person's device for unauthorized remote access.
- Do not use this software to steal data or invade another person's privacy.
- Remote control without the device user's informed consent may violate Article 285 of the Criminal Law of the People's Republic of China, the Personal Information Protection Law of the People's Republic of China, Article 29 of the Cybersecurity Law of the People's Republic of China, the EU GDPR, the US CFAA, and other applicable local laws.
- Users are solely responsible for legal consequences arising from violations of these requirements.
- The software is provided "as is." To the maximum extent permitted by applicable law, the author is not liable for direct or indirect loss caused by unlawful, unauthorized, or improper use.

### Security Mechanisms That Cannot Be Disabled

- **Mandatory password authentication:** Every remote connection requires a non-empty password. There is no empty-password, authentication-bypass, or passwordless switch.
- **Audit logging:** Connection activity, desktop sessions, file operations and transfers, identifiable ordinary-shell commands, disconnects, and session summaries are written to the controlled device's fixed audit directory. Logs are created daily, durably flushed per record, retained for at least six calendar months, and cannot be disabled or deleted remotely.
- **Controlled-side notification:** In a graphical session where device-owner authorization has not yet been accepted, application startup first shows a ten-second review countdown; this is not a connection notification. After a remote session is established, the application shows an approximately five-second system-tray notification and keeps the active status and session menu visible for the duration of the connection. The local user can disconnect one session, every session from one device, or all connections. The application provides no setting that disables the tray status or session controls; whether the notification balloon appears still depends on operating-system notification settings.
- **Headless operation:** Without a graphical session, a local administrator must configure an executable notification script that succeeds within ten seconds. A missing script, start failure, timeout, or nonzero exit status rejects the connection.
- **Local-only administration:** Controlled access, passwords, notification scripts, and audit-log management can be changed only on the controlled device.

[简体中文](README.md) | English

airan-desk is a remote desktop control application built on Qt 5/6 and the Google WebRTC Native API. It supports desktop streaming, remote keyboard and mouse control, file transfer, and a remote terminal. The current desktop targets are Windows, Linux, and macOS.

## Security and Acceptable Use Notice

> [!IMPORTANT]
> **This project does not provide a public signaling, STUN, TURN, SFU, or other relay service.** Users must deploy their own private signaling server (see [wxalh/signal-server](https://github.com/wxalh/signal-server)) and select, deploy, and administer any required ICE infrastructure.

- A fresh installation does not connect to an author-operated or preset third-party server. Signaling and ICE addresses are empty until the local user explicitly configures them.
- Both `ws://` and `wss://` are supported. Use `wss://` on the public Internet or any other untrusted network. Use `ws://` only on a trusted LAN, through a VPN, or inside another encrypted transport.
- **Never blindly trust or use other signaling server addresses found online.** Do not use an address obtained from search results, forums, group chats, tutorials, or other third-party sources unless you can verify the operator and fully trust its security practices. An unknown or untrusted server may expose connection information, cause connections to the wrong party, enable unauthorized access, or result in the loss of data, accounts, or other assets on your devices.
- You may use this software only with devices and systems that you own or are explicitly authorized to access. **Using this software for any unlawful or criminal activity is strictly prohibited**, including unauthorized access to or control of another person's device, invasion of privacy, theft or destruction of data, distribution of malware, or any other violation of applicable law.
- You are responsible for confirming authorization, complying with applicable law, and accepting the consequences of your server selection, deployment configuration, connection targets, and actions. To the maximum extent permitted by applicable law, the author is not liable for any direct or indirect loss resulting from unlawful or unauthorized use, third-party servers, misconfiguration, or failure to follow this notice.
- Configuration, identity, log, cache, and license files use the operating system's normal filesystem permissions. When the program directory is not writable, ordinary configuration and logs fall back to the user data directory. The remote protocol does not provide an interface for deleting local audit files on the controlled device.

This notice is not legal advice and does not replace your own compliance obligations.

See [PRIVACY.md](PRIVACY.md) for privacy and local-data handling details.

## Overview

- Built with **Qt 5/6 + Google WebRTC static libraries + spdlog + libvterm**.
- Media transport, data channels, bandwidth estimation, loss recovery, key-frame requests, and audio devices are handled by libwebrtc.
- File transfer and input control run over WebRTC DataChannel.
- The remote terminal uses Windows ConPTY or Linux/macOS forkpty, with native `libvterm + Qt Widgets` rendering for full-screen TUI programs.
- spdlog, libvterm, and Google WebRTC are linked as static dependencies. Qt is still deployed as official dynamic libraries.
- Windows OpenSSL runtime deployment is optional: CMake probes it by default and copies `libssl-*.dll` / `libcrypto-*.dll` when found. Missing OpenSSL DLLs do not block the build.

> The project requires **CMake 3.21+**. Build instructions use `CMakePresets.json`; override local paths with `CMakeUserPresets.json`.

## Current WebRTC Architecture

The project now uses the Google WebRTC static package under `third_party/webrtc`.

Large binary dependencies are not committed. Keep `third_party/webrtc` and
`third_party/ffmpeg-builds` locally; see `doc/dependencies.en.md` for download
locations and directory conventions.

Windows packages are built with `is_clang=true`, but they use `clang-cl` with the MSVC ABI and MSVC STL/CRT. They can link with official MSVC Qt binaries as long as the architecture matches: `x86` with `x86`, `x64` with `x64`, and `arm64` with `arm64`. Do not mix MinGW Qt with MSVC/clang-cl WebRTC artifacts.

Linux packages default to the GNU libstdc++ ABI. CI publishes an x64 `GLIBC_2.17` package and x64/arm64/armhf `GLIBC_2.27` packages. Use the 2.17 package on x64 systems with glibc 2.17-2.26, and prefer the 2.27 package on systems with glibc 2.27 or newer. See `doc/build_linux.en.md` for distro examples and limitations.

## Platform Capability Matrix

Each WebRTC slice carries its actual `args.gn`, package checksum, and exact
source revision. This metadata and the packaged static library record the
built-in capabilities of the monolithic WebRTC slice. Airan-Desk does not
separately bundle a user-imported Cisco OpenH264 binary.

| Platform | WebRTC package | Desktop capture | Codec status |
| --- | --- | --- | --- |
| Windows 10/11 x64/arm64 | `win10_x64_m144_md` / `win10_arm64_m144_md` | libwebrtc selects WGC/DXGI/GDI; the app reports the actual backend from `DesktopFrame::capturer_id()` | Without FFmpeg, WebRTC's built-in VP8/VP9/AV1 software codecs are available. A working FFmpeg shared runtime adds H.264/VP8/VP9/AV1 probes; NVENC/QSV/AMF/D3D11 paths appear only with matching drivers and runtime support |
| Windows 7 x86 | `win7_x86_m109_md` | **GDI only; DXGI and WGC are not compiled or supported** | Without FFmpeg, WebRTC's built-in VP8/VP9/AV1 software codecs are available. A working FFmpeg 7.1 runtime can add the H.264/VP8/VP9/AV1 software probes that open successfully; a user-provided OpenH264 binary is also runtime-probed |
| Linux x64/arm64/armhf | `linux_*_m144_gnu`; CentOS 7 x64 uses `linux_centos7_x64_m144_libcxx` | libwebrtc DesktopCapturer; X11/Wayland behavior depends on the WebRTC package and runtime session | The glibc 2.17 package does not bundle FFmpeg and uses WebRTC's built-in VP8/VP9/AV1 software codecs. The glibc 2.27 packages add H.264/VP8/VP9/AV1 when the FFmpeg runtime probe succeeds; VAAPI/V4L2/CUDA paths depend on drivers and devices |
| macOS x64/arm64 | `macos_x64_m144` / `macos_arm64_m144` | libwebrtc DesktopCapturer; ScreenCaptureKit behavior depends on OS permissions and runtime session | Without FFmpeg, WebRTC's built-in VP8/VP9/AV1 software codecs are available. A working FFmpeg runtime adds the H.264/VP8/VP9/AV1 probes that open successfully; hardware paths depend on the actual runtime and device |

Notes:

- Windows 10/11 packages enable `RTC_ENABLE_WIN_WGC=1`, but the final WGC/DXGI/GDI choice is made by libwebrtc's capturer logic and the runtime environment. Windows 7 builds contain only the GDI capture path and do not support DXGI or WGC.
- The UI capture method comes from `capturer_id()` on captured frames, not from parsing WebRTC logs.
- The UI encoder/decoder names come from WebRTC RTCStats `encoder_implementation` / `decoder_implementation`, not from parsing logs.
- `HardwareFirstVideoEncoderFactory` and `HardwareFirstVideoDecoderFactory` retain a dynamic FFmpeg backend. `AIRAN_ENABLE_FFMPEG_RUNTIME` defaults to `ON`; a codec is reported only after its FFmpeg files, codec, and hardware probe succeed. Otherwise the application falls back to WebRTC's built-in VP8/VP9/AV1. A Cisco OpenH264 binary is obtained separately by the user and remains under user enable/disable control.

## Build Docs

- [Windows build guide](doc/build_win.en.md)
- [Linux build guide](doc/build_linux.en.md)
- [macOS build guide](doc/build_mac.en.md)

## Usage

### Before Launching

1. Enter the program output directory.
2. Make sure these resource directories exist:
   - `conf/`
   - `locale/`
3. Edit `conf/common.ini` and configure at least:
   - `signal_server.wsUrl`

Example:

```ini
[signal_server]
wsUrl = wss://your-signal-server.example/ws
```

### Language

The UI language is configured in `conf/common.ini`:

```ini
[local]
language = auto
```

Supported values:

- `auto`: follow the system language, using Simplified Chinese for Chinese systems and English otherwise.
- `zh_CN`: Simplified Chinese.
- `en_US`: English.

### Launching

- Windows: run `release\airan-desk.exe`
- Linux: run `./airan-desk` from the output directory
- macOS: prefer running `airan-desk.app`

### Basic Workflow

1. Start the controlled-side and controller-side programs.
2. Make sure both sides can access the same signaling server.
3. On the controller side, enter the target connection information and start the connection.
4. After the connection succeeds, you can view the remote desktop and control keyboard and mouse input.
5. To transfer files, open the file transfer window and send files from there.
6. To use a remote command line, choose Terminal mode. Full-screen TUI programs such as `vim`, `top`, and `htop` are handled through PTY/ConPTY plus the native terminal emulator.

## Dependencies

Main dependencies:

- [Qt](https://www.qt.io/): cross-platform GUI, networking, WebSocket, and localization foundation.
- Google WebRTC: PeerConnection, DataChannel, audio/video capture and transport, congestion control, audio devices, and RTP/RTCP support.
- [spdlog](https://github.com/gabime/spdlog): logging library, currently linked statically.
- [libvterm](https://github.com/neovim/libvterm): terminal escape sequence parsing and screen state handling.

The remote terminal UI is a native Qt Widgets component and does not depend on `QtWebEngine`.

Third-party source dependencies are managed with Git submodules. After cloning or when updating dependencies, run:

```bash
git submodule update --init --recursive
```

## License

Original Airan-Desk code is available under the [Mozilla Public License 2.0 (MPL-2.0)](LICENSE). A release configuration uses FFmpeg as replaceable dynamic GNU Lesser General Public License libraries. Airan-Desk does not separately bundle a user-imported Cisco OpenH264 binary. The actual build arguments for a monolithic WebRTC slice are recorded in the packaged `WebRTC-args.gn`. See [THIRD_PARTY_SOURCE_OFFER.md](THIRD_PARTY_SOURCE_OFFER.md) and the package's `licenses/` directory for third-party terms, exact source locations, and library replacement information. H.264 patent scope depends on use and jurisdiction; this documentation is not a promise of absolute compliance or zero patent risk.
