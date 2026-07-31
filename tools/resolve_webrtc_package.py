#!/usr/bin/env python3
import argparse
import json
import re
import sys
from pathlib import Path, PurePath
from typing import Dict


def resolve(manifest_path: Path, package_name: str) -> Dict[str, object]:
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid WebRTC release manifest: {exc}") from exc
    if manifest.get("schema") != 1 or not isinstance(manifest.get("packages"), dict):
        raise ValueError("unsupported WebRTC release manifest schema")
    selected = manifest["packages"].get(package_name)
    if not isinstance(selected, dict):
        raise ValueError(f"WebRTC package is not present in release manifest: {package_name}")

    asset = selected.get("asset")
    digest = selected.get("sha256")
    size = selected.get("size")
    expected_asset = f"libwebrtc-{package_name}.tar.zst"
    if not isinstance(asset, str) or asset != expected_asset or PurePath(asset).name != asset:
        raise ValueError(f"invalid asset name for WebRTC package {package_name}: {asset}")
    if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
        raise ValueError(f"invalid SHA256 for WebRTC package {package_name}")
    if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
        raise ValueError(f"invalid size for WebRTC package {package_name}")
    return {"asset": asset, "sha256": digest, "size": size}


def main() -> int:
    parser = argparse.ArgumentParser(description="Resolve one self-contained WebRTC package from a release manifest.")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--package", required=True)
    parser.add_argument("--field", choices=("asset", "sha256", "size"))
    args = parser.parse_args()
    try:
        selected = resolve(args.manifest, args.package)
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    if args.field:
        print(selected[args.field])
    else:
        print(json.dumps(selected, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
