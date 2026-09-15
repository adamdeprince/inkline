#!/usr/bin/env python3
"""Stage the exact Emacs and private-library sources with their build recipes."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
lock = json.loads((ROOT / "scripts/utilities/emacs/inputs.lock.json").read_text())
deb_lock = json.loads((ROOT / "scripts/utilities/debian.lock.json").read_text())
downloads = ROOT / ".cache/utilities/downloads"
output = ROOT / "build/emacs/sources"
output.mkdir(parents=True, exist_ok=True)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fetch(item):
    path = downloads / item["url"].rsplit("/", 1)[-1]
    if not path.exists():
        temporary = path.with_name(path.name + ".partial")
        with urllib.request.urlopen(item["url"], timeout=60) as response, temporary.open("wb") as stream:
            shutil.copyfileobj(response, stream)
        temporary.replace(path)
    if digest(path) != item["sha256"]:
        raise RuntimeError("Checksum mismatch: " + str(path))
    return path


source = fetch(lock["source"])
shutil.copy2(source, output / source.name)
signature = source.with_name(source.name + ".sig")
if signature.exists():
    shutil.copy2(signature, output / signature.name)

spec = importlib.util.spec_from_file_location("debian", ROOT / "scripts/utilities/debian.py")
debian = importlib.util.module_from_spec(spec)
spec.loader.exec_module(debian)
private = {"libgnutls30", "libp11-kit0", "libidn2-0", "libunistring2", "libtasn1-6",
           "libnettle8", "libhogweed6", "libgmp10", "libffi8", "libc-bin", "base-files"}
source_names = {deb_lock["packages"][name]["source"] for name in private}
source_inputs = [item for name, item in deb_lock["sources"].items()
                 if any(name.startswith(source_name + "_") for source_name in source_names)]
dependencies = output / "emacs-private-library-sources.tar"
with tarfile.open(dependencies, "w") as archive:
    archive.add(ROOT / "scripts/utilities/debian.lock.json", arcname="debian.lock.json")
    archive.add(ROOT / "scripts/utilities/emacs/inputs.lock.json", arcname="emacs-inputs.lock.json")
    for item in source_inputs:
        archive.add(debian.fetch(item), arcname=item["filename"])
    for item in [*lock["tree_sitter_source"], *lock["sqlite_source"]]:
        path = fetch(item)
        archive.add(path, arcname=path.name)

recipes = output / "inkline-emacs-build-recipes.tar.gz"
with tarfile.open(recipes, "w:gz") as archive:
    paths = ["LICENSE", "scripts/build-emacs.sh", "scripts/package-emacs.py",
             "scripts/package-emacs-sources.py", "scripts/package-utilities.py",
             "scripts/fetch-sdk.py", "scripts/extract-sdk-sysroot.py",
             "scripts/zig-ar.sh", "scripts/zig-ranlib.sh", "toolchains/sdk.lock.json",
             "toolchains/README.md", "scripts/utilities/debian.py",
             "scripts/utilities/debian.lock.json", "scripts/utilities/cross.py",
             "scripts/utilities/install-device.sh", "scripts/utilities/uninstall-device.sh"]
    paths += [str(path.relative_to(ROOT)) for path in sorted((ROOT / "scripts/utilities/emacs").iterdir())
              if path.is_file() and path.suffix != ".pyc"]
    for relative in paths:
        archive.add(ROOT / relative, arcname="inkline/" + relative)

for path in (output / source.name, dependencies, recipes):
    path.with_name(path.name + ".sha256").write_text(digest(path) + "  " + path.name + "\n")
    print(path.name, path.stat().st_size)
if signature.exists():
    path = output / signature.name
    path.with_name(path.name + ".sha256").write_text(digest(path) + "  " + path.name + "\n")
