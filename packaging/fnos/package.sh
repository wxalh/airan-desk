#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" != 4 ]]; then
    echo 'Usage: package.sh <amd64|arm64> <version> <portable.tar.gz> <output-directory>' >&2
    exit 2
fi

arch="$1"
version="$2"
case "$arch" in
    amd64) platform=x86 ;;
    arm64) platform=arm ;;
    *) printf 'Unsupported fnOS architecture: %s\n' "$arch" >&2; exit 2 ;;
esac
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9]+([.-][A-Za-z0-9]+)*)?$ ]]; then
    printf 'Invalid fnOS version: %s\n' "$version" >&2
    exit 2
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$root/../.." && pwd)"
archive="$(realpath -- "$3")"
[[ -f "$archive" ]] || { printf 'Portable archive not found: %s\n' "$archive" >&2; exit 1; }
mkdir -p -- "$4"
output="$(cd "$4" && pwd)"
work="$(mktemp -d "${TMPDIR:-/tmp}/airan-fnos-package.XXXXXXXX")"
trap 'rm -rf -- "$work"' EXIT
stage="$work/stage"
mkdir -p "$stage/wizard" "$stage/app/ui/images" "$stage/app/bin" "$stage/cmd" "$stage/config"
python3 "$root/prepare-payload.py" "$arch" "$archive" "$work/portable"
mv "$work/portable/airan" "$stage/app/airan"
install -m 0644 "$work/portable/PORTABLE_SOURCE.txt" "$stage/app/PORTABLE_SOURCE.txt"

(
    cd "$root/runtime"
    CGO_ENABLED=0 GOOS=linux GOARCH="$arch" go build -trimpath -ldflags='-s -w' \
        -o "$stage/app/bin/fnos-manager" .
)
mkdir -p "$stage/app/licenses"
goroot="$(go env GOROOT)"
go_series="$(go env GOVERSION | sed -E 's/^go([0-9]+\.[0-9]+).*/\1/')"
if [[ -f "$goroot/LICENSE" ]]; then
    install -m 0644 "$goroot/LICENSE" "$stage/app/licenses/go-LICENSE.txt"
elif [[ -f "/usr/share/doc/golang-$go_series-go/copyright" ]]; then
    install -m 0644 "/usr/share/doc/golang-$go_series-go/copyright" "$stage/app/licenses/go-COPYRIGHT.txt"
else
    echo 'The Go toolchain is missing its redistribution license.' >&2
    exit 1
fi
patents="$goroot/PATENTS"
if [[ ! -f "$patents" ]]; then
    patents="$goroot/src/vendor/golang.org/x/net/PATENTS"
fi
install -m 0644 "$patents" "$stage/app/licenses/go-PATENTS.txt"
install -m 0755 "$root/template/app/bin/notify" "$stage/app/bin/notify"
sed -e "s/@VERSION@/$version/g" -e "s/@PLATFORM@/$platform/g" \
    "$root/template/manifest" > "$stage/manifest"
install -m 0644 "$root/template/config/privilege" "$stage/config/privilege"
install -m 0644 "$root/template/config/resource" "$stage/config/resource"
install -m 0644 "$root/template/app/ui/config" "$stage/app/ui/config"
install -m 0755 "$root/template/cmd/main" "$stage/cmd/main"
for operation in install upgrade uninstall config; do
    for phase in init callback; do
        hook="$stage/cmd/${operation}_${phase}"
        printf '#!/bin/sh\nexit 0\n' > "$hook"
        chmod 0755 "$hook"
    done
done
for size in 64 256; do
    icon="$repo/resource/icons/hicolor/${size}x${size}/apps/airan-desk.png"
    install -m 0644 "$icon" "$stage/app/ui/images/icon_$size.png"
done
install -m 0644 "$stage/app/ui/images/icon_64.png" "$stage/ICON.PNG"
install -m 0644 "$stage/app/ui/images/icon_256.png" "$stage/ICON_256.PNG"

if [[ -n "${FN_PACK:-}" ]]; then
    fnpack="$(command -v -- "$FN_PACK")"
    fnpack="$(realpath -- "$fnpack")"
else
    case "$(uname -m)" in
        x86_64)
            tool_arch=amd64
            digest=1464d6aa9b995ebecb263c5ec60f0f4a7cbf45e0ce887836755fd65abd647d1e
            ;;
        aarch64)
            tool_arch=arm
            digest=213c05a3df07ff881110d9f227926cd2d19601fc4f621815e673bd9ec0ff9373
            ;;
        *) echo 'Unsupported build host architecture; set FN_PACK to a local fnpack binary.' >&2; exit 1 ;;
    esac
    fnpack="$work/fnpack"
    curl --fail --silent --show-error --location --retry 3 \
        "https://static2.fnnas.com/fnpack/fnpack-1.3.3-linux-$tool_arch" -o "$fnpack"
    printf '%s  %s\n' "$digest" "$fnpack" | sha256sum --check --status
    chmod 0755 "$fnpack"
fi

build_flags=()
if [[ -n "${FN_PACK_KEY:-}" || -n "${FN_PACK_CERT:-}" ]]; then
    if [[ ! -f "${FN_PACK_KEY:-}" || ! -f "${FN_PACK_CERT:-}" ]]; then
        echo 'Set both FN_PACK_KEY and FN_PACK_CERT to the developer signing files.' >&2
        exit 2
    fi
    build_flags+=(--key "$(realpath -- "$FN_PACK_KEY")" --cert "$(realpath -- "$FN_PACK_CERT")")
elif [[ "$("$fnpack" build --help)" == *--nosign* ]]; then
    build_flags+=(--nosign)
fi
(cd "$stage" && "$fnpack" build "${build_flags[@]}")
name="airan-desk-fnos-$version-$arch.fpk"
if [[ ! -s "$stage/airan-desk.fpk" ]]; then
    echo 'fnpack did not produce an application archive.' >&2
    exit 1
fi
install -m 0644 "$stage/airan-desk.fpk" "$output/$name"
(cd "$output" && sha256sum "$name" > "$name.sha256")
printf 'Created %s\n' "$output/$name"
