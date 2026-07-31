#!/usr/bin/env bash
set -euo pipefail

source_dir="${1:-${DEPENDENCY_SOURCE:-}}"
package_set="${2:-${PACKAGE_SET:-all}}"
webrtc_package="${3:-${WEBRTC_PACKAGE:-}}"
ffmpeg_package="${4:-${FFMPEG_PACKAGE:-}}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
download_dir="$repo_root/third_party/_downloads"
webrtc_dir="$repo_root/third_party/webrtc"
ffmpeg_dir="$repo_root/third_party/ffmpeg-builds"
webrtc_marker="$webrtc_dir/.airan-package-sha256"
webrtc_identity_marker="$webrtc_dir/.airan-package-id"

mkdir -p "$download_dir" "$webrtc_dir" "$ffmpeg_dir"

# Portable sha256 (Linux: sha256sum, macOS: shasum -a 256)
_airan_sha256() {
  if command -v sha256sum &>/dev/null; then
    sha256sum "$1" | awk '{print tolower($1)}'
  else
    shasum -a 256 "$1" | awk '{print tolower($1)}'
  fi
}

# Portable file size (Linux: stat -c '%s', macOS: stat -f '%z')
_airan_file_size() {
  if stat --version &>/dev/null 2>&1; then
    stat -c '%s' "$1"
  else
    stat -f '%z' "$1"
  fi
}

if [[ -z "$webrtc_package" ]]; then
  echo "ERROR: WebRTC package is required; pass the exact platform/architecture/ABI package as argument 3 or WEBRTC_PACKAGE." >&2
  exit 1
fi
webrtc_manifest="libwebrtc-manifest.json"
webrtc_release_url="https://github.com/wxalh/libwebrtc_build/releases/latest/download"
webrtc_resolver="$repo_root/tools/resolve_webrtc_package.py"
ffmpeg_release_url="https://github.com/wxalh/FFmpeg-Builds/releases/download/latest"
ffmpeg_checksums="checksums.sha256"

ffmpeg_packages=(
  "windows:ffmpeg-n7.1-latest-win32-lgpl-shared-7.1.zip"
  "windows:ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip"
  "windows:ffmpeg-n8.1-latest-winarm64-lgpl-shared-8.1.zip"
  "linux:ffmpeg-n8.1-latest-linux64-lgpl-shared-8.1.tar.xz"
  "linux:ffmpeg-n8.1-latest-linuxarm64-lgpl-shared-8.1.tar.xz"
  "linux:ffmpeg-n8.1-latest-linuxarmhf-lgpl-shared-8.1.tar.xz"
)

source_file() {
  local name="$1"
  local expected_size="${2:-}"
  if [[ -n "$source_dir" && -f "$source_dir/$name" ]]; then
    if [[ -n "$expected_size" && "$(_airan_file_size "$source_dir/$name")" != "$expected_size" ]]; then
      echo "Ignoring stale local package: $name" >&2
    else
    printf '%s\n' "$source_dir/$name"
    return
    fi
  fi
  if [[ -f "$download_dir/$name" ]]; then
    if [[ -n "$expected_size" && "$(_airan_file_size "$download_dir/$name")" != "$expected_size" ]]; then
      rm -f "$download_dir/$name"
      return
    fi
    printf '%s\n' "$download_dir/$name"
    return
  fi
}

download_file() {
  local name="$1"
  local url="$2"
  local target="$download_dir/$name"
  local temp_target="$target.tmp"
  echo "Downloading $name" >&2
  local downloaded=false
  local attempt
  for attempt in 1 2 3 4 5; do
    rm -f "$temp_target"
    if curl -L --fail --retry 5 --retry-delay 5 --connect-timeout 30 \
        -o "$temp_target" "$url"; then
      downloaded=true
      break
    fi
    sleep 5
  done
  if [[ "$downloaded" != true ]]; then
    rm -f "$temp_target"
    echo "Download failed: $url" >&2
    return 1
  fi
  mv -f "$temp_target" "$target"
  printf '%s\n' "$target"
}

