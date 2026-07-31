#!/usr/bin/env bash
set -euo pipefail

release_dir=""
package_name=""
glibc_floor="2.27"
include_ffmpeg="ON"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --release-dir) release_dir="$2"; shift 2 ;;
    --package-name) package_name="$2"; shift 2 ;;
    --glibc-floor) glibc_floor="$2"; shift 2 ;;
    --include-ffmpeg) include_ffmpeg="$2"; shift 2 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [[ -z "$release_dir" || -z "$package_name" ]]; then
  echo "--release-dir and --package-name are required" >&2
  exit 2
fi
if [[ ! "$package_name" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
  echo "Portable package name contains unsupported characters: $package_name" >&2
  exit 2
fi
if [[ ! "$glibc_floor" =~ ^[0-9]+\.[0-9]+$ ]]; then
  echo "Invalid glibc compatibility floor: $glibc_floor" >&2
  exit 2
fi
if [[ "$include_ffmpeg" != "ON" && "$include_ffmpeg" != "OFF" ]]; then
  echo "--include-ffmpeg must be ON or OFF" >&2
  exit 2
fi

for tool in cmake file ldd patchelf readelf realpath tar; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Required portable packaging tool was not found: $tool" >&2
    exit 1
  fi
done

qmake_command="${QMAKE:-qmake}"
if ! command -v "$qmake_command" >/dev/null 2>&1; then
  echo "qmake was not found; set QMAKE to the qmake used for this build" >&2
  exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
release_dir="$(cd "$release_dir" && pwd)"
source_executable="$release_dir/airan-desk"
if [[ ! -x "$source_executable" ]]; then
  echo "Linux release executable was not found: $source_executable" >&2
  exit 1
fi
source_launcher="$release_dir/airan-desk-wrapper.sh"
if [[ ! -f "$source_launcher" ]]; then
  echo "Linux launcher was not found: $source_launcher" >&2
  exit 1
fi

linked_qt_major="$(ldd "$source_executable" 2>/dev/null |
  sed -n 's/.*libQt\([56]\)Core\.so.*/\1/p' | head -n 1)"
if [[ -z "$linked_qt_major" ]]; then
  echo "Could not determine the Qt major version linked by: $source_executable" >&2
  exit 1
fi

qmake_query() {
  if [[ -z "${QMAKE:-}" ]]; then
    QT_SELECT="qt$linked_qt_major" "$qmake_command" -query "$1"
  else
    "$qmake_command" -query "$1"
  fi
}

qmake_qt_major="$(qmake_query QT_VERSION | cut -d. -f1)"
if [[ "$linked_qt_major" != "$qmake_qt_major" ]]; then
  echo "qmake Qt$qmake_qt_major does not match the executable Qt$linked_qt_major" >&2
  exit 1
fi

portable_root="$repo_root/out/portable"
portable_dir="$portable_root/$package_name"
lib_dir="$portable_dir/lib"
plugin_dir="$portable_dir/plugins"
package_dir="$repo_root/out/packages"
package_path="$package_dir/$package_name.tar.gz"

temporary_package_path=""
inspection_dir=""
package_published=false
cleanup_package_artifacts() {
  if [[ -n "$temporary_package_path" ]]; then
    rm -f "$temporary_package_path"
  fi
  if [[ -n "$inspection_dir" ]]; then
    rm -rf "$inspection_dir"
  fi
  if [[ "$package_published" != true ]]; then
    rm -rf "$portable_dir"
  fi
}
trap cleanup_package_artifacts EXIT

rm -rf "$portable_dir"
mkdir -p "$lib_dir" "$plugin_dir" "$package_dir"
install -m 0755 "$source_executable" "$portable_dir/airan-desk.real"

copy_tree_if_present() {
  local source="$1"
  local destination="$2"
  if [[ -d "$source" ]]; then
    mkdir -p "$destination"
    cp -aL "$source/." "$destination/"
  fi
}

copy_library_file() {
  local source="$1"
  local destination
  destination="$lib_dir/$(basename "$source")"
  if [[ -e "$destination" ]]; then
    if ! cmp -s "$(readlink -f "$source")" "$destination"; then
      echo "Portable dependency basename collision: $source and $destination" >&2
      exit 1
    fi
    return 1
  fi
  cp -L "$source" "$destination"
  chmod u+w "$destination"
  return 0
}

# Preserve project-provided private runtimes before resolving the remaining
# dependency closure.
while IFS= read -r runtime_library; do
  copy_library_file "$runtime_library" || true
done < <(find "$release_dir" -maxdepth 1 \( -type f -o -type l \) -name '*.so*' -print)

if [[ -d "$release_dir/lib" ]]; then
  copy_tree_if_present "$release_dir/lib" "$lib_dir"
fi

# Fontconfig and FreeType form a tightly coupled system font stack. Keeping
# only one side private can cause symbol-version failures in the Qt XCB plugin.
find "$lib_dir" -maxdepth 1 \
  \( -name 'libfontconfig.so*' -o -name 'libfreetype.so*' \) -delete

qt_plugin_root="$(qmake_query QT_INSTALL_PLUGINS)"
if [[ ! -d "$qt_plugin_root/platforms" ]]; then
  echo "Qt platform plugins were not found under: $qt_plugin_root" >&2
  exit 1
fi

for category in platforms iconengines imageformats xcbglintegrations bearer platforminputcontexts; do
  copy_tree_if_present "$qt_plugin_root/$category" "$plugin_dir/$category"
done

if ! find "$plugin_dir/platforms" -maxdepth 1 -name 'libqxcb.so' -print -quit | grep -q .; then
  echo "The portable package is missing the Qt XCB platform plugin" >&2
  exit 1
fi

for resource_dir in conf icons locale translations; do
  copy_tree_if_present "$release_dir/$resource_dir" "$portable_dir/$resource_dir"
done

for release_asset in LICENSE README.md README.en.md PRIVACY.md \
  THIRD_PARTY_SOURCE_OFFER.md THIRD_PARTY_SOURCE_OFFER.zh-CN.md; do
  if [[ -f "$release_dir/$release_asset" ]]; then
    install -m 0644 "$release_dir/$release_asset" "$portable_dir/$release_asset"
  fi
done
copy_tree_if_present "$release_dir/script" "$portable_dir/script"
if [[ -f "$portable_dir/script/airan-notify.sh" ]]; then
  chmod 0755 "$portable_dir/script/airan-notify.sh"
fi

for integration_file in \
  install-linux.sh remove-linux.sh airan-desk.desktop airan-desk-autostart.desktop; do
  if [[ -f "$release_dir/$integration_file" ]]; then
    install -m 0644 "$release_dir/$integration_file" "$portable_dir/$integration_file"
  fi
done
chmod 0755 "$portable_dir/install-linux.sh" "$portable_dir/remove-linux.sh"

mkdir -p "$portable_dir/licenses"
if [[ -d "$release_dir/licenses" ]]; then
  cp -a "$release_dir/licenses/." "$portable_dir/licenses/"
fi

if [[ -f "$repo_root/LICENSE" ]]; then
  install -m 0644 "$repo_root/LICENSE" "$portable_dir/licenses/Airan-Desk.txt"
fi
if [[ -f "$repo_root/doc/third_party_notices.md" ]]; then
  install -m 0644 "$repo_root/doc/third_party_notices.md" \
    "$portable_dir/licenses/Third-Party-Notices.md"
fi

required_notices=(
  Third-Party-Notices.md \
  third_party_notices.zh-CN.md \
  Cisco-OpenH264-BINARY_LICENSE.txt \
  WebRTC-LICENSE.txt \
  WebRTC-PATENTS.txt \
  WebRTC-Third-Party-Licenses.txt \
  WebRTC-args.gn \
  WebRTC-package.sha256 \
  WebRTC-source-revision.txt \
  spdlog-LICENSE.txt \
  fmt-LICENSE.rst \
  libvterm-LICENSE.txt \
  Qt-LGPLv3.txt \
  Qt-GPLv3.txt
)
if [[ "$include_ffmpeg" == "ON" ]]; then
  required_notices+=(
    FFmpeg-LICENSE.txt
    FFmpeg-source-revision.txt
    FFmpeg-builds-revision.txt
    FFmpeg-build-configuration.txt
    FFmpeg-dependencies.txt
    FFmpeg-SHA256SUMS
    FFmpeg-SOURCE_URLS.txt
  )
fi
for required_notice in "${required_notices[@]}"; do
  if [[ ! -f "$portable_dir/licenses/$required_notice" ]]; then
    echo "Portable package is missing required license notice: $required_notice" >&2
    exit 1
  fi
done

if [[ "$include_ffmpeg" == "ON" ]]; then
  if ! find "$portable_dir" \( -type f -o -type l \) \
      -name 'libavcodec.so*' -print -quit | grep -q .; then
    echo "Portable package does not contain the required FFmpeg avcodec runtime" >&2
    exit 1
  fi
elif find "$portable_dir" \( -type f -o -type l \) \
    \( -name 'libavcodec.so*' -o -name 'libavdevice.so*' \
       -o -name 'libavfilter.so*' -o -name 'libavformat.so*' \
       -o -name 'libavutil.so*' -o -name 'libswresample.so*' \
       -o -name 'libswscale.so*' \) -print -quit | grep -q .; then
  echo "Portable package unexpectedly contains an FFmpeg runtime" >&2
  exit 1
fi
if [[ -f "$repo_root/third_party/linux-runtime/pipewire-sdk/share/licenses/pipewire/COPYING" ]]; then
  install -m 0644 \
    "$repo_root/third_party/linux-runtime/pipewire-sdk/share/licenses/pipewire/COPYING" \
    "$portable_dir/licenses/PipeWire-COPYING.txt"
fi
if find "$portable_dir/lib" -maxdepth 1 -type f -name 'libva.so*' -print -quit | grep -q .; then
  for libva_notice in \
    libva-LICENSE.txt libva-source-revision.txt libva-source-url.txt \
    libva-build-configuration.txt libva-SHA256SUMS; do
    if [[ ! -f "$portable_dir/licenses/$libva_notice" ]]; then
      echo "Portable package is missing required libva metadata: $libva_notice" >&2
      exit 1
    fi
  done
  libva_revision="$(tr -d '[:space:]' < "$portable_dir/licenses/libva-source-revision.txt")"
  if [[ ! "$libva_revision" =~ ^[0-9a-f]{40}$ ]]; then
    echo "Portable package contains an invalid libva source revision" >&2
    exit 1
  fi
  if ! grep -Eq "^https://github\\.com/intel/libva/archive/${libva_revision}\\.tar\\.gz$" \
      "$portable_dir/licenses/libva-source-url.txt"; then
    echo "Portable package contains an unpinned libva source URL" >&2
    exit 1
  fi
  if ! grep -q "MIT License\|Permission is hereby granted" \
      "$portable_dir/licenses/libva-LICENSE.txt"; then
    echo "Portable package contains an invalid libva license notice" >&2
    exit 1
  fi
  while read -r expected_hash runtime_name; do
    [[ -n "$expected_hash" ]] || continue
    runtime_path="$portable_dir/lib/$runtime_name"
    [[ -f "$runtime_path" ]] || { echo "Missing libva runtime from checksum manifest: $runtime_name" >&2; exit 1; }
    actual_hash="$(sha256sum "$runtime_path" | awk '{print $1}')"
    [[ "$actual_hash" == "$expected_hash" ]] || { echo "libva checksum mismatch: $runtime_name" >&2; exit 1; }
  done < "$portable_dir/licenses/libva-SHA256SUMS"
fi
if command -v dpkg-query >/dev/null 2>&1; then
  for qt_package in qtbase5-dev libqt5svg5-dev libqt5websockets5-dev; do
    copyright_file="$(dpkg-query -L "$qt_package" 2>/dev/null |
      grep '/copyright$' | head -n 1 || true)"
    if [[ -n "$copyright_file" && -f "$copyright_file" ]]; then
      install -m 0644 "$copyright_file" \
        "$portable_dir/licenses/${qt_package}-copyright.txt"
    fi
  done
