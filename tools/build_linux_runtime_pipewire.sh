#!/usr/bin/env bash
set -euo pipefail

pipewire_version="${AIRAN_PIPEWIRE_VERSION:-0.3.65}"
pipewire_sha256="${AIRAN_PIPEWIRE_SHA256:-bb76f938136d0ce8c35bffa99e002dc2dbaeab5e14c6c34154e7f750013d1d6b}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
download_dir="$repo_root/third_party/_downloads"
archive="$download_dir/pipewire-$pipewire_version.tar.gz"
source_dir="$repo_root/out/linux-runtime-build/pipewire-$pipewire_version-source"
build_dir="$repo_root/out/linux-runtime-build/pipewire-$pipewire_version-build"
sdk_prefix="$repo_root/third_party/linux-runtime/pipewire-sdk"
runtime_lib_dir="$repo_root/third_party/linux-runtime/lib"
marker="$sdk_prefix/.airan-pipewire-$pipewire_version"

for tool in curl meson ninja sha256sum tar; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Required PipeWire build tool was not found: $tool" >&2
    exit 1
  fi
done

if [[ -f "$marker" && -e "$sdk_prefix/lib/libpipewire-0.3.so" && \
      -d "$runtime_lib_dir/pipewire-0.3" && -d "$runtime_lib_dir/spa-0.2" && \
      -d "$runtime_lib_dir/pipewire-config" ]]; then
  echo "PipeWire $pipewire_version runtime is already prepared"
  exit 0
fi

mkdir -p "$download_dir" "$repo_root/out/linux-runtime-build" "$runtime_lib_dir"
if [[ ! -f "$archive" ]]; then
  curl -L --fail --retry 5 --retry-delay 5 \
    -o "$archive.tmp" \
    "https://github.com/PipeWire/pipewire/archive/refs/tags/$pipewire_version.tar.gz"
  mv -f "$archive.tmp" "$archive"
fi

actual_sha256="$(sha256sum "$archive" | awk '{print tolower($1)}')"
if [[ "$actual_sha256" != "$pipewire_sha256" ]]; then
  echo "PipeWire source checksum mismatch: expected $pipewire_sha256, got $actual_sha256" >&2
  exit 1
fi

rm -rf "$source_dir" "$build_dir" "$sdk_prefix"
mkdir -p "$source_dir" "$build_dir" "$sdk_prefix"
tar -xzf "$archive" -C "$source_dir" --strip-components=1

meson setup "$build_dir" "$source_dir" \
  --prefix="$sdk_prefix" \
  --libdir=lib \
  --buildtype=release \
  -Ddocs=disabled \
  -Dexamples=disabled \
  -Dman=disabled \
  -Dtests=disabled \
  -Dinstalled_tests=disabled \
  -Dgstreamer=disabled \
  -Dsystemd=disabled \
  -Dpipewire-alsa=disabled \
  -Dpipewire-jack=disabled \
  -Dpipewire-v4l2=disabled \
  -Djack-devel=false \
  -Dspa-plugins=enabled \
  -Dalsa=disabled \
  -Daudiomixer=enabled \
  -Daudioconvert=enabled \
  -Dbluez5=disabled \
  -Davb=disabled \
  -Dcontrol=disabled \
  -Daudiotestsrc=disabled \
  -Djack=disabled \
  -Dsupport=enabled \
  -Dv4l2=disabled \
  -Dlibcamera=disabled \
  -Dvideoconvert=enabled \
  -Dvideotestsrc=disabled \
  -Ddbus=disabled \
  -Dpw-cat=disabled \
  -Dudev=disabled \
  -Dsdl2=disabled \
  -Dsndfile=disabled \
  -Dlibpulse=disabled \
  -Davahi=disabled \
  -Decho-cancel-webrtc=disabled \
  -Dlibusb=disabled \
  -Dsession-managers=[] \
  -Dx11=disabled \
  -Dlibcanberra=disabled \
  -Dlegacy-rtkit=false \
  -Dflatpak=disabled \
  -Dreadline=disabled \
  -Dgsettings=disabled

meson compile -C "$build_dir"
meson install -C "$build_dir"

mkdir -p "$sdk_prefix/share/licenses/pipewire"
install -m 0644 "$source_dir/COPYING" "$sdk_prefix/share/licenses/pipewire/COPYING"

rm -rf "$runtime_lib_dir/pipewire-0.3" "$runtime_lib_dir/spa-0.2" \
  "$runtime_lib_dir/pipewire-config"
find "$runtime_lib_dir" -maxdepth 1 \( -type f -o -type l \) \
  -name 'libpipewire-0.3.so*' -delete

while IFS= read -r library; do
  cp -a "$library" "$runtime_lib_dir/"
done < <(find "$sdk_prefix/lib" -maxdepth 1 \( -type f -o -type l \) \
  -name 'libpipewire-0.3.so*' -print)
cp -a "$sdk_prefix/lib/pipewire-0.3" "$runtime_lib_dir/pipewire-0.3"
cp -a "$sdk_prefix/lib/spa-0.2" "$runtime_lib_dir/spa-0.2"
cp -a "$sdk_prefix/share/pipewire" "$runtime_lib_dir/pipewire-config"

touch "$marker"
echo "Prepared PipeWire $pipewire_version SDK: $sdk_prefix"
echo "Prepared PipeWire $pipewire_version runtime: $runtime_lib_dir"
