#!/usr/bin/env python3
"""Validate and unpack a complete Linux portable release for an fnOS package."""

import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import posixpath
import shutil
import struct
import tarfile


def reject(message):
    raise ValueError(message)


def archive_path(name):
    if "\\" in name or "\0" in name or any(ord(c) < 32 for c in name):
        reject("Unsupported archive path: {!r}".format(name))
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts:
        reject("Archive path escapes the payload: {!r}".format(name))
    return path


def unpack(archive, destination):
    with tarfile.open(archive, "r:gz") as source:
        members = source.getmembers()
        names = {}
        links = set()
        for member in members:
            path = archive_path(member.name)
            if not path.parts:
                if not member.isdir():
                    reject("Invalid archive root")
                continue
            if path in names:
                reject("Duplicate archive path: {}".format(path))
            names[path] = member
            if member.issym():
                target = archive_path(member.linkname)
                resolved = posixpath.normpath(str(path.parent / target))
                if resolved == ".." or resolved.startswith("../"):
                    reject("Symlink escapes the payload: {}".format(path))
                links.add(path)
            elif not (member.isfile() or member.isdir()):
                reject("Only regular files, directories and relative symlinks are supported: {}".format(path))
        for path in names:
            if any(parent in links for parent in path.parents):
                reject("Archive entry is nested below a symlink: {}".format(path))
        # No link can be traversed while writing a regular file.
        for path, member in names.items():
            target = destination.joinpath(*path.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(member) as incoming, target.open("xb") as outgoing:
                    shutil.copyfileobj(incoming, outgoing)
                target.chmod(0o755 if member.mode & 0o111 else 0o644)
        for path in links:
            target = destination.joinpath(*path.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.symlink_to(names[path].linkname)
        for path in links:
            target = destination.joinpath(*path.parts)
            if not target.exists() or destination.resolve() not in target.resolve().parents:
                reject("Broken or external portable symlink: {}".format(path))


def validate(payload, arch):
    required = [
        "airan-desk", "airan-desk.real", "conf/common.ini", "qt.conf", "LICENSE",
        "PRIVACY.md", "THIRD_PARTY_SOURCE_OFFER.md", "THIRD_PARTY_SOURCE_OFFER.zh-CN.md",
        "licenses/Third-Party-Notices.md", "licenses/WebRTC-LICENSE.txt",
        "licenses/WebRTC-PATENTS.txt", "licenses/WebRTC-Third-Party-Licenses.txt",
        "licenses/WebRTC-source-revision.txt", "licenses/WebRTC-args.gn",
        "licenses/spdlog-LICENSE.txt", "licenses/fmt-LICENSE.rst",
        "licenses/libvterm-LICENSE.txt", "licenses/Qt-LGPLv3.txt",
        "licenses/Qt-GPLv3.txt",
    ]
    for relative in required:
        path = payload / relative
        if not path.is_file() or path.stat().st_size == 0:
            reject("Incomplete portable package; missing {}".format(relative))
    if not os.access(payload / "airan-desk", os.X_OK):
        reject("Portable launcher is not executable")
    if not os.access(payload / "airan-desk.real", os.X_OK):
        reject("Portable application is not executable")
    if not (payload / "airan-desk").read_bytes().startswith(b"#!"):
        reject("Expected a portable shell launcher at airan-desk")
    with (payload / "airan-desk.real").open("rb") as executable:
        if executable.read(4) != b"\x7fELF":
            reject("Expected a Linux ELF at airan-desk.real")
    for qt in ("Core", "Gui", "Widgets", "Network", "Svg", "WebSockets"):
        if not any((payload / "lib").glob("libQt[56]{}.so.*".format(qt))):
            reject("Incomplete portable package; missing private Qt{}".format(qt))
    if not (payload / "plugins/platforms/libqxcb.so").is_file():
        reject("Incomplete portable package; missing Qt platform plugins")
    if any((payload / "lib").glob("libpipewire-0.3.so*")):
        for relative in ("lib/pipewire-config/client.conf", "lib/spa-0.2/support/libspa-support.so"):
            if not (payload / relative).is_file():
                reject("Incomplete PipeWire runtime; missing {}".format(relative))
        if not any((payload / "lib/pipewire-0.3").glob("*.so")):
            reject("Incomplete PipeWire runtime; missing modules")
    for private in ("logs", "conf/id.ini", "conf/airan.ini"):
        if (payload / private).exists():
            reject("Portable input contains runtime state: {}".format(private))
    expected_machine = {"amd64": 62, "arm64": 183}[arch]
    for path in payload.rglob("*"):
        if path.is_file():
            with path.open("rb") as stream:
                header = stream.read(20)
            if header[:4] == b"\x7fELF":
                if len(header) < 20 or header[4:6] != b"\x02\x01":
                    reject("Unsupported ELF format: {}".format(path.relative_to(payload)))
                if struct.unpack_from("<H", header, 18)[0] != expected_machine:
                    reject("Wrong ELF architecture for {}: {}".format(arch, path.relative_to(payload)))
    # Configuration and logs are linked to the persistent state by the manager.
    (payload / "conf").rename(payload / "defaults-conf")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arch", choices=("amd64", "arm64"))
    parser.add_argument("archive", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    args.destination.mkdir()
    extracted = args.destination / "extract"
    extracted.mkdir()
    unpack(args.archive, extracted)
    candidates = [p.parent for p in extracted.rglob("airan-desk.real")]
    if len(candidates) != 1:
        reject("Expected exactly one complete portable application")
    payload = candidates[0]
    validate(payload, args.arch)
    payload.rename(args.destination / "airan")
    if extracted.exists():
        shutil.rmtree(extracted)
    with args.archive.open("rb") as stream:
        digest = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    (args.destination / "PORTABLE_SOURCE.txt").write_text(
        "Portable source: {}\nSHA256: {}\nArchitecture: {}\n".format(
            args.archive.name, digest.hexdigest(), args.arch), encoding="utf-8")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, tarfile.TarError) as error:
        raise SystemExit("Portable validation failed: {}".format(error))
