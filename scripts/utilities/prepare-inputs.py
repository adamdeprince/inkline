#!/usr/bin/env python3
"""Fetch checksum-pinned utility inputs and prepare isolated source trees."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import posixpath
from pathlib import Path
import shutil
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / ".cache/utilities"
LOCK = json.loads((ROOT / "scripts/utilities/inputs.lock.json").read_text())


def fetch(item):
    path = CACHE / "downloads" / item["filename"]
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        partial = path.with_name(path.name + ".partial")
        with urllib.request.urlopen(item["url"], timeout=60) as response, partial.open("wb") as output:
            shutil.copyfileobj(response, output)
        partial.replace(path)
    with path.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != item["sha256"]:
            raise RuntimeError("Checksum mismatch: " + str(path))
    return path


def extract(item, destination, *, strip_root=False):
    marker = destination / ".inkline-input"
    if marker.exists() and marker.read_text().strip() == item["sha256"]:
        return
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    with tarfile.open(CACHE / "downloads" / item["filename"]) as archive:
        for member in archive:
            if strip_root:
                member = member.replace(name="/".join(member.name.split("/")[1:]))
                if member.islnk():
                    member = member.replace(linkname="/".join(member.linkname.split("/")[1:]))
                if not member.name:
                    continue
            archive.extract(member, destination, filter="data")
    marker.write_text(item["sha256"] + "\n")


def python_runtime(item):
    destination = CACHE / "python-3.15"
    marker = destination / ".inkline-input"
    if marker.exists() and marker.read_text().strip() == item["sha256"]:
        return
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    with tarfile.open(CACHE / "downloads" / item["filename"]) as archive:
        members = {member.name: member for member in archive}
        for member in members.values():
            # Some terminal aliases differ only by case. Avoid self-links on
            # the default macOS filesystem, retaining common terminal entries.
            if member.name.startswith("python/share/terminfo"):
                continue
            archive.extract(member, destination, filter="data")
        for name in ("xterm", "xterm-256color", "screen", "screen-256color", "tmux", "tmux-256color", "vt100", "ansi", "dumb"):
            member = next(m for m in members.values() if "/terminfo/" in m.name and m.name.endswith("/" + name))
            seen = set()
            while member.issym() or member.islnk():
                if member.name in seen:
                    raise RuntimeError("Cyclic terminfo alias: " + member.name)
                seen.add(member.name)
                target = member.linkname if member.islnk() else posixpath.normpath(posixpath.join(posixpath.dirname(member.name), member.linkname))
                member = members[target]
            target = destination / "python/share/terminfo" / name[0] / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.extractfile(member).read())
    marker.write_text(item["sha256"] + "\n")


def main():
    inputs = LOCK["inputs"]
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(fetch, inputs.values()))
    for name in ("goblin-mosh", "goblin-view", "goblin-purrfect"):
        extract(inputs[name], CACHE / "src" / name, strip_root=True)
    extract(inputs["webp"], CACHE / "src/libwebp-1.6.0", strip_root=True)
    extract(inputs["texlive"], CACHE / "texlive-installer")
    extract(inputs["python-build-metadata"], CACHE / "python-full-metadata")
    python_runtime(inputs["python"])
    protoc = CACHE / "protoc-25.8"
    protoc.mkdir(exist_ok=True)
    with zipfile.ZipFile(CACHE / "downloads" / inputs["protoc"]["filename"]) as archive:
        for name in archive.namelist():
            if Path(name).is_absolute() or ".." in Path(name).parts:
                raise RuntimeError("Unsafe path in protoc archive")
        archive.extractall(protoc)
    (protoc / "bin/protoc").chmod(0o755)
    shutil.copytree(CACHE / "src/goblin-view/share/licenses", CACHE / "licenses/goblin-view", dirs_exist_ok=True)
    print("Verified and prepared isolated utility inputs.")


if __name__ == "__main__":
    main()
