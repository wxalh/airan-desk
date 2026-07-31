#!/usr/bin/env bash
set -euo pipefail

configure_preset=""
build_preset=""
package_name=""
configuration="Release"
cmake_args=()
jobs=""
portable_package_name=""
portable_glibc_floor="2.27"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --configure-preset) configure_preset="$2"; shift 2 ;;
    --build-preset) build_preset="$2"; shift 2 ;;
    --package-name) package_name="$2"; shift 2 ;;
    --configuration) configuration="$2"; shift 2 ;;
    --cmake-arg) cmake_args+=("$2"); shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    --portable-package-name) portable_package_name="$2"; shift 2 ;;
    --portable-glibc-floor) portable_glibc_floor="$2"; shift 2 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [[ -z "$configure_preset" ]]; then
  echo "--configure-preset is required" >&2
  exit 2
fi

if [[ -z "$build_preset" ]]; then
  build_preset="$configure_preset"
fi

if [[ -n "$portable_package_name" && "$configure_preset" != linux-* ]]; then
  echo "--portable-package-name is only supported for Linux presets" >&2
  exit 2
fi

if [[ -z "$package_name" ]]; then
  package_name="$configure_preset"
fi

package_ffmpeg="ON"
if [[ "$configure_preset" == macos-* ]]; then
  package_ffmpeg="OFF"
else
  for cmake_arg in "${cmake_args[@]}"; do
    if [[ "$cmake_arg" == -DAIRAN_ENABLE_FFMPEG_RUNTIME=* ]]; then
      package_ffmpeg="${cmake_arg#*=}"
    fi
  done
fi
if [[ "$package_ffmpeg" != "ON" && "$package_ffmpeg" != "OFF" ]]; then
  echo "AIRAN_ENABLE_FFMPEG_RUNTIME must be ON or OFF" >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

stage_macos_distribution_payload() {
  local bundle_payload_dir="$release_dir/airan-desk.app/Contents/MacOS"
  local entry
  local entries=(
    LICENSE README.md README.en.md PRIVACY.md THIRD_PARTY_SOURCE_OFFER.md
    THIRD_PARTY_SOURCE_OFFER.zh-CN.md
    licenses script
  )
  for entry in "${entries[@]}"; do
    if [[ ! -e "$bundle_payload_dir/$entry" ]]; then
      echo "macOS bundle payload is missing: $entry" >&2
      exit 1
    fi
    rm -rf "$release_dir/$entry"
    cp -a "$bundle_payload_dir/$entry" "$release_dir/$entry"
  done
}

release_dir="$repo_root/out/build/$configure_preset/release"
if [[ "$package_ffmpeg" == "OFF" && -d "$release_dir" ]]; then
  find "$release_dir" -maxdepth 1 \( -type f -o -type l \) \
    \( -iname 'avcodec*.dll' -o -iname 'avdevice*.dll' -o -iname 'avfilter*.dll' \
       -o -iname 'avformat*.dll' -o -iname 'avutil*.dll' -o -iname 'swresample*.dll' \
       -o -iname 'swscale*.dll' -o -name 'libavcodec.so*' -o -name 'libavdevice.so*' \
       -o -name 'libavfilter.so*' -o -name 'libavformat.so*' -o -name 'libavutil.so*' \
       -o -name 'libswresample.so*' -o -name 'libswscale.so*' \
       -o -name 'libavcodec*.dylib' -o -name 'libavdevice*.dylib' \
       -o -name 'libavfilter*.dylib' -o -name 'libavformat*.dylib' \
       -o -name 'libavutil*.dylib' -o -name 'libswresample*.dylib' \
       -o -name 'libswscale*.dylib' \) -delete
fi
official_cmake_args=(
  -DAIRAN_ENABLE_FFMPEG_RUNTIME="$package_ffmpeg"
)
set +u
cmake --preset "$configure_preset" "${cmake_args[@]}" \
  "${official_cmake_args[@]}"
set -u
if [[ -n "$jobs" ]]; then
  cmake --build --preset "$build_preset" --config "$configuration" --parallel "$jobs"
else
  cmake --build --preset "$build_preset" --config "$configuration" --parallel
fi

if [[ ! -d "$release_dir" ]]; then
  echo "Release output directory not found: $release_dir" >&2
  exit 1
fi

if [[ "$configure_preset" == macos-* ]]; then
  stage_macos_distribution_payload
fi

