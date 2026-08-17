# Third-Party Dependencies

This repository does not commit large binary dependency directories. Keep them locally on development and build machines; `.gitignore` ignores them.

License boundary: original Airan-Desk code is licensed under the Mozilla Public License 2.0 (MPL-2.0); third-party components retain their own terms. FFmpeg is not covered by MPL-2.0 and is used as replaceable, dynamically loaded GNU Lesser General Public License shared libraries.

## Source Dependencies

These dependencies are built from repository source or submodules:

- `third_party/spdlog`: static library; tests, examples, and benchmarks are disabled by default.
- `third_party/libvterm`: static library; CMake generates the required encoding include files.

After cloning, run:

```bash
git submodule update --init --recursive
```

## WebRTC Static Package

Select the exact self-contained package for the build target. The preparation
scripts download `libwebrtc-manifest.json` from the latest immutable release,
resolve one asset, and verify its declared size and SHA-256 before replacing
the shared `third_party/webrtc` directory:

```powershell
./tools/prepare_third_party.ps1 -PackageSet windows -WebrtcPackage windows-win10-x64-m144-md
```

```bash
bash ./tools/prepare_third_party.sh "" linux linux-ubuntu18-x64-m144-gnu
```

CMake currently selects targets by platform, architecture, runtime, and build configuration:

| Platform | target |
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

Notes:

- Windows can select `/MD` or `/MT` packages with `WEBRTC_MSVC_RUNTIME=md|mt`; the default is `md`.
- Windows can select the package family with `WEBRTC_WINDOWS_PLATFORM=win10|win7`.
- Linux can select the STL ABI with `WEBRTC_LINUX_STL=gnu|libcxx`; the default is `gnu`.
- Linux selects `ubuntu18` or `centos7` with `WEBRTC_LINUX_COMPAT`; the CentOS 7 package is x64/libc++ only.
- Debug configurations prefer the matching `*_debug` target. If the debug package is missing, CMake falls back to the release target.

WebRTC m144 is sourced from Chromium branch-head 7559:
<https://webrtc.googlesource.com/src/+/refs/branch-heads/7559>. Windows 7 x86
uses m109 branch-head 5414:
<https://webrtc.googlesource.com/src/+/refs/branch-heads/5414>. Verify the
selected platform package against its entry in `libwebrtc-manifest.json`, and
ship `WebRTC-LICENSE.txt`, `WebRTC-PATENTS.txt`, and the platform-slice-generated
`WebRTC-Third-Party-Licenses.txt`. The build scripts come from
<https://github.com/wxalh/libwebrtc_build>.

Each selected slice provides its actual `args.gn`, exact `source_revision.txt`,
and `PACKAGE-METADATA.json`. The preparation scripts verify archive size and
SHA-256 against `libwebrtc-manifest.json` only while downloading. Extraction
cache markers are not part of the WebRTC package interface and are not required
by CMake or release packaging. The build copies the selected slice's build
arguments and source revision into the release directory.

## FFmpeg shared/dev Package

Download LGPL shared/dev packages from:

https://github.com/wxalh/FFmpeg-Builds/releases/tag/latest

CMake checks these default directories:

| Platform | Default directory |
| --- | --- |
| Windows 10/11 x64 | `third_party/ffmpeg-builds/ffmpeg-n8.1-latest-win64-lgpl-shared-8.1` |
| Windows 10/11 arm64 | `third_party/ffmpeg-builds/ffmpeg-n8.1-latest-winarm64-lgpl-shared-8.1` |
| Windows 7 x86 | `third_party/ffmpeg-builds/ffmpeg-n7.1-latest-win32-lgpl-shared-7.1` |
| Linux x64 | `third_party/ffmpeg-builds/ffmpeg-n8.1-latest-linux64-lgpl-shared-8.1` |
| Linux arm64 | `third_party/ffmpeg-builds/ffmpeg-n8.1-latest-linuxarm64-lgpl-shared-8.1` |
| Linux armhf | `third_party/ffmpeg-builds/ffmpeg-n8.1-latest-linuxarmhf-lgpl-shared-8.1` |

Override with the `AIRAN_FFMPEG_ROOT` cache variable from `CMakeUserPresets.json`.

The package directory must contain:

- `include/`
- `lib/`
- Windows also needs `bin/*.dll`
- Linux packages may include `bin/ffmpeg` for command-line use

The FFmpeg codec backend dynamically loads `avutil`, `avcodec`, `avfilter`, and
`swscale`. `AIRAN_ENABLE_FFMPEG_RUNTIME` defaults to `ON`; a development build
emits an explicit warning and falls back to WebRTC's internal codecs when the
complete shared/dev package is absent. Official Windows and Linux packaging
forces the FFmpeg backend on and requires the complete shared runtime, license,
and corresponding source metadata; official macOS packages do not include
FFmpeg. Release configurations use replaceable dynamic LGPL FFmpeg and must not use
`--enable-gpl`, `--enable-nonfree`, or ordinary linked `--enable-libopenh264`.

