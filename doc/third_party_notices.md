# Third-Party Notices

[简体中文](third_party_notices.zh-CN.md) | English

## WebRTC-Derived Desktop Capture Source

Files under `src/desktop_capture/` are forked from the Google WebRTC
`modules/desktop_capture` tree for Airan-owned capture backends and baseline
compatibility work. The copied files keep their original WebRTC copyright
headers and BSD-style license provenance.

Binary packages include the WebRTC copyright and license notices, including the
additional patent grant notice referenced by the copied source headers.

## FFmpeg Runtime Libraries

Airan uses FFmpeg as replaceable LGPL shared runtime libraries. Product packages
use LGPL-compatible shared FFmpeg builds without `--enable-gpl` or
`--enable-nonfree`.

Release packages identify the exact FFmpeg build and include the applicable
FFmpeg LGPL license and copyright notices, corresponding source location, and
build configuration. Airan modifications to FFmpeg are distributed under the
applicable LGPL terms.

## Optional Cisco OpenH264 Binary

OpenH264 Video Codec provided by Cisco Systems, Inc.

Airan-Desk does not separately distribute, mirror, or cache the user-imported
Cisco OpenH264 binary used by the optional FFmpeg runtime path. A user may
separately download an official Cisco binary to the user's device, import it
after installation, and enable, disable, or re-enable it independently. This
statement does not describe the contents of a monolithic WebRTC static slice.
The packaged `WebRTC-args.gn` records that slice's build arguments. The
application packages Cisco's license text, not a separate Cisco codec binary.
The complete authoritative notice is available in source at
`third_party/licenses/Cisco-OpenH264-BINARY_LICENSE.txt` and in a binary package
at `licenses/Cisco-OpenH264-BINARY_LICENSE.txt`.

Cisco's AVC patent license is limited by the terms in that notice. It does not
grant or imply a license for every other use, including every remunerated or
commercial use by content providers and broadcasters. Patent scope and other
obligations depend on the use and jurisdiction; this notice is not a guarantee
of absolute compliance or zero patent risk.

## Qt Runtime Libraries

Airan dynamically links the selected Qt 5 or Qt 6 distribution. The applicable
Qt license depends on the exact Qt distribution used for a package. Airan
packages keep Qt replaceable, permit reverse engineering for debugging
modifications to the LGPL library, and include the repository's
`Qt-LGPLv3.txt` and `Qt-GPLv3.txt` license texts.

## OpenSSL Runtime Libraries

Windows packages may include dynamically loaded OpenSSL runtime libraries for
Qt TLS support. When those DLLs are present, the package includes the license
file from the configured OpenSSL installation.

## spdlog, fmt, and libvterm

spdlog and its bundled fmt dependency are distributed under the MIT license.
libvterm is also distributed under the MIT license. Their full copyright and
permission notices are included in every normal package under `licenses/`.

## libva Runtime Libraries

Linux release packages may include libva shared runtime libraries built from
the official `intel/libva` source tree. The bundled libva binaries are used as
private runtime loader libraries under `/opt/airan-desk/lib` so FFmpeg VAAPI/QSV
probing can run against a libva version with the symbols required by the
packaged FFmpeg build.

Release packages identify libva 2.24.0 commit
`add80723247b8031fb8de14d8f599923d3759242`, and include its MIT `COPYING` text,
source archive URL, build configuration, and SHA-256 manifest when that runtime
is included.

## PipeWire Runtime Libraries

Portable Linux packages include PipeWire 0.3.65 client libraries, modules,
configuration, and SPA support built from the upstream PipeWire source archive.
PipeWire is distributed under the MIT license. The package includes the
upstream `COPYING` file, and the corresponding source is available from
<https://github.com/PipeWire/pipewire/tree/0.3.65>.