copy_or_download() {
  local name="$1"
  local url="$2"
  local expected_size="${3:-}"
  local target="$download_dir/$name"
  local source
  source="$(source_file "$name" "$expected_size" || true)"
  if [[ -n "$source" && "$source" != "$target" ]]; then
    cp -f "$source" "$target"
    printf '%s\n' "$target"
    return
  fi
  if [[ -f "$target" ]]; then
    printf '%s\n' "$target"
    return
  fi
  download_file "$name" "$url" >/dev/null
  printf '%s\n' "$target"
}

copy_or_refresh() {
  local name="$1"
  local url="$2"
  local target="$download_dir/$name"
  local source
  source="$(source_file "$name" || true)"
  if [[ -n "$source" && "$source" != "$target" ]]; then
    cp -f "$source" "$target"
    printf '%s\n' "$target"
    return
  fi
  download_file "$name" "$url"
}

ffmpeg_checksum() {
  local checksums_path="$1"
  local name="$2"
  awk -v expected="$name" '
    length($1) == 64 && $1 ~ /^[0-9a-fA-F]+$/ {
      candidate = $2
      sub(/^\*/, "", candidate)
      if (candidate == expected) {
        print tolower($1)
        found++
      }
    }
    END { if (found != 1) exit 1 }
  ' "$checksums_path"
}

verify_ffmpeg_archive() {
  local archive="$1"
  local expected="$2"
  local name="$3"
  [[ -f "$archive" ]] || return 1
  local actual
  actual="$(_airan_sha256 "$archive")"
  if [[ "$actual" != "$expected" ]]; then
    echo "FFmpeg SHA256 mismatch for $name. Expected $expected, got $actual" >&2
    return 1
  fi
}

ffmpeg_archive() {
  local name="$1"
  local expected="$2"
  local target="$download_dir/$name"
  if [[ -n "$source_dir" && -f "$source_dir/$name" ]] \
      && verify_ffmpeg_archive "$source_dir/$name" "$expected" "$name"; then
    cp -f "$source_dir/$name" "$target"
  fi
  if ! verify_ffmpeg_archive "$target" "$expected" "$name"; then
    rm -f "$target"
    download_file "$name" "$ffmpeg_release_url/$name" >/dev/null
  fi
  if ! verify_ffmpeg_archive "$target" "$expected" "$name"; then
    rm -f "$target"
    echo "Downloaded FFmpeg package failed SHA256 verification: $name" >&2
    return 1
  fi
  printf '%s\n' "$target"
}

webrtc_ready() {
  [[ -f "$webrtc_dir/cmake/LibWebRTCConfig.cmake" || -f "$webrtc_dir/LibWebRTCConfig.cmake" ]]
}

verify_webrtc_archive() {
  local archive="$1"
  local expected="$2"
  local expected_size="$3"
  local actual
  if [[ "$(_airan_file_size "$archive")" != "$expected_size" ]]; then
    echo "WebRTC package size mismatch for $webrtc_package" >&2
    return 1
  fi
  actual="$(_airan_sha256 "$archive")"
  if [[ "$actual" != "$expected" ]]; then
    echo "WebRTC SHA256 mismatch. Expected $expected, got $actual" >&2
    return 1
  fi
}

webrtc_prepared_matches() {
  local expected="$1"
  webrtc_ready || return 1
  [[ -f "$webrtc_marker" && -f "$webrtc_identity_marker" \
    && "$(cat "$webrtc_marker")" == "$expected" \
    && "$(cat "$webrtc_identity_marker")" == "$webrtc_package" ]]
}

