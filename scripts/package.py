#!/usr/bin/env python3
"""Make a checksummed ARM bundle plus the Inkline source used to build it."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def repository_files():
    """Return tracked paths so local drafts and build debris never enter a release."""
    try:
        output = subprocess.check_output(
            ["git", "-C", str(ROOT), "ls-files", "-z"], stderr=subprocess.DEVNULL
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None, None
    files = {Path(value.decode()) for value in output.split(b"\0") if value}
    directories = {Path(".")}
    for path in files:
        directories.update(path.parents)
    return files, directories


def copy_repository_tree(name, destination, tracked_files):
    source = ROOT / name
    if tracked_files is None:
        shutil.copytree(source, destination)
        return
    destination.mkdir()
    for relative in sorted(path for path in tracked_files if path.is_relative_to(name)):
        path = ROOT / relative
        if not path.is_file():
            continue
        output = destination / relative.relative_to(name)
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, output)


def main():
    binary = ROOT / "build/tablet/inkline"
    if not binary.is_file() or binary.read_bytes()[:20] != bytes.fromhex("7f454c4601010100000000000000000002002800"):
        raise SystemExit("Build the ARM application first: scripts/build-tablet.sh")
    version = re.search(r"project\(inkline VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text())[1]
    dist = ROOT / "build/dist"
    dist.mkdir(parents=True, exist_ok=True)
    tracked_files, tracked_directories = repository_files()
    with tempfile.TemporaryDirectory(prefix=".package-", dir=dist) as temp:
        stage = Path(temp) / "inkline"
        stage.mkdir()
        shutil.copy2(binary, stage / "inkline")
        shutil.copy2(ROOT / "build/tablet/inkline-hotkey", stage / "inkline-hotkey")
        copy_repository_tree("assets", stage / "assets", tracked_files)
        for name in ["install-device.sh", "uninstall-device.sh", "install-manual.sh", "run-session.sh", "inkline-launcher", "inkline.service", "inkline-hotkey.service", "hotkey-service.sh", "shortcut-launch.sh", "shortcut-epaper.sh", "shortcut-restore.sh", "power-control.sh"]:
            shutil.copy2(ROOT / "scripts" / name, stage / name)
        shutil.copy2(ROOT / "scripts/utilities/prune-releases.sh", stage / "prune-releases.sh")
        for name in ["README.md", "LICENSE", "NOTICE", "THIRD_PARTY.md"]:
            shutil.copy2(ROOT / name, stage / name)
        copy_repository_tree("docs", stage / "docs", tracked_files)
        copy_repository_tree("scripts/usb", stage / "usb", tracked_files)
        copy_repository_tree("licenses", stage / "licenses", tracked_files)
        (stage / "INSTALL.md").write_text((ROOT / "docs/install.md").read_text().replace("(keyboard.md)", "(docs/keyboard.md)"))
        (stage / "manifest.json").write_text(json.dumps({
            "name": "Inkline", "version": version, "maturity": "preview",
            "model": "reMarkable 2", "firmware_line": "3.27", "target_firmware": "3.27.3.0",
            "qt_abi": "6.8", "ghostty_commit": "448062571c5edf010b7490d06869b88b5ebf8f80",
            "license": "GPL-3.0-or-later",
        }, indent=2) + "\n")
        with tarfile.open(stage / "inkline-source.tar.gz", "w:gz") as source:
            for name in ["CMakeLists.txt", "README.md", "LICENSE", "NOTICE", "THIRD_PARTY.md", "licenses", ".gitignore", ".gitattributes", "src", "include", "tests", "scripts", "cmake", "patches", "docs", "toolchains", "assets"]:
                def source_filter(info):
                    if "__pycache__" in info.name or info.name.endswith(".pyc"):
                        return None
                    if tracked_files is not None:
                        relative = Path(info.name).relative_to("inkline-source")
                        if relative not in tracked_files and relative not in tracked_directories:
                            return None
                    return info
                source.add(ROOT / name, arcname="inkline-source/" + name, filter=source_filter)
        ghostty = ROOT / ".cache/ghostty"
        if not ghostty.is_dir():
            ghostty = ROOT / "build/tablet/_deps/ghostty-src"
        if (ghostty / "LICENSE").read_bytes() != (stage / "licenses/Ghostty-MIT.txt").read_bytes():
            raise SystemExit("Update the embedded Ghostty license before packaging this engine revision")
        for entry in json.loads((stage / "licenses/catalog.json").read_text()):
            for name in entry["files"]:
                if not (stage / name).is_file():
                    raise SystemExit(f"Missing packaged notice: {name}")
        # This project's dedicated Zig cache holds the hash-addressed source
        # archives used by the pinned engine (including Unicode dependencies).
        dependencies = ROOT / ".cache/zig-global/p"
        if dependencies.is_dir():
            (stage / "dependency-sources").mkdir()
            for archive in sorted(dependencies.glob("*.tar.gz")):
                shutil.copy2(archive, stage / "dependency-sources" / archive.name)
        # Include the exact patched upstream tree as well as its dependency
        # lockfile. Build caches and Git metadata are never part of a release.
        with tarfile.open(stage / "ghostty-source.tar.gz", "w:gz") as source:
            def upstream_filter(info):
                if any(part in (".git", ".zig-cache", "zig-out", ".DS_Store") for part in Path(info.name).parts):
                    return None
                return info
            source.add(ghostty, arcname="ghostty-source", filter=upstream_filter)
        for path in stage.rglob("*"):
            if path.is_file():
                path.chmod(0o700 if path.name in ("inkline", "inkline-hotkey", "inkline-launcher", "inkline-usb", "inkline-type", "keyboard-send", "inkline-usb-daemon") or path.suffix == ".sh" else 0o600)
        files = sorted(p for p in stage.rglob("*") if p.is_file())
        (stage / "SHA256SUMS").write_text("".join(f"{digest(p)}  {p.relative_to(stage)}\n" for p in files))
        archive = dist / "inkline-rm2.tar.gz"
        temporary = dist / ".inkline-rm2.tar.gz.new"
        with tarfile.open(temporary, "w:gz") as output:
            output.add(stage, arcname="inkline")
        temporary.replace(archive)
        (dist / "inkline-rm2.tar.gz.sha256").write_text(f"{digest(archive)}  inkline-rm2.tar.gz\n")
    print(f"{archive} ({archive.stat().st_size // (1024 * 1024)} MiB)")
    print("Contains the binary, installer, launcher, uninstall procedure and source archives.")


if __name__ == "__main__":
    main()