fi

if [[ -f "$release_dir/60-airan-desk-uinput.rules" ]]; then
  install -m 0644 "$release_dir/60-airan-desk-uinput.rules" \
    "$portable_dir/60-airan-desk-uinput.rules"
fi
if [[ -f "$release_dir/airan-desk-ensure-uinput-rule" ]]; then
  install -m 0755 "$release_dir/airan-desk-ensure-uinput-rule" \
    "$portable_dir/airan-desk-ensure-uinput-rule"
fi

cat >"$portable_dir/qt.conf" <<'EOF'
[Paths]
Plugins = plugins
Translations = translations
EOF

install -m 0755 "$source_launcher" "$portable_dir/airan-desk"

cat >"$portable_dir/README.txt" <<EOF
Airan Desk portable Linux package

Simplified Chinese: README.zh-CN.txt

Run: ./airan-desk
Install: sudo ./install-linux.sh
Compatibility baseline: GLIBC_$glibc_floor

This package contains its Qt runtime and required user-space shared libraries.
Always use ./airan-desk so LD_LIBRARY_PATH resolves the private runtime set.
The Linux kernel, glibc, graphics drivers, desktop services, and hardware drivers
remain system dependencies. The launcher requests administrator privileges when
the uinput rule, kernel module, device permissions, or input-group membership
need configuration.
EOF

