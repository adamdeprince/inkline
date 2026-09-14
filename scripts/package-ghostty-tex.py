#!/usr/bin/env python3
"""Package ghostty-tex with private ARM converters and an Inkline RAM-build profile."""
import argparse
import concurrent.futures
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent
RECIPE = ROOT / "scripts/utilities/ghostty-tex"
UPSTREAM = "ghostty-tex-0.1.4.tar.gz"
SHA256 = "30e66c3c94d540647e132c9ef51057692ed62d6a3a49b7be849bbcfb9aadc6a4"
VERSION = "0.1.4+rm2.1"
UPSTREAM_URL = "https://inkline.goblinreactor.com/downloads/utilities/2026-09-14.2/sources/" + UPSTREAM


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def checksum(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-file", type=Path, help="Exact upstream 0.1.4 archive (SHA-256 pinned).")
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--archive", action="store_true", help="Also create the installable archive.")
    parser.add_argument("--release", default="2026-09-14.2")
    args = parser.parse_args()
    common = module("utility_packages", ROOT / "scripts/package-utilities.py")
    debian = module("utility_debian", ROOT / "scripts/utilities/debian.py")
    cache = ROOT / ".cache/utilities/ghostty-tex"
    cache.mkdir(parents=True, exist_ok=True)
    upstream = cache / UPSTREAM
    if args.package_file:
        if checksum(args.package_file) != SHA256:
            raise SystemExit("The upstream ghostty-tex archive does not match the pinned SHA-256.")
        shutil.copy2(args.package_file, upstream)
    if not upstream.exists() and not args.offline:
        with urllib.request.urlopen(UPSTREAM_URL, timeout=60) as response, upstream.open("wb") as output:
            shutil.copyfileobj(response, output)
    if not upstream.exists() or checksum(upstream) != SHA256:
        raise SystemExit("Provide the pinned archive using --package-file, or allow its download.")

    lock = json.loads((RECIPE / "debian.lock.json").read_text())
    inputs = [*lock["packages"].values(), *lock["sources"].values()]
    if args.offline:
        for item in inputs:
            path = debian.CACHE / "downloads" / item["filename"]
            if not path.exists() or checksum(path) != item["sha256"]:
                raise SystemExit("Missing or corrupt offline input: " + str(path))
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            list(pool.map(debian.fetch, inputs))
    # The shared locale/license caches are prepared by scripts/utilities/debian.py.
    package = common.prepare("ghostty-tex", VERSION)
    for item in lock["packages"].values():
        debian.extract_deb(debian.CACHE / "downloads" / item["filename"], package / "runtime/debian")
    with tarfile.open(upstream) as archive:
        archive.extractall(package / "share", filter="data")
    (package / "libexec").mkdir(exist_ok=True)
    build_env = os.environ.copy()
    build_env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / ".cache/zig-global")
    subprocess.run(["zig", "cc", "-target", "arm-linux.5.4-gnueabihf.2.35",
                    "-mcpu=cortex_a7", "-Os", "-s", "-Wall", "-Wextra", "-Werror",
                    str(RECIPE / "cell-size.c"), "-o", str(package / "libexec/cell-size")],
                   check=True, env=build_env)
    for name in ("inkline-ghostty-tex.el", "ram-build.el", "configure.el", "early-init.el", "check.sh",
                 "pre-install.sh", "post-install.sh", "pre-uninstall.sh", "README.txt", "debian.lock.json"):
        shutil.copy2(RECIPE / name, package / name)
        if name.endswith(".sh"):
            (package / name).chmod(0o755)

    gs_init = next((package / "runtime/debian/usr/share/ghostscript").glob("*/Resource/Init"))
    gs_base = gs_init.parent.parent.relative_to(package / "runtime/debian")
    magick_modules = next((package / "runtime/debian/usr/lib/arm-linux-gnueabihf").glob("ImageMagick-*/modules-Q16"))
    magick_base = magick_modules.relative_to(package / "runtime/debian")
    environment = f'''export TMPDIR=/tmp
export XDG_CACHE_HOME=/tmp/inkline-tex-cache
export FONTCONFIG_FILE="$utility_root/fontconfig.conf"
export GS_LIB="$utility_debian/{gs_base}/Resource/Init:$utility_debian/{gs_base}/lib:$utility_debian/{gs_base}/Resource/Font:$utility_debian/usr/share/fonts/type1/urw-base35:$utility_debian/usr/share/poppler/cMap"
export MAGICK_CONFIGURE_PATH="$utility_debian/etc/ImageMagick-6:$utility_debian/usr/share/ImageMagick-6"
export MAGICK_CODER_MODULE_PATH="$utility_debian/{magick_base}/coders"
export MAGICK_FILTER_MODULE_PATH="$utility_debian/{magick_base}/filters"
export MAGICK_TEMPORARY_PATH=/tmp
export MAGICK_THREAD_LIMIT=1 MAGICK_MEMORY_LIMIT=32MiB MAGICK_MAP_LIMIT=0 MAGICK_DISK_LIMIT=0
'''
    for command, executable in (("gs", "gs"), ("convert", "convert-im6.q16"), ("identify", "identify-im6.q16")):
        common.wrapper(package, command, environment + f'exec "$utility_debian/usr/bin/{executable}" "$@"')
    common.wrapper(package, "dvipdfmx", '''tex_prefix=$(cat /home/root/.local/share/inkline-utilities/texlive/state/prefix)
export TMPDIR=/tmp
export TEXMFVAR=/tmp/inkline-tex-cache/texmf-var TEXMFCONFIG=/tmp/inkline-tex-cache/texmf-config TEXMFCACHE=/tmp/inkline-tex-cache/texmf-var
export PATH="$tex_prefix/bin/armhf-linux:$PATH"
exec "$tex_prefix/bin/armhf-linux/dvipdfmx" "$@"''')
    common.wrapper(package, "ghostty-tex-configure", '''export INKLINE_GHOSTTY_TEX_ROOT="$utility_root"
export INKLINE_GHOSTTY_TEX_ACTION="${1:---check}"
exec /home/root/.local/bin/emacs -Q --batch -l "$utility_root/configure.el"''')
    common.write(package / "fontconfig.conf", '''<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <dir prefix="relative">runtime/debian/usr/share/fonts</dir>
  <dir>/usr/share/fonts</dir>
  <cachedir prefix="xdg">fontconfig</cachedir>
</fontconfig>
''')
    common.write(package / "commands", "gs\nconvert\nidentify\ndvipdfmx\nghostty-tex-configure\n")
    common.write(package / "manifest.json", json.dumps({
        "name": "ghostty-tex", "version": VERSION, "model": "reMarkable 2",
        "architecture": "armv7-hard-float", "firmware_line": "3.27", "tested_firmware": "3.27.3.0",
        "release": args.release, "requires": ["emacs", "texlive (compact or larger)"],
        "upstream": {"archive": UPSTREAM, "sha256": SHA256, "unmodified": True},
        "debian_packages": lock["packages"],
        "runtime_storage": "LaTeX output, logs, editing recovery, preview and converter caches use RAM-backed /tmp. Save PDFs explicitly to keep them.",
    }, indent=2) + "\n")
    common.write(package / "SHA256SUMS", "".join(
        f"{checksum(path)}  {path.relative_to(package)}\n"
        for path in sorted(package.rglob("*")) if path.is_file()))

    sources = ROOT / "build/ghostty-tex/sources"
    sources.mkdir(parents=True, exist_ok=True)
    shutil.copy2(upstream, sources / UPSTREAM)
    source_archive = sources / "ghostty-tex-debian-sources.tar"
    with tarfile.open(source_archive, "w") as archive:
        archive.add(RECIPE / "debian.lock.json", arcname="debian.lock.json")
        for item in lock["sources"].values():
            archive.add(debian.CACHE / "downloads" / item["filename"], arcname=item["filename"])
    for path in (sources / UPSTREAM, source_archive):
        common.write(path.with_name(path.name + ".sha256"), f"{checksum(path)}  {path.name}\n")
    print("Staged", package, flush=True)
    if args.archive:
        common.archive(["ghostty-tex"], args.release)


if __name__ == "__main__":
    main()