expand_webrtc() {
  local archive="$1"
  local expected="$2"
  local expected_size="$3"

  if ! verify_webrtc_archive "$archive" "$expected" "$expected_size"; then
    exit 1
  fi

  echo "Extracting WebRTC package: $webrtc_package"
  find "$webrtc_dir" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
  zstd -d --stdout "$archive" | tar -x -C "$webrtc_dir" --strip-components=1
  printf '%s' "$expected" > "$webrtc_marker"
  printf '%s' "$webrtc_package" > "$webrtc_identity_marker"
}

expand_ffmpeg() {
  local archive="$1"
  local name="$2"
  local expected="$3"
  local target_name="${name%.zip}"
  target_name="${target_name%.tar.xz}"
  local target_dir="$ffmpeg_dir/$target_name"
  local marker_path="$target_dir/.airan-package-sha256"

  if [[ -d "$target_dir/include" && -f "$marker_path" && "$(cat "$marker_path")" == "$expected" ]]; then
    echo "FFmpeg package already prepared: $target_name"
    return
  fi

  echo "Extracting FFmpeg package: $name"
  rm -rf "$target_dir"
  case "$name" in
    *.zip) unzip -q -o "$archive" -d "$ffmpeg_dir" ;;
    *.tar.xz) tar -xf "$archive" -C "$ffmpeg_dir" ;;
  esac
  printf '%s' "$expected" > "$marker_path"
}

manifest_file="$(copy_or_refresh "$webrtc_manifest" "$webrtc_release_url/$webrtc_manifest")"
webrtc_archive="$(python3 "$webrtc_resolver" --manifest "$manifest_file" --package "$webrtc_package" --field asset)"
webrtc_sha256="$(python3 "$webrtc_resolver" --manifest "$manifest_file" --package "$webrtc_package" --field sha256)"
webrtc_size="$(python3 "$webrtc_resolver" --manifest "$manifest_file" --package "$webrtc_package" --field size)"
if webrtc_prepared_matches "$webrtc_sha256"; then
  echo "WebRTC package already prepared"
else
  webrtc_archive_path="$(copy_or_download "$webrtc_archive" "$webrtc_release_url/$webrtc_archive" "$webrtc_size")"
  if ! verify_webrtc_archive "$webrtc_archive_path" "$webrtc_sha256" "$webrtc_size"; then
    echo "Refreshing WebRTC package from latest release" >&2
    rm -f "$download_dir/$webrtc_archive"
    webrtc_archive_path="$(download_file "$webrtc_archive" "$webrtc_release_url/$webrtc_archive")"
  fi
  expand_webrtc "$webrtc_archive_path" "$webrtc_sha256" "$webrtc_size"
fi

ffmpeg_checksums_path="$(copy_or_refresh "$ffmpeg_checksums" "$ffmpeg_release_url/$ffmpeg_checksums")"
ffmpeg_package_matched=0
if [[ -z "$ffmpeg_package" ]]; then
  ffmpeg_package_matched=1
fi
for package_spec in "${ffmpeg_packages[@]}"; do
  set_name="${package_spec%%:*}"
  package="${package_spec#*:}"
  if [[ "$package_set" != "all" && "$package_set" != "$set_name" ]]; then
    continue
  fi
  if [[ -n "$ffmpeg_package" && "$ffmpeg_package" != "$package" ]]; then
    continue
  fi
  ffmpeg_package_matched=1
  if ! expected_ffmpeg_sha256="$(ffmpeg_checksum "$ffmpeg_checksums_path" "$package")"; then
    echo "FFmpeg checksum list must contain exactly one entry for $package" >&2
    exit 1
  fi
  archive="$(ffmpeg_archive "$package" "$expected_ffmpeg_sha256")"
  expand_ffmpeg "$archive" "$package" "$expected_ffmpeg_sha256"
done
if [[ "$ffmpeg_package_matched" -ne 1 ]]; then
  echo "Unknown FFmpeg package selection: $ffmpeg_package" >&2
  exit 1
fi

echo "Third-party packages are ready"