cat >"$portable_dir/README.zh-CN.txt" <<EOF
Airan Desk Linux portable 包

English: README.txt

运行：./airan-desk
安装：sudo ./install-linux.sh
兼容基线：GLIBC_$glibc_floor

本包包含 Qt 运行库和所需的用户态共享库。请始终通过 ./airan-desk 启动，
以便 LD_LIBRARY_PATH 正确解析包内私有运行库。Linux 内核、glibc、图形驱动、
桌面服务和硬件驱动仍由系统提供。uinput 规则、内核模块、设备权限或 input 组
成员关系需要配置时，启动器会请求管理员权限。
EOF

is_elf() {
  file -Lb "$1" | grep -q '^ELF '
}

is_system_runtime() {
  case "$1" in
    ld-linux*.so*|ld-*.so*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|\
    librt.so.*|libresolv.so.*|libutil.so.*|libanl.so.*|libnss_*.so.*|\
    libGL.so.*|libEGL.so.*|libGLX.so.*|libOpenGL.so.*|libGLdispatch.so.*|libdrm.so.*|\
    libgbm.so.*|libvulkan.so.*|libcuda.so.*|libfontconfig.so.*|libfreetype.so.*)
      return 0
      ;;
  esac
  return 1
}

dependency_paths() {
  LD_LIBRARY_PATH="$lib_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$1" 2>/dev/null |
    awk '/=> \// { print $3 } /^\// { print $1 }'
}

