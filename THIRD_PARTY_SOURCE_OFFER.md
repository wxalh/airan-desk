# Third-Party Source And Relinking Information

[简体中文](THIRD_PARTY_SOURCE_OFFER.zh-CN.md) | English

Original Airan-Desk source code is licensed under the Mozilla Public License
Version 2.0 (MPL-2.0); the complete text is in `LICENSE`. This document records
the source, license, and relinking information for third-party components used
by a binary package built from this repository. The terms for each third-party
component remain those stated by its copyright holders.

## Qt Runtime Libraries and Corresponding Source

Airan-Desk dynamically links Qt shared libraries under the GNU Lesser General
Public License version 3. The official Windows x64 and Windows 7 x86 workflows
use Qt 5.15.2, and the Windows arm64 workflow uses Qt 6.8.3. The local Windows
7 x86 preset also supports Qt 5.9.9. Linux workflows install Qt from the target
distribution's `apt` or `yum` repositories, while macOS workflows install
Homebrew `qt@5`; their exact package versions and downstream patches are
therefore determined by the build environment. Packages keep Qt libraries as
separate replaceable files and do not prohibit reverse engineering for
debugging modifications to Qt.

Corresponding upstream source for the fixed Windows versions:

Qt official download and source index: <https://download.qt.io/>

<https://download.qt.io/archive/qt/5.15/5.15.2/single/qt-everywhere-src-5.15.2.tar.xz>

<https://download.qt.io/archive/qt/5.9/5.9.9/single/qt-everywhere-opensource-src-5.9.9.tar.xz>

<https://download.qt.io/official_releases/qt/6.8/6.8.3/single/qt-everywhere-src-6.8.3.tar.xz>

The applicable LGPLv3 and GPLv3 texts are included in `licenses/`. Source for
official Qt binaries is available from the Qt links above. Linux distribution
packages use the matching source packages and patches from their `apt` or `yum`
repositories. The Homebrew `qt@5` formula and its source references are
available from <https://formulae.brew.sh/formula/qt@5>.

## WebRTC m109 and m144

Airan-Desk uses milestone-specific Google WebRTC static libraries built by
`wxalh/libwebrtc_build`. Each packaged slice includes its actual `args.gn`,
package checksum, and exact `source_revision.txt`. This metadata and the static
library contents identify the slice's H.264/FFmpeg/OpenH264 content. WebRTC and
the linked third-party components are under BSD-style, MIT, ISC, and other
licenses reproduced in:

- `licenses/WebRTC-LICENSE.txt`
- `licenses/WebRTC-PATENTS.txt`
- `licenses/WebRTC-Third-Party-Licenses.txt`

Source provenance:

- WebRTC m109: <https://webrtc.googlesource.com/src/+/refs/branch-heads/5414>
- WebRTC m144: <https://webrtc.googlesource.com/src/+/refs/branch-heads/7559>
- Build scripts: <https://github.com/wxalh/libwebrtc_build>

The package records the exact GN arguments in `WebRTC-args.gn`, its archive
checksum in `WebRTC-package.sha256`, and the exact source revision in
`WebRTC-source-revision.txt`. All three files appear beside the notices.

## OpenSSL

Windows x64/x86 presets may include replaceable OpenSSL shared libraries used
by Qt TLS when a compatible dynamic installation is detected. The Windows
arm64 official workflow disables this optional deployment. When OpenSSL runtime
DLLs are included, their license is available as
`licenses/OpenSSL-LICENSE.txt`. The configured `OPENSSL_ROOT_DIR` identifies
the exact OpenSSL version; upstream releases are available from
<https://www.openssl.org/source/>.

## FFmpeg shared runtime

FFmpeg is not covered by Airan-Desk's MPL-2.0 license. When a release package
includes FFmpeg, it uses separately replaceable, dynamically loaded shared
libraries built under the GNU Lesser General Public License. Packaged builds
exclude `--enable-gpl` and `--enable-nonfree`; recipients may replace the shared
libraries, and the package imposes no restriction on reverse engineering needed
to debug changes to an LGPL library.

Windows 10/11 and current Linux package inputs follow FFmpeg `release/8.1`;
Windows 7 x86 follows `release/7.1`. Upstream source is
<https://github.com/FFmpeg/FFmpeg.git>, and the build lineage, including Airan's
OpenH264 runtime-loader patch, is
<https://github.com/wxalh/FFmpeg-Builds>. A moving `latest` download is not an
exact source record. Packages containing FFmpeg include the exact FFmpeg and
FFmpeg-Builds source revisions, complete build configuration, dependency
revisions and licenses, checksum manifest, and durable source URLs.

## Optional Cisco OpenH264 binary

FFmpeg-Builds uses only the `wels/` headers from OpenH264 2.6.0 source revision
`e3f5b10438e2bacc155cf54578222bd4236c9f06`; it does not link or package an
OpenH264 implementation. The source release is
<https://github.com/cisco/openh264/releases/tag/v2.6.0>, and official binaries
originate at <https://ciscobinary.openh264.org/>. The application manifest pins
each accepted filename, size, URL, and decompressed SHA-256; for example, the
Windows x64 2.6.0 manifest hash is
`2076cb5675ec6c1a4c70e7a2a322552f547b6eeed649d6dfcd9e02a543b24691`.

Airan-Desk does not distribute, mirror, or cache a Cisco OpenH264 binary. The
end user separately downloads and imports it and controls whether it is
enabled, disabled, or re-enabled. Cisco's complete terms are included at
`licenses/Cisco-OpenH264-BINARY_LICENSE.txt` in a package and at
`third_party/licenses/Cisco-OpenH264-BINARY_LICENSE.txt` in source. Cisco's AVC
patent license is limited by those terms and is not a blanket assurance for all
commercial, remunerated, content-provider, broadcaster, or jurisdiction-specific
uses.

## Other components

spdlog, fmt, libvterm, PipeWire, libva, and other packaged runtime components
are identified in `licenses/Third-Party-Notices.md`. For libva, packages include
the exact 2.24.0 source commit, pinned source archive URL, MIT `COPYING` text,
build configuration, and SHA-256 manifest beside that notice. Their complete
license texts are included when the corresponding component is present.
