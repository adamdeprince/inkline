#!/usr/bin/env python3
"""Use Zig with the stock reMarkable GNU ABI for utility cross builds."""
import os
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sdk = root / ".cache/sdk/sysroot-5.7.119"
mode, *arguments = sys.argv[1:]
if mode not in ("cc", "c++"):
    raise SystemExit("Usage: cross.py cc|c++ COMPILER_ARGUMENTS...")
os.environ.setdefault("ZIG_GLOBAL_CACHE_DIR", str(root / ".cache/zig-global"))
environment = subprocess.check_output(["zig", "env"], text=True)
match = re.search(r'\.lib_dir = "([^\"]+)"', environment)
if not match:
    raise SystemExit("Cannot find Zig's compiler headers")
compiler_headers = Path(match[1]) / "include"
flags = ["-target", "arm-linux.5.4-gnueabihf.2.39", "-mcpu=cortex_a7",
         "--sysroot=" + str(sdk), "-nostdinc"]
if mode == "c++":
    cxx = next((sdk / "usr/include/c++").iterdir())
    flags += ["-nostdinc++", "-nostdlib++", "-isystem", str(cxx),
              "-isystem", str(cxx / "arm-remarkable-linux-gnueabi"),
              "-isystem", str(cxx / "backward")]
flags += ["-isystem", str(compiler_headers), "-isystem", str(sdk / "usr/include")]
# pkg-config's root include must not precede the GNU C++ wrapper headers.
arguments = [arg for arg in arguments if arg != "-I" + str(sdk / "usr/include")]
linking = not any(arg in ("-c", "-E", "-S", "-M", "-MM", "--version", "-v") for arg in arguments)
if linking:
    flags += ["-L/usr/lib"]
    if mode == "c++":
        arguments += [str(sdk / "usr/lib/libstdc++.so"), str(sdk / "usr/lib/libgcc_s.so.1")]
os.execvp("zig", ["zig", mode, *flags, *arguments])
