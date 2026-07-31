#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/.." && pwd)"

libva_version="${LIBVA_VERSION:-2.24.0}"
case "$libva_version" in
  2.24.0) default_commit="add80723247b8031fb8de14d8f599923d3759242" ;;
  *) echo "Unsupported libva version: $libva_version (only 2.24.0 is release-approved)" >&2; exit 1 ;;
esac
libva_commit="${LIBVA_COMMIT:-$default_commit}"
if [[ ! "$libva_commit" =~ ^[0-9a-f]{40}$ ]]; then
  echo "LIBVA_COMMIT must be a lowercase 40-hex commit" >&2
  exit 1
fi
runtime_lib_dir="${AIRAN_LINUX_RUNTIME_LIB_DIR:-$repo_root/third_party/linux-runtime/lib}"
metadata_dir="${AIRAN_LINUX_RUNTIME_METADATA_DIR:-$repo_root/third_party/linux-runtime/metadata}"
work_dir="${AIRAN_LINUX_RUNTIME_BUILD_DIR:-$repo_root/out/linux-runtime-build}"
src_dir="$work_dir/libva-src"
build_dir="$work_dir/libva-build"
stage_dir="$work_dir/libva-stage"

case "$(uname -m)" in
  x86_64|amd64) lib_arch_dir="x86_64-linux-gnu" ;;
  aarch64|arm64) lib_arch_dir="aarch64-linux-gnu" ;;
  armv7l|armv7*|armhf) lib_arch_dir="arm-linux-gnueabihf" ;;
  *) lib_arch_dir="" ;;
esac

rm -rf "$src_dir" "$build_dir" "$stage_dir"
mkdir -p "$work_dir" "$runtime_lib_dir" "$metadata_dir"
find "$runtime_lib_dir" -maxdepth 1 -type f -name 'libva*.so*' -delete

git clone --no-checkout https://github.com/intel/libva.git "$src_dir"
(
  cd "$src_dir"
  git checkout --detach "$libva_commit"
)
actual_commit="$(cd "$src_dir" && git rev-parse HEAD)"
if [[ "$actual_commit" != "$libva_commit" ]]; then
  echo "libva checkout revision mismatch: $actual_commit" >&2
  exit 1
fi

meson setup "$src_dir" "$build_dir" \
  --prefix=/usr \
  --buildtype=release \
  --default-library=shared
ninja -C "$build_dir"
DESTDIR="$stage_dir" ninja -C "$build_dir" install

install -m 0644 "$src_dir/COPYING" "$metadata_dir/libva-LICENSE.txt"
printf '%s\n' "$actual_commit" > "$metadata_dir/libva-source-revision.txt"
printf '%s\n' "https://github.com/intel/libva/archive/${actual_commit}.tar.gz" > "$metadata_dir/libva-source-url.txt"
cat > "$metadata_dir/libva-build-configuration.txt" <<EOF
version=$libva_version
commit=$actual_commit
meson_args=--prefix=/usr --buildtype=release --default-library=shared
EOF

copy_libs_from_dir()
{
  local dir="$1"
  [ -d "$dir" ] || return 0
  find "$dir" -maxdepth 1 -type f \( -name 'libva*.so' -o -name 'libva*.so.*' \) -print0 |
    while IFS= read -r -d '' file; do
      cp -a "$file" "$runtime_lib_dir/"
    done
  find "$dir" -maxdepth 1 -type l \( -name 'libva*.so' -o -name 'libva*.so.*' \) -print0 |
    while IFS= read -r -d '' link; do
      cp -a "$link" "$runtime_lib_dir/"
    done
}

copy_libs_from_dir "$stage_dir/usr/lib"
copy_libs_from_dir "$stage_dir/usr/lib64"
if [ -n "$lib_arch_dir" ]; then
  copy_libs_from_dir "$stage_dir/usr/lib/$lib_arch_dir"
fi

if ! find "$runtime_lib_dir" -maxdepth 1 -name 'libva.so.2*' | grep -q .; then
  echo "libva runtime build did not produce libva.so.2 under $runtime_lib_dir" >&2
  exit 1
fi

: > "$metadata_dir/libva-SHA256SUMS"
while IFS= read -r -d '' runtime_file; do
  printf '%s  %s\n' "$(sha256sum "$runtime_file" | awk '{print $1}')" "$(basename "$runtime_file")" \
    >> "$metadata_dir/libva-SHA256SUMS"
done < <(find "$runtime_lib_dir" -maxdepth 1 -type f -name 'libva*.so*' -print0 | sort -z)
if [[ ! -s "$metadata_dir/libva-SHA256SUMS" ]]; then
  echo "libva runtime checksum manifest is empty" >&2
  exit 1
fi

echo "Bundled libva $libva_version ($actual_commit) runtime libraries in $runtime_lib_dir"
