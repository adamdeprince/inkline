#!/usr/bin/env python3
"""Stage relocatable reMarkable utility packages from verified build inputs."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / ".cache/utilities"
STAGE = ROOT / "build/utilities/packages"
DIST = ROOT / "build/dist/utilities/2026-09-13"
os.environ.setdefault("ZIG_GLOBAL_CACHE_DIR", str(ROOT / ".cache/zig-global"))


def write(path, text, executable=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(0o755 if executable else 0o644)


def tree(source, destination):
    shutil.copytree(source, destination, symlinks=True, dirs_exist_ok=True)


def binary(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    objcopy = os.environ.get("LLVM_OBJCOPY") or shutil.which("llvm-objcopy")
    if not objcopy:
        rust_sysroot = subprocess.check_output(["rustc", "--print", "sysroot"], text=True).strip()
        candidates = list(Path(rust_sysroot).glob("lib/rustlib/*/bin/llvm-objcopy"))
        if not candidates:
            candidates = list((Path.home() / ".rustup/toolchains").glob("stable-*/lib/rustlib/*/bin/llvm-objcopy"))
        if candidates:
            objcopy = str(candidates[0])
    if not objcopy:
        raise RuntimeError("Set LLVM_OBJCOPY or install rustup's llvm-tools component.")
    subprocess.run([objcopy, "--strip-all", str(source), str(destination)], check=True)
    destination.chmod(0o755)


def wrapper(package, command, body):
    write(package / "bin" / command, '''#!/bin/sh
set -eu
utility_root=$(CDPATH= cd -- "$(dirname -- "$(readlink -f -- "$0")")/.." && pwd)
. "$utility_root/runtime.env"
''' + body.replace("exec ", "utility_exec ") + "\n", True)


def prepare(name, version, group=None, release=None):
    package = STAGE / name
    if package.exists():
        shutil.rmtree(package)
    package.mkdir(parents=True)
    write(package / "package-name", name + "\n")
    write(package / "version", version + "\n")
    for script in ("install-device.sh", "uninstall-device.sh"):
        shutil.copy2(ROOT / "scripts/utilities" / script, package / script)
        (package / script).chmod(0o755)
    locale = CACHE / "debian/groups/locale/usr/lib/locale/C.utf8"
    tree(locale, package / "locale/C.utf8")
    tree(CACHE / "debian/groups/licenses/usr/share/common-licenses", package / "licenses/common")
    shutil.copy2(CACHE / "debian/groups/locale/usr/share/doc/libc-bin/copyright", package / "licenses/locale-copyright")
    if group:
        tree(CACHE / "debian/groups" / group, package / "runtime/debian")
    write(package / "runtime.env", '''# Sourced by this package's command wrappers.
export LOCPATH="$utility_root/locale${LOCPATH:+:$LOCPATH}"
utility_exec() { exec env LC_ALL=C.UTF-8 LANG=C.UTF-8 "$@"; }
export PATH="$utility_root/bin:$PATH"
if [ -d "$utility_root/runtime/debian" ]; then
    utility_debian="$utility_root/runtime/debian"
    export LD_LIBRARY_PATH="$utility_debian/lib/arm-linux-gnueabihf:$utility_debian/usr/lib/arm-linux-gnueabihf:$utility_debian/lib:$utility_debian/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export PERL5LIB="$utility_debian/usr/share/perl/5.36:$utility_debian/usr/lib/arm-linux-gnueabihf/perl/5.36:$utility_debian/usr/lib/arm-linux-gnueabihf/perl-base:$utility_debian/usr/share/perl5"
fi
''')
    metadata = {"name": name, "version": version, "model": "reMarkable 2", "architecture": "armv7-hard-float",
                "firmware_line": "3.27", "tested_firmware": "3.27.3.0", "release": release or ("2026-09-14.1" if name == "goblin-purrfect" else "2026-09-14" if name == "goblin-view" else "2026-09-13")}
    if group:
        lock = json.loads((CACHE / "debian/lock.json").read_text())
        metadata["debian_packages"] = {p: lock["packages"][p] for p in lock["groups"][group]}
    write(package / "manifest.json", json.dumps(metadata, indent=2) + "\n")
    write(package / "README.txt", f'''{name} {version} for reMarkable 2 / firmware 3.27

Installation: https://inkline.goblinreactor.com/install.html#utilities
Catalog, matching source and licenses: https://inkline.goblinreactor.com/utilities.html

After verifying the outer archive checksum, run ./install-device.sh --check on
the tablet, then ./install-device.sh. Commands appear in /home/root/.local/bin,
which is already on Inkline's PATH. No stock system libraries are replaced.
Run the installed current/uninstall-device.sh to remove this utility.

The package includes a private UTF-8 locale. Perl and other missing runtimes,
where required, are private to this package. Programs can save files normally;
the terminal's policy of keeping graphics in RAM is separate from document I/O.
''')
    return package


def finish(package, commands, check):
    write(package / "commands", "\n".join(commands) + "\n")
    write(package / "check.sh", '''#!/bin/sh
set -eu
utility_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
''' + check + "\n", True)
    # All checksums use regular-file contents; package links remain relative.
    files = sorted(p for p in package.rglob("*") if p.is_file())
    checksums = []
    for path in files:
        with path.open("rb") as stream:
            checksums.append(f"{hashlib.file_digest(stream, 'sha256').hexdigest()}  {path.relative_to(package)}\n")
    write(package / "SHA256SUMS", "".join(checksums))


def perl_check():
    return '''. "$utility_root/runtime.env"
env LC_ALL=C.UTF-8 LANG=C.UTF-8 "$utility_debian/usr/bin/perl" -MGetopt::Long -MIO::Socket -MIPC::Open3 -MText::ParseWords -MSocket -MPOSIX -MSymbol -e 'POSIX::setlocale(POSIX::LC_CTYPE(), "") or die "UTF-8 locale unavailable"; print "Perl and UTF-8 locale OK\\n"'
'''


def rust_licenses(package):
    metadata = json.loads((ROOT / "build/utilities/purrfect-metadata.json").read_text())
    notices = []
    for crate in metadata["packages"]:
        if not crate["source"]:
            continue
        source = Path(crate["manifest_path"]).parent
        destination = package / "licenses/crates" / f'{crate["name"]}-{crate["version"]}'
        found = []
        for path in source.rglob("*"):
            if path.is_file() and path.name.lower().startswith(("license", "copying", "notice", "copyright", "authors")):
                relative = path.relative_to(source)
                target = destination / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
                found.append(str(relative))
        if not found:
            raise RuntimeError(f'Missing license text for {crate["name"]}')
        notices.append({"name": crate["name"], "version": crate["version"], "license": crate["license"], "files": found})
    write(package / "licenses/crates.json", json.dumps(notices, indent=2) + "\n")
    compiler = os.environ.get("RUSTC", "rustc")
    sysroot = Path(subprocess.check_output([compiler, "--print", "sysroot"], text=True).strip())
    tree(sysroot / "share/doc/rust/licenses", package / "licenses/rust")
    shutil.copy2(sysroot / "share/doc/rust/COPYRIGHT-library.html", package / "licenses/rust/COPYRIGHT-library.html")


def build_goblin_view():
    package = prepare("goblin-view", "0.1.0+20260914.rm2.2")
    source = CACHE / "src/goblin-view"
    binary(source / "goblin-view", package / "libexec/goblin-view")
    tree(source / "share", package / "share/goblin-view")
    tree(CACHE / "licenses/goblin-view", package / "licenses/input-methods")
    for name in ("LICENSE", "NOTICE"):
        shutil.copy2(source / name, package / name)
    wrapper(package, "goblin-view", 'export GOBLIN_VIEW_IMDATA="$utility_root/share/goblin-view"\nif [ "${TERM_PROGRAM:-}" = inkline ]; then set -- -m "$@"; fi\nexec "$utility_root/libexec/goblin-view" "$@"')
    finish(package, ["goblin-view"], '"$utility_root/bin/goblin-view" -h >/dev/null 2>&1 || test "$?" = 1')


def build_goblin_purrfect():
    package = prepare("goblin-purrfect", "0.1.0+20260914.rm2.2")
    source = CACHE / "src/goblin-purrfect"
    binary(ROOT / "build/utilities/purrfect/armv7-unknown-linux-gnueabihf/release/goblin-purrfect", package / "libexec/goblin-purrfect")
    for name in ("LICENSE", "NOTICE"):
        shutil.copy2(source / name, package / name)
    for fonts in ("latin-modern-math", "text"):
        for license in (source / "assets/fonts" / fonts).iterdir():
            if license.suffix.lower() in (".txt", ".md"):
                write(package / "licenses/fonts" / fonts / license.name, license.read_text())
    rust_licenses(package)
    wrapper(package, "goblin-purrfect", (ROOT / "scripts/utilities/purrfect-launch.sh").read_text())
    finish(package, ["goblin-purrfect"], '"$utility_root/bin/goblin-purrfect" --help | grep -q "epaper: black text on white"')


def build_python():
    package = prepare("python3", "3.15.0rc2+20260901.rm2.3", release="2026-09-15")
    # Preserve the upstream interpreter, standard library, pip and ensurepip.
    # Cache preferences belong in the user's shell configuration.
    tree(CACHE / "python-3.15/python", package / "runtime/python")
    tree(CACHE / "python-full-metadata/python/licenses", package / "licenses/python")
    shutil.copy2(CACHE / "python-full-metadata/python/PYTHON.json", package / "licenses/python/PYTHON.json")
    shutil.copy2(ROOT / "docs/python.md", package / "PYTHON.md")
    for command in ("python3", "python3.15", "pip3", "pip3.15"):
        argument = " -m pip" if command.startswith("pip") else ""
        wrapper(package, command, '''export TERMINFO_DIRS="$utility_root/runtime/python/share/terminfo:/etc/terminfo:/usr/share/terminfo"
if [ -z "${SSL_CERT_FILE:-}" ] && [ -z "${SSL_CERT_DIR:-}" ]; then
    export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
fi
exec "$utility_root/runtime/python/bin/python3.15"''' + argument + ' "$@"')
    metadata = json.loads((package / "manifest.json").read_text())
    metadata["python_input"] = json.loads((ROOT / "scripts/utilities/inputs.lock.json").read_text())["inputs"]["python"]
    metadata["cache_policy"] = "Unmodified upstream Python; configure preferences in ~/.bashrc."
    write(package / "manifest.json", json.dumps(metadata, indent=2) + "\n")
    finish(package, ["python3", "python3.15", "pip3", "pip3.15"], '''"$utility_root/bin/python3.15" -c 'import sys, ssl, sqlite3, ctypes, readline, decimal, zlib, bz2, lzma, venv, pip; assert sys.version_info[:2] == (3, 15); assert ssl.create_default_context().cert_store_stats()["x509_ca"] > 0; print(sys.version)' ''')


def build():
    lock = json.loads((CACHE / "debian/lock.json").read_text())
    package = prepare("goblin-mosh", "1.4.0+e4e8afbb.rm2.1", "perl")
    source = CACHE / "src/goblin-mosh"
    for command, subdir in (("goblin-mosh-client", "frontend"), ("goblin-mosh-server", "frontend"), ("goblin-moshcp", "moshcp")):
        binary(source / "src" / subdir / command, package / "libexec" / command)
        wrapper(package, command, f'exec "$utility_root/libexec/{command}" "$@"')
    shutil.copy2(source / "scripts/goblin-mosh", package / "libexec/goblin-mosh.pl")
    wrapper(package, "goblin-mosh", 'exec "$utility_debian/usr/bin/perl" "$utility_root/libexec/goblin-mosh.pl" --no-downloads "$@"')
    for name in ("COPYING", "THIRD_PARTY.md", "README.md"):
        write(package / "licenses/goblin-mosh" / name, (source / name).read_text())
    for name in ("COPYING", "PATENTS", "AUTHORS"):
        write(package / "licenses/webp" / name, (CACHE / "src/libwebp-1.6.0" / name).read_text())
    finish(package, ["goblin-mosh", "goblin-mosh-client", "goblin-mosh-server", "goblin-moshcp"], perl_check() + '"$utility_root/bin/goblin-mosh" --help >/dev/null\n"$utility_root/bin/goblin-mosh-client" --version')

    package = prepare("mosh", lock["packages"]["mosh"]["version"], "mosh")
    for command in ("mosh-client", "mosh-server"):
        wrapper(package, command, f'exec "$utility_debian/usr/bin/{command}" "$@"')
    wrapper(package, "mosh", 'exec "$utility_debian/usr/bin/perl" "$utility_debian/usr/bin/mosh" "$@"')
    finish(package, ["mosh", "mosh-client", "mosh-server"], perl_check() + '"$utility_root/bin/mosh" --help >/dev/null\n"$utility_root/bin/mosh-client" --version')

    build_goblin_view()

    build_goblin_purrfect()

    package = prepare("emacs", lock["packages"]["emacs-nox"]["version"], "emacs")
    dump = next((package / "runtime/debian").rglob("emacs*.pdmp")).relative_to(package / "runtime/debian")
    lisp = package / "runtime/debian/usr/share/emacs/28.2/lisp"
    load_path = ":".join("$utility_debian/" + str(p.relative_to(package / "runtime/debian"))
                         for p in [lisp, *sorted(p for p in lisp.rglob("*") if p.is_dir())])
    wrapper(package, "emacs", f'''export EMACSDATA="$utility_debian/usr/share/emacs/28.2/etc"
export EMACSDOC="$utility_debian/usr/share/emacs/28.2/etc"
export EMACSLOADPATH="{load_path}"
export EMACSPATH="$utility_debian/usr/libexec/emacs/28.2/arm-linux-gnueabihf"
export EMACSNATIVELOADPATH="$utility_debian/usr/lib/emacs/28.2/native-lisp"
exec "$utility_debian/usr/bin/emacs-nox" --dump-file="$utility_debian/{dump}" --no-site-file --eval '(setq native-comp-deferred-compilation nil)' "$@"''')
    finish(package, ["emacs"], '''"$utility_root/bin/emacs" -Q --batch --eval '(progn (require (quote org)) (require (quote tramp)) (require (quote tex-mode)) (with-temp-buffer (insert "Inkline λ") (unless (= (buffer-size) 9) (error "Unicode failed"))) (princ emacs-version) (terpri))' ''')

    package = prepare("git", lock["packages"]["git"]["version"], "git")
    wrapper(package, "git", '''export GIT_EXEC_PATH="$utility_debian/usr/lib/git-core"
export GIT_TEMPLATE_DIR="$utility_debian/usr/share/git-core/templates"
exec "$utility_debian/usr/bin/git" "$@"''')
    finish(package, ["git"], '"$utility_root/bin/git" --version')

    build_python()
    package = prepare("texlive", "2026+20260913.rm2.1", "texlive")
    tree(CACHE / "texlive-installer/install-tl-20260913", package / "runtime/texlive-installer")
    for script in ("install-texlive.sh", "remove-texlive.sh"):
        target = package / "libexec" / script
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "scripts/utilities" / script, target)
        target.chmod(0o755)
    wrapper(package, "perl", 'exec "$utility_debian/usr/bin/perl" "$@"')
    wrapper(package, "curl", 'exec "$utility_debian/usr/bin/curl" "$@"')
    wrapper(package, "texlive-install", 'exec "$utility_root/libexec/install-texlive.sh" "$@"')
    wrapper(package, "texlive-remove", 'exec "$utility_root/libexec/remove-texlive.sh" "$@"')
    tex_commands = ["tex", "pdftex", "latex", "pdflatex", "luatex", "lualatex", "xelatex", "bibtex", "biber", "kpsewhich", "latexmk", "tlmgr"]
    for command in tex_commands:
        optional = "xetex" if command == "xelatex" else command
        wrapper(package, command, '''state="${INKLINE_TEXLIVE_STATE_DIR:-/home/root/.local/share/inkline-utilities/texlive/state}/prefix"
test -r "$state" || { echo 'Run texlive-install to download the TeX Live collection first.' >&2; exit 1; }
texlive_prefix=$(cat "$state")
export PATH="$texlive_prefix/bin/armhf-linux:$PATH"
export TEXLIVE_DOWNLOADER=curl
''' + f'''test -x "$texlive_prefix/bin/armhf-linux/{command}" || {{ echo 'Install this optional tool with: tlmgr install {optional}' >&2; exit 1; }}
exec "$texlive_prefix/bin/armhf-linux/{command}" "$@"''')
    finish(package, ["texlive-install", "texlive-remove", *tex_commands], perl_check() + '"$utility_root/bin/curl" --version\n"$utility_root/bin/texlive-install" --check')
    print("Staged eight utility packages. Run device checks before creating release archives.")


def archive(names=None, release="2026-09-13"):
    import re
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}(?:\.\d+)?", release):
        raise SystemExit("Use a release directory such as 2026-09-13.1.")
    output_dir = DIST.parent / release
    packages = [STAGE / name for name in names] if names else sorted(STAGE.iterdir())
    for package in packages:
        if not (package / "SHA256SUMS").is_file():
            raise SystemExit(f"Package has not been staged: {package.name}")
        if (output_dir / f"{package.name}-rm2.tar.gz").exists():
            raise SystemExit(f"{package.name} already exists in {release}; choose a new --release.")
    output_dir.mkdir(parents=True, exist_ok=True)
    for package in packages:
        destination = output_dir / f"{package.name}-rm2.tar.gz"
        def normalized(member):
            member.uid = member.gid = 0
            member.uname = member.gname = "root"
            return member
        with tarfile.open(destination, "w:gz") as output:
            output.add(package, arcname=package.name, filter=normalized)
        with destination.open("rb") as stream:
            checksum = hashlib.file_digest(stream, "sha256").hexdigest()
        write(destination.with_name(destination.name + ".sha256"), f"{checksum}  {destination.name}\n")
        print(destination.name, destination.stat().st_size)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", nargs="*", choices=["goblin-mosh", "mosh", "emacs", "goblin-view", "goblin-purrfect", "git", "python3", "texlive"], help="Archive all staged packages, or only the names listed.")
    parser.add_argument("--only", choices=["python3"], help="Stage only the selected utility.")
    parser.add_argument("--release", default="2026-09-13", help="New versioned output directory; existing archives are never replaced.")
    args = parser.parse_args()
    if args.archive is not None:
        archive(args.archive, args.release)
    elif args.only == "python3":
        build_python()
    else:
        build()
