#!/usr/bin/env python3
"""Cross compiler plus temporary QEMU launchers for Emacs's build helpers.

Use on Linux with QEMU_ARM set. ELF outputs are retained as NAME.arm; only
the local build gets a shell launcher. Packaging always uses the ELF files.
This avoids a global binfmt registration or installing an ARM compiler.
"""
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
args = sys.argv[1:]
if args and args[0] == "--wrap":
    output = Path(args[1]).absolute()
else:
    result = subprocess.run([sys.executable, str(root / "scripts/utilities/cross.py"), "cc", *args])
    if result.returncode:
        sys.exit(result.returncode)
    if "-o" not in args or any(arg in args for arg in ("-c", "-E", "-S", "-shared", "-r")):
        sys.exit(0)
    output = Path(args[args.index("-o") + 1]).absolute()
    helpers = {"make-docfile", "make-fingerprint", "hexl", "emacsclient", "etags", "ctags", "ebrowse", "movemail", "update-game-score"}
    if output.name not in helpers:
        sys.exit(0)
if output.read_bytes()[:4] != b"\x7fELF":
    sys.exit(0)
qemu = os.environ["QEMU_ARM"]
sdk = root / ".cache/sdk/sysroot-5.7.119"
deps = root / ".cache/emacs-deps"
binary = output.with_name(output.name + ".arm")
output.replace(binary)
library_path = f"{deps}/lib/arm-linux-gnueabihf:{deps}/usr/lib/arm-linux-gnueabihf:{sdk}/lib:{sdk}/usr/lib"
dump = 'if [ -f "$0.pdmp" ]; then set -- "--dump-file=$0.pdmp" "$@"; fi\n' if output.name == "temacs" else ""
output.write_text("#!/bin/sh\n" + dump + "exec " + shlex.join([qemu, "-L", str(sdk), "-E", "LD_LIBRARY_PATH=" + library_path, "-0"]) + ' "$0" ' + shlex.quote(str(binary)) + ' "$@"\n')
output.chmod(0o755)