# Resolve dependencies repeatedly because copied libraries and plugins can add
# their own transitive dependencies.
changed=1
while [[ "$changed" -eq 1 ]]; do
  changed=0
  while IFS= read -r elf_file; do
    is_elf "$elf_file" || continue
    while IFS= read -r dependency; do
      [[ -n "$dependency" && -e "$dependency" ]] || continue
      dependency_name="$(basename "$dependency")"
      if is_system_runtime "$dependency_name"; then
        continue
      fi
      if copy_library_file "$dependency"; then
        changed=1
      fi
    done < <(dependency_paths "$elf_file")
  done < <(find "$portable_dir" -type f -print)
done

if find "$portable_dir" \( -type f -o -type l \) \
    \( -name 'libssl.so*' -o -name 'libcrypto.so*' \) -print -quit | grep -q . &&
   [[ ! -f "$portable_dir/licenses/OpenSSL-LICENSE.txt" ]]; then
  openssl_license=""
  for candidate in \
    /usr/share/licenses/openssl*/LICENSE* \
    /usr/share/doc/openssl*/LICENSE* \
    /usr/share/doc/libssl*/copyright; do
    if [[ -f "$candidate" ]]; then
      openssl_license="$candidate"
      break
    fi
  done
  if [[ -n "$openssl_license" ]]; then
    install -m 0644 "$openssl_license" "$portable_dir/licenses/OpenSSL-LICENSE.txt"
  fi