required_release_files=(
  "LICENSE"
  "README.md"
  "README.en.md"
  "PRIVACY.md"
  "THIRD_PARTY_SOURCE_OFFER.md"
  "THIRD_PARTY_SOURCE_OFFER.zh-CN.md"
  "licenses/Third-Party-Notices.md"
  "licenses/third_party_notices.zh-CN.md"
  "licenses/Cisco-OpenH264-BINARY_LICENSE.txt"
  "licenses/WebRTC-LICENSE.txt"
  "licenses/WebRTC-PATENTS.txt"
  "licenses/WebRTC-Third-Party-Licenses.txt"
  "licenses/WebRTC-args.gn"
  "licenses/WebRTC-package.sha256"
  "licenses/WebRTC-source-revision.txt"
  "licenses/spdlog-LICENSE.txt"
  "licenses/fmt-LICENSE.rst"
  "licenses/libvterm-LICENSE.txt"
  "licenses/Qt-LGPLv3.txt"
  "licenses/Qt-GPLv3.txt"
  "script/airan-notify.sh"
  "script/airan-notify.bat"
  "script/README.md"
  "script/README.zh-CN.md"
)
if [[ "$package_ffmpeg" == "ON" ]]; then
  required_release_files+=(
    "licenses/FFmpeg-LICENSE.txt"
    "licenses/FFmpeg-source-revision.txt"
    "licenses/FFmpeg-builds-revision.txt"
    "licenses/FFmpeg-build-configuration.txt"
    "licenses/FFmpeg-dependencies.txt"
    "licenses/FFmpeg-SHA256SUMS"
    "licenses/FFmpeg-SOURCE_URLS.txt"
  )
fi
for required_file in "${required_release_files[@]}"; do
  if [[ ! -f "$release_dir/$required_file" ]]; then
    echo "Package payload is missing required file: $required_file" >&2
    exit 1
  fi
done

if [[ "$package_ffmpeg" == "ON" ]]; then
  if ! find "$release_dir" \( -type f -o -type l \) \
      \( -iname 'avcodec*.dll' -o -name 'libavcodec.so*' \
         -o -name 'libavcodec*.dylib' \) -print -quit | grep -q .; then
    echo "Official package does not contain the required FFmpeg avcodec runtime" >&2
    exit 1
  fi
elif find "$release_dir" \( -type f -o -type l \) \
    \( -iname 'avcodec*.dll' -o -iname 'avutil*.dll' \
       -o -name 'libavcodec.so*' -o -name 'libavutil.so*' \
       -o -name 'libavcodec*.dylib' -o -name 'libavutil*.dylib' \) \
    -print -quit | grep -q .; then
  echo "Package unexpectedly contains an FFmpeg runtime" >&2
  exit 1
fi

if find "$release_dir" \( -type f -o -type l \) \
    \( -iname 'openh264*.dll' -o -iname 'libopenh264*.dll' \
       -o -iname 'openh264*.dylib' -o -iname 'libopenh264*.dylib' \
       -o -iname 'openh264*.a' -o -iname 'libopenh264*.a' \
       -o -iname 'openh264*.lib' -o -iname 'libopenh264*.lib' \
       -o -name 'openh264.so*' -o -name 'libopenh264.so*' \) \
    -print -quit | grep -q .; then
  echo "Package contains a forbidden OpenH264 binary" >&2
  exit 1
fi

if find "$release_dir/lib" -maxdepth 1 -type f -name 'libva.so*' -print -quit 2>/dev/null | grep -q .; then
  for required_libva_file in \
    licenses/libva-LICENSE.txt licenses/libva-source-revision.txt \
    licenses/libva-source-url.txt licenses/libva-build-configuration.txt \
    licenses/libva-SHA256SUMS; do
    if [[ ! -f "$release_dir/$required_libva_file" ]]; then
      echo "Package contains libva runtime but is missing $required_libva_file" >&2
      exit 1
    fi
  done
fi

if find "$release_dir" \( -type f -o -type l \) \
    \( -iname 'libssl*.dll' -o -iname 'libcrypto*.dll' \
       -o -name 'libssl.so*' -o -name 'libcrypto.so*' \
       -o -name 'libssl*.dylib' -o -name 'libcrypto*.dylib' \) -print -quit | grep -q . &&
   [[ ! -f "$release_dir/licenses/OpenSSL-LICENSE.txt" ]]; then
  echo "Package contains OpenSSL runtime libraries but OpenSSL-LICENSE.txt is missing" >&2
  exit 1
fi
chmod 0755 "$release_dir/script/airan-notify.sh"

if [[ "$configure_preset" == linux-* ]]; then
  required_linux_files=(
    "airan-desk"
    "airan-desk-wrapper.sh"
    "install-linux.sh"
    "remove-linux.sh"
    "60-airan-desk-uinput.rules"
    "airan-desk-ensure-uinput-rule"
    "airan-desk.desktop"
    "airan-desk-autostart.desktop"
    "conf/common.ini"
  )
  for required_file in "${required_linux_files[@]}"; do
    if [[ ! -e "$release_dir/$required_file" ]]; then
      echo "Linux package payload is missing required file: $required_file" >&2
      exit 1
    fi
  done
  if [[ ! -d "$release_dir/icons/hicolor" ]]; then
    echo "Linux package payload is missing required directory: icons/hicolor" >&2
    exit 1
  fi
  chmod +x "$release_dir/install-linux.sh"
  chmod +x "$release_dir/remove-linux.sh"
fi