Windows 10/11 and current Linux packages use FFmpeg `release/8.1`; Windows 7
x86 uses `release/7.1`. Upstream is <https://github.com/FFmpeg/FFmpeg.git>. The
build lineage is <https://github.com/wxalh/FFmpeg-Builds>, including Airan's
OpenH264 runtime-loader patch. A `latest` URL moves and is not an exact source
record. Every shipped FFmpeg binary must be accompanied by its exact FFmpeg
source revision, exact FFmpeg-Builds source revision, complete build
configuration (including configure command/output and target variant),
dependency revisions and licenses, artifact checksum manifest, and durable
source URLs for both repositories. These files are copied into the package
when present; a directory containing only `LICENSE.txt` is still an incomplete
release artifact for users who need the corresponding source materials.

## Optional Cisco OpenH264

FFmpeg-Builds installs only the `wels/` headers from Cisco OpenH264 2.6.0 source
revision `e3f5b10438e2bacc155cf54578222bd4236c9f06`; it does not link or package
an OpenH264 implementation. The source repository is
<https://github.com/cisco/openh264.git>, the official release page is
<https://github.com/cisco/openh264/releases/tag/v2.6.0>, and Cisco's binary
origin is <https://ciscobinary.openh264.org/>.

`src/media/codec/openh264/openh264_release_manifest.h` pins each official file
name, size, URL, and SHA-256 after decompression. For example, the Windows x64
2.6.0 manifest hash is
`2076cb5675ec6c1a4c70e7a2a322552f547b6eeed649d6dfcd9e02a543b24691`;
other platforms must use their own entry in that manifest. Airan-Desk does not
distribute, mirror, or cache a Cisco binary. The user separately downloads,
decompresses, imports, and explicitly enables it, and can disable or re-enable
it. Only a manifest-matching file installed in the application-managed user data
directory is used by `libopenh264`. The user remains free to replace the FFmpeg
and Qt shared libraries; an unlisted OpenH264 implementation is not trusted. See
`third_party/licenses/Cisco-OpenH264-BINARY_LICENSE.txt` for Cisco's complete
terms.

## Qt

Most current desktop presets use Qt 5, while the Windows arm64 preset uses
Qt 6.8.3:

- Required components: `Core`, `Gui`, `Svg`, `Widgets`, `WebSockets`, `Network`
- Optional component: `LinguistTools`, used to build project `.qm` translations
- Linux optional component: `DBus`, possibly needed by Wayland/portal related paths

Qt system translations are auto-detected from:

- `qmake -query QT_INSTALL_TRANSLATIONS`
- `translations` under the Qt install prefix
- `share/qt5/translations` under the Qt install prefix
- `share/qt6/translations` under the Qt install prefix
- `/usr/share/qt5/translations`
- `/usr/share/qt6/translations`
- `/usr/share/qt/translations`
- `/usr/lib/qt5/translations`
- `/usr/lib/qt6/translations`

Override with the `AIRAN_QT_TRANSLATIONS_DIR` cache variable from `CMakeUserPresets.json` when needed.

## Linux Compiler

The project requires CMake 3.21+ on Linux, and the current WebRTC m144 headers require GCC/G++ 11+. Ubuntu 18.04 defaults to `g++` 7.5, which fails in WebRTC C++20 headers. Override `CMAKE_C_COMPILER` and `CMAKE_CXX_COMPILER` from `CMakeUserPresets.json` when the default compiler is too old.

Ubuntu 18.04 can install GCC/G++ 11 from the Ubuntu Toolchain PPA:

```bash
sudo apt install software-properties-common
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt update
sudo apt install gcc-11 g++-11
```

## OpenSSL

OpenSSL is auto-detected only for Windows deployment, where it supports Qt TLS/HTTPS/WSS at runtime. It is not a hard build requirement.

Default behavior:

- `AIRAN_DEPLOY_OPENSSL_RUNTIME=ON`
- Search for `libssl-*.dll` and `libcrypto-*.dll`
- Copy found DLLs next to `airan-desk.exe`
- Missing DLLs only produce a status message and do not block the build

Override the explicit path with the `OPENSSL_ROOT_DIR` cache variable from `CMakeUserPresets.json`.

## Codec SDK Policy

The current mainline uses FFmpeg as the unified path for NVENC, QSV/oneVPL, AMF, VAAPI, V4L2 mem2mem, and related backends. Runtime machines still need the corresponding GPU drivers and system runtimes.
