#!/usr/bin/env python3
"""Stage the source-built Emacs 31.1 terminal-only tablet bundle."""
import importlib.util
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("utilities", ROOT / "scripts/package-utilities.py")
utilities = importlib.util.module_from_spec(spec)
spec.loader.exec_module(utilities)
lock = json.loads((ROOT / "scripts/utilities/emacs/inputs.lock.json").read_text())
prefix = ROOT / "build/emacs/install/home/root/.local/share/inkline-utilities/emacs/current/runtime/emacs"
if not (prefix / "bin/emacs-31.1").is_file():
    raise SystemExit("Build Emacs first; see scripts/utilities/emacs/README.md")
package = utilities.prepare("emacs", "31.1+20260915.rm2.2", release="2026-09-15.2")
utilities.tree(prefix, package / "runtime/emacs")
# Strip ELF debug/symbol sections; never change the portable-dump fingerprint.
for path in (package / "runtime/emacs").rglob("*"):
    if not path.is_file() or path.is_symlink():
        continue
    with path.open("rb") as stream:
        header = stream.read(20)
    if header[:4] == b"\x7fELF":
        if header[18:20] != b"\x28\0":
            raise RuntimeError("Non-ARM binary in package: " + str(path))
        temporary = path.with_name(path.name + ".stripped")
        utilities.binary(path, temporary)
        temporary.replace(path)
    elif header.startswith(b"#!/bin/sh") and "qemu" in path.read_text(errors="replace"):
        raise RuntimeError("Build helper leaked into package: " + str(path))

# GnuTLS and SQLite are absent from stock firmware. XML, lcms and terminal
# libraries use the matching stock SDK/firmware ABI.
library_packages = {
    "libgnutls.so.30": "libgnutls30", "libp11-kit.so.0": "libp11-kit0",
    "libidn2.so.0": "libidn2-0", "libunistring.so.2": "libunistring2",
    "libtasn1.so.6": "libtasn1-6", "libnettle.so.8": "libnettle8",
    "libhogweed.so.6": "libhogweed6", "libgmp.so.10": "libgmp10", "libffi.so.8": "libffi8",
}
debian = ROOT / ".cache/utilities/debian/groups/emacs"
library_dir = package / "runtime/lib"
library_dir.mkdir()
for library, name in library_packages.items():
    source = next(debian.rglob(library))
    shutil.copy2(source.resolve(), library_dir / library)
    utilities.tree(debian / "usr/share/doc" / name, package / "licenses" / name)
deps = ROOT / ".cache/emacs-deps"
shutil.copy2((deps / "usr/lib/arm-linux-gnueabihf/libsqlite3.so.0").resolve(),
             library_dir / "libsqlite3.so.0")
utilities.tree(deps / "usr/share/doc/libsqlite3-0", package / "licenses/libsqlite3-0")

source = ROOT / ".cache/utilities/src/emacs-31.1"
shutil.copy2(source / "COPYING", package / "licenses/Emacs-GPL-3.0.txt")
tree_license = ROOT / ".cache/emacs-deps/usr/share/doc/libtree-sitter-dev/copyright"
shutil.copy2(tree_license, package / "licenses/tree-sitter-copyright")
utilities.tree(ROOT / "scripts/utilities/emacs", package / "build-recipe/emacs")
shutil.copy2(ROOT / "scripts/build-emacs.sh", package / "build-recipe/build-emacs.sh")
shutil.copy2(ROOT / "scripts/package-emacs.py", package / "build-recipe/package-emacs.py")
dump = next((package / "runtime/emacs/libexec").rglob("emacs-*.pdmp")).relative_to(package)
lisp = package / "runtime/emacs/share/emacs/31.1/lisp"
load_path = ":".join("$utility_root/" + str(p.relative_to(package))
                     for p in [lisp, *sorted(p for p in lisp.rglob("*") if p.is_dir())])
environment = f'''export LD_LIBRARY_PATH="$utility_root/runtime/lib${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}"
export EMACSDATA="$utility_root/runtime/emacs/share/emacs/31.1/etc"
export EMACSDOC="$utility_root/runtime/emacs/share/emacs/31.1/etc"
export EMACSLOADPATH="{load_path}"
export EMACSPATH="$utility_root/runtime/emacs/libexec/emacs/31.1/arm-linux-gnueabihf"
'''
utilities.wrapper(package, "emacs", environment + f'''exec "$utility_root/runtime/emacs/bin/emacs-31.1" --dump-file="$utility_root/{dump}" --no-window-system --no-site-file "$@"''')
utilities.wrapper(package, "emacsclient", '''exec "$utility_root/runtime/emacs/bin/emacsclient" --tty "$@"''')
shutil.copy2(ROOT / "scripts/utilities/emacs/check.el", package / "check.el")
metadata = json.loads((package / "manifest.json").read_text())
deb_lock = json.loads((ROOT / "scripts/utilities/debian.lock.json").read_text())
metadata.update({"upstream": lock, "build": "Source-built ARMv7, no X/NS/PGTK/Cairo or native compilation",
                 "debian_packages": {name: deb_lock["packages"][name] for name in sorted(set(library_packages.values()))}})
utilities.write(package / "manifest.json", json.dumps(metadata, indent=2) + "\n")
with (package / "README.txt").open("a") as output:
    output.write("\nGNU Emacs 31.1, built from verified upstream source for terminal use.\n")
    output.write("Run emacs normally; the wrapper always uses --no-window-system (-nw).\n")
    output.write("Native compilation is disabled at build time, so no automatic .eln cache is created.\n")
    output.write("After installation, unused older Emacs releases are removed automatically.\n")
    output.write("In-use releases are kept; after closing Emacs, run ~/inkline cleanup emacs.\n")
    output.write("Your ~/.emacs and ~/.emacs.d configuration is preserved. emacsclient uses --tty.\n")
    output.write("Source: https://inkline.goblinreactor.com/downloads/utilities/2026-09-15.1/sources/\n")
utilities.finish(package, ["emacs", "emacsclient"], '"$utility_root/bin/emacs" -Q --batch -l "$utility_root/check.el"')
print(package)