if [[ "$configure_preset" == macos-* ]]; then
  app_bundle="$release_dir/airan-desk.app"
  app_executable="$app_bundle/Contents/MacOS/airan-desk"
  if [[ ! -x "$app_executable" ]]; then
    echo "macOS app executable is missing: $app_executable" >&2
    exit 1
  fi
  if [[ ! -d "$app_bundle/Contents/Frameworks" ]]; then
    echo "macOS app bundle has no deployed Frameworks directory" >&2
    exit 1
  fi
  if [[ ! -f "$app_bundle/Contents/Resources/airan-desk.icns" ]]; then
    echo "macOS app bundle is missing its application icon" >&2
    exit 1
  fi
  for localization in en zh-Hans; do
    localized_info="$app_bundle/Contents/Resources/$localization.lproj/InfoPlist.strings"
    if [[ ! -f "$localized_info" ]]; then
      echo "macOS app bundle is missing localized permission text: $localization" >&2
      exit 1
    fi
    plutil -lint "$localized_info"
  done
  plutil -lint "$app_bundle/Contents/Info.plist"
  /usr/libexec/PlistBuddy -c "Print :NSMicrophoneUsageDescription" "$app_bundle/Contents/Info.plist" >/dev/null
  /usr/libexec/PlistBuddy -c "Print :NSAppleEventsUsageDescription" "$app_bundle/Contents/Info.plist" >/dev/null
  codesign --verify --deep --strict "$app_bundle"
  entitlements_xml="$(codesign -d --entitlements :- "$app_bundle" 2>/dev/null)"
  if ! grep -q '<key>com.apple.security.device.audio-input</key>' <<<"$entitlements_xml" ||
      ! grep -q '<key>com.apple.security.automation.apple-events</key>' <<<"$entitlements_xml"; then
    echo "macOS app bundle is missing required hardened-runtime entitlements" >&2
    exit 1
  fi

  screen_capture_load_command="$(
    otool -l "$app_executable" |
      awk '$1 == "cmd" { load_command = $2 }
           $1 == "name" && $2 ~ /ScreenCaptureKit\.framework/ { print load_command; exit }'
  )"
  if [[ "$screen_capture_load_command" != "LC_LOAD_WEAK_DYLIB" ]]; then
    echo "ScreenCaptureKit must be weak-linked for the macOS 12.0 deployment target" >&2
    otool -l "$app_executable" >&2
    exit 1
  fi

  expected_arch="arm64"
  if [[ "$configure_preset" == "macos-x64"* ]]; then
    expected_arch="x86_64"
  fi
  while IFS= read -r bundled_file; do
    if ! file "$bundled_file" | grep -q 'Mach-O'; then
      continue
    fi
    if ! lipo -archs "$bundled_file" | tr ' ' '\n' | grep -qx "$expected_arch"; then
      echo "macOS bundle contains a Mach-O file without $expected_arch: $bundled_file" >&2
      exit 1
    fi
    if otool -L "$bundled_file" | awk 'NR > 1 { print $1 }' |
        grep -E '^/' |
        grep -Ev '^(/usr/lib/|/System/Library/Frameworks/)' >/dev/null; then
      echo "macOS bundle contains a nonportable dependency: $bundled_file" >&2
      otool -L "$bundled_file" >&2
      exit 1
    fi
    if otool -l "$bundled_file" |
        awk '$1 == "cmd" && $2 == "LC_RPATH" { want_path = 1; next }
             want_path && $1 == "path" { print $2; want_path = 0 }' |
        grep -E '^/' >/dev/null; then
      echo "macOS bundle contains a nonportable runtime search path: $bundled_file" >&2
      otool -l "$bundled_file" >&2
      exit 1
    fi
  done < <(find "$app_bundle/Contents" -type f -print)
fi

if [[ "$configure_preset" == win* ]]; then
  windows_executable="$release_dir/airan-desk.exe"
  if [[ ! -x "$windows_executable" && ! -f "$windows_executable" ]]; then
    echo "Windows release executable is missing: $windows_executable" >&2
    exit 1
  fi
fi

package_dir="$repo_root/out/packages"
mkdir -p "$package_dir"
if [[ -n "$portable_package_name" ]]; then
  bash "$repo_root/tools/build_linux_portable.sh" \
    --release-dir "$release_dir" \
    --package-name "$portable_package_name" \
    --glibc-floor "$portable_glibc_floor" \
    --include-ffmpeg "$package_ffmpeg"
else
  package_path="$package_dir/$package_name.tar.gz"
  temporary_package_path="$(mktemp "$package_dir/.${package_name}.tar.gz.XXXXXX")"
  inspection_dir=""
  cleanup_package_artifacts() {
    rm -f "$temporary_package_path"
    if [[ -n "$inspection_dir" ]]; then
      rm -rf "$inspection_dir"
    fi
  }
  trap cleanup_package_artifacts EXIT
  tar --exclude='*.lib' -czf "$temporary_package_path" -C "$release_dir" .
  inspection_dir="$(mktemp -d)"
  tar -xzf "$temporary_package_path" -C "$inspection_dir"
  mv -f "$temporary_package_path" "$package_path"
  cleanup_package_artifacts
  trap - EXIT
  echo "PACKAGE_PATH=$package_path"
fi