fi

# Make linked ELF dependencies independently relocatable as an additional
# integrity guarantee. The launcher still sets LD_LIBRARY_PATH so every private
# runtime resolves consistently.
patchelf --set-rpath "\$ORIGIN/lib" "$portable_dir/airan-desk.real"
while IFS= read -r library; do
  is_elf "$library" || continue
  relative_lib_dir="$(realpath --relative-to="$(dirname "$library")" "$lib_dir")"
  if [[ "$relative_lib_dir" == "." ]]; then
    patchelf --set-rpath "\$ORIGIN" "$library"
  else
    patchelf --set-rpath "\$ORIGIN/$relative_lib_dir" "$library"
  fi
done < <(find "$lib_dir" -type f -print)

while IFS= read -r plugin; do
  is_elf "$plugin" || continue
  relative_lib_dir="$(realpath --relative-to="$(dirname "$plugin")" "$lib_dir")"
  patchelf --set-rpath "\$ORIGIN/$relative_lib_dir" "$plugin"
done < <(find "$plugin_dir" -type f -print)

if find "$portable_dir" \( -type f -o -type l \) \
    \( -name 'libssl.so*' -o -name 'libcrypto.so*' \) -print -quit | grep -q . &&
   [[ ! -f "$portable_dir/licenses/OpenSSL-LICENSE.txt" ]]; then
  echo "Portable package contains OpenSSL runtime libraries but OpenSSL-LICENSE.txt is missing" >&2
  exit 1
fi

for qt_library in Core Gui Widgets Network Svg WebSockets; do
  if ! find "$lib_dir" -maxdepth 1 -name "libQt${linked_qt_major}${qt_library}.so.*" -print -quit | grep -q .; then
    echo "Portable package is missing Qt${qt_library}" >&2
    exit 1
  fi
done

if find "$lib_dir" -maxdepth 1 \
    \( -name 'libfontconfig.so*' -o -name 'libfreetype.so*' \) \
    -print -quit | grep -q .; then
  echo "Portable package must use the system Fontconfig and FreeType stack" >&2
  exit 1
fi

if env -u LD_LIBRARY_PATH ldd "$portable_dir/airan-desk.real" |
    awk '/libQt/ && /=>/ { print $3 }' |
    grep -Ev "^$lib_dir/" | grep -q .; then
  echo "Portable executable resolves a Qt library outside its private lib directory" >&2
  env -u LD_LIBRARY_PATH ldd "$portable_dir/airan-desk.real" >&2
  exit 1
fi

version_gt() {
  [[ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n 1)" == "$1" && "$1" != "$2" ]]
}

max_glibc=""
while IFS= read -r elf_file; do
  is_elf "$elf_file" || continue
  file_max_glibc="$(readelf --version-info "$elf_file" 2>/dev/null |
    grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sed 's/^GLIBC_//' | sort -Vu | tail -n 1 || true)"
  if [[ -n "$file_max_glibc" ]] && { [[ -z "$max_glibc" ]] || version_gt "$file_max_glibc" "$max_glibc"; }; then
    max_glibc="$file_max_glibc"
  fi
done < <(find "$portable_dir" -type f -print)

if [[ -n "$max_glibc" ]] && version_gt "$max_glibc" "$glibc_floor"; then
  echo "Portable package requires GLIBC_$max_glibc, above configured floor GLIBC_$glibc_floor" >&2
  exit 1
fi

temporary_package_path="$(mktemp "$package_dir/.${package_name}.tar.gz.XXXXXX")"
tar -czf "$temporary_package_path" -C "$portable_root" "$package_name"
inspection_dir="$(mktemp -d)"
tar -xzf "$temporary_package_path" -C "$inspection_dir"
mv -f "$temporary_package_path" "$package_path"
package_published=true
cleanup_package_artifacts
trap - EXIT
echo "PORTABLE_PACKAGE_PATH=$package_path"
echo "PORTABLE_MAX_GLIBC=${max_glibc:-not-detected}"
