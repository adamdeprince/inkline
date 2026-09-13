#!/usr/bin/env python3
"""Extract target headers/libraries for Zig cross-compilation on macOS.

This does not execute the Linux SDK installer or install anything on the tablet.
The complete SDK remains available for the manufacturer's Linux build workflow.
"""
import hashlib
import json
import os
import shutil
from pathlib import Path, PurePosixPath
import tarfile
import tempfile

PROJECT = Path(__file__).resolve().parent.parent
TARGET = "cortexa7hf-neon-remarkable-linux-gnueabi"


def main():
    lock = json.loads((PROJECT / "toolchains/sdk.lock.json").read_text())
    installer = PROJECT / lock["local_path"]
    destination = PROJECT / ".cache/sdk" / ("sysroot-" + lock["sdk_version"])
    manifest = {"sdk_sha256": lock["sha256"], "target": TARGET, "selection_version": 2}
    upgrade = False
    if destination.exists():
        marker = destination / "rmt-sysroot.json"
        if marker.is_file() and json.loads(marker.read_text()) == manifest:
            print(destination)
            return
        old_manifest = dict(manifest, selection_version=1)
        upgrade = marker.is_file() and json.loads(marker.read_text()) == old_manifest
        if not upgrade:
            raise SystemExit(f"Unrecognized existing sysroot: {destination}")
    with installer.open("rb") as source:
        if hashlib.file_digest(source, "sha256").hexdigest() != lock["sha256"]:
            raise SystemExit("SDK checksum mismatch; run scripts/fetch-sdk.py")
        source.seek(0)
        offset = source.read(32768).find(b"\xfd7zXZ\x00")
        if offset < 0:
            raise SystemExit("SDK archive marker not found")
        source.seek(offset)
        with tempfile.TemporaryDirectory(prefix=".sysroot-", dir=destination.parent) as temporary:
            stage = Path(temporary)
            root = stage / "sysroots" / TARGET
            count = 0

            def safe_filter(member, path):
                # Yocto may use absolute target symlinks. Keep their meaning
                # within this sysroot rather than pointing into the host OS.
                if member.issym() and member.linkname.startswith("/"):
                    target = root / member.linkname.lstrip("/")
                    parent = (stage / member.name).parent
                    member = member.replace(linkname=os.path.relpath(target, parent))
                return tarfile.data_filter(member, path)

            with tarfile.open(fileobj=source, mode="r|xz") as archive:
                for member in archive:
                    parts = PurePosixPath(member.name).parts
                    if len(parts) < 3 or parts[:2] != ("sysroots", TARGET):
                        continue
                    relative = PurePosixPath(*parts[2:])
                    # Headers, shared/static link inputs and pkg-config files.
                    # Exclude target programs, debug symbols and native tools.
                    wanted = (relative.is_relative_to("usr/include") or
                              relative.is_relative_to("usr/lib/mkspecs") or
                              relative.is_relative_to("usr/lib/pkgconfig") or
                              relative.parent in (PurePosixPath("usr/lib"), PurePosixPath("lib")) or
                              str(relative) in ("lib", "usr/lib"))
                    if not wanted or not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
                        continue
                    archive.extract(member, path=stage, filter=safe_filter)
                    count += 1
            (root / "rmt-sysroot.json").write_text(json.dumps(manifest, indent=2) + "\n")
            if upgrade:
                backup = stage / "previous-sysroot"
                destination.rename(backup)
                try:
                    root.rename(destination)
                except BaseException:
                    backup.rename(destination)
                    raise
                shutil.rmtree(backup)
            else:
                root.rename(destination)
    print(f"Extracted {count} target entries; no SDK programs were executed.")
    print(destination)


if __name__ == "__main__":
    main()
