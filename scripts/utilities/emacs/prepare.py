#!/usr/bin/env python3
"""Fetch locked Emacs source/build headers and assemble private build dependencies."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[3]
lock = json.loads((Path(__file__).parent / "inputs.lock.json").read_text())
downloads = ROOT / ".cache/utilities/downloads"
downloads.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location("debian", ROOT / "scripts/utilities/debian.py")
debian = importlib.util.module_from_spec(spec)
spec.loader.exec_module(debian)

def fetch(item):
    path = downloads / item["url"].rsplit("/", 1)[-1]
    if not path.exists():
        temporary = path.with_suffix(path.suffix + ".partial")
        with urllib.request.urlopen(item["url"], timeout=60) as response, temporary.open("wb") as output:
            shutil.copyfileobj(response, output)
        temporary.replace(path)
    with path.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != item["sha256"]:
            raise RuntimeError("Checksum mismatch: " + str(path))
    return path

source = fetch(lock["source"])
sources = ROOT / ".cache/utilities/src"
sources.mkdir(parents=True, exist_ok=True)
if not (sources / ("emacs-" + lock["version"])).is_dir():
    with tarfile.open(source) as archive:
        archive.extractall(sources, filter="data")
deps = ROOT / ".cache/emacs-deps"
for item in [*lock["build_headers"], *lock.get("runtime_packages", [])]:
    debian.extract_deb(fetch(item), deps)
runtime = ROOT / ".cache/utilities/debian/groups/emacs"
for subdir in ("lib/arm-linux-gnueabihf", "usr/lib/arm-linux-gnueabihf"):
    # copytree replaces regular files but cannot replace existing symlinks.
    for link in (runtime / subdir).rglob("*"):
        if link.is_symlink():
            (deps / subdir / link.relative_to(runtime / subdir)).unlink(missing_ok=True)
    shutil.copytree(runtime / subdir, deps / subdir, symlinks=True, dirs_exist_ok=True)
print("Emacs source and ARM build dependencies verified and prepared.")
