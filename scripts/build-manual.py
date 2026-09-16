#!/usr/bin/env python3
"""Build the single nine-language manual with LuaLaTeX and temporary caches.

Requires Python 3 and TeX Live with LuaTeX, LuaTeX-ja, fontspec, babel's Latin
languages, TeX Gyre, geometry, fancyhdr, tcolorbox, listings, xurl and bookmark.
See docs/manual/README.md. Nothing is compiled or cached on the tablet during
installation: the resulting PDF is bundled as an ordinary document.
"""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "docs/Inkline Manual.pdf")
    args = parser.parse_args()
    engine = shutil.which("lualatex")
    if not engine:
        raise SystemExit("Install LuaLaTeX and put it on PATH; see docs/manual/README.md.")
    version = re.search(r"project\(inkline VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text())[1]
    work = Path(tempfile.mkdtemp(prefix="inkline-manual-"))
    env = os.environ.copy()
    for key in ("TEXMFVAR", "TEXMFCACHE", "TEXMFCONFIG"):
        directory = work / key.lower()
        directory.mkdir()
        env[key] = str(directory)
    env["TEXINPUTS"] = str(work) + os.pathsep + env.get("TEXINPUTS", "")
    # Fixed source date; callers may supply a release's SOURCE_DATE_EPOCH.
    env.setdefault("SOURCE_DATE_EPOCH", str(int(datetime(2026, 9, 15, tzinfo=timezone.utc).timestamp())))
    env["FORCE_SOURCE_DATE"] = "1"
    env["TZ"] = "UTC"
    (work / "inkline-version.tex").write_text("\\newcommand{\\InklineVersion}{" + version + "}\n")
    command = [engine, "-interaction=nonstopmode", "-halt-on-error", "-file-line-error",
               "-no-shell-escape", "-jobname=inkline-manual", "-output-directory=" + str(work),
               "docs/manual.tex"]
    print(f"Building Inkline {version}, nine languages; temporary files: {work}", flush=True)
    try:
        with (work / "build.log").open("w") as log:
            for _ in range(3):
                subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        tex_log = (work / "inkline-manual.log").read_text(errors="replace")
        if "Missing character:" in tex_log or "undefined references" in tex_log:
            raise RuntimeError("The manual contains missing glyphs or unresolved page links.")
        boxes = [line for line in tex_log.splitlines() if "Overfull" in line]
        if boxes:
            raise RuntimeError("Layout exceeds page bounds:\n" + "\n".join(boxes))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(work / "inkline-manual.pdf", args.output)
        print(f"{args.output}: {args.output.stat().st_size:,} bytes")
    except (subprocess.CalledProcessError, RuntimeError) as error:
        raise SystemExit(f"{error}\nBuild files retained at {work}; read build.log.") from error
    else:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
