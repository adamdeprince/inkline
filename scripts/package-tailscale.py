#!/usr/bin/env python3
"""Package verified upstream ARM binaries and their notices for reMarkable 2.

Requires Python 3.12+, curl, and Go (for reading build metadata and retrieving
hash-verified dependency sources). No SDK, npm install, or binary rebuild.
"""
import argparse
import base64
import csv
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parent.parent
RECIPES = ROOT / "scripts/utilities/tailscale"
CACHE = ROOT / ".cache/utilities/tailscale"
STAGE = ROOT / "build/utilities/packages/tailscale"
LOCK = json.loads((RECIPES / "inputs.lock.json").read_text())
OFFLINE = False
NOTICE_NAMES = ("license", "copying", "notice", "copyright", "authors", "patents")


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(0o644)


def fetch(filename, url, digest, algorithm="sha256"):
    destination = CACHE / filename
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists():
        if OFFLINE:
            raise RuntimeError(f"Offline input is missing: {filename}")
        temporary = destination.with_suffix(destination.suffix + ".part")
        subprocess.run(["curl", "-fsSL", "--retry", "3", "-H", "Accept: application/vnd.github.raw+json", "--proto", "=https",
                        "--proto-redir", "=https", "-o", str(temporary), url], check=True)
        temporary.rename(destination)
    with destination.open("rb") as stream:
        actual = hashlib.file_digest(stream, algorithm).hexdigest()
    if actual != digest:
        raise RuntimeError(f"Checksum mismatch: {filename}")
    return destination


def unpack(archive, destination):
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    with tarfile.open(archive) as source:
        source.extractall(destination, filter="data")
    return destination


def copy_notices(source, destination):
    files = []
    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(source)
        if (path.name.lower().startswith(NOTICE_NAMES) or
                any(p.lower() in ("license", "licenses") for p in relative.parts[:-1])):
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
            target.chmod(0o644)
            files.append(str(relative))
    if not files:
        raise RuntimeError(f"No license files found for {source.name}")
    return files


def json_stream(text):
    decoder = json.JSONDecoder()
    while text.strip():
        item, end = decoder.raw_decode(text.lstrip())
        yield item
        text = text.lstrip()[end:]


def web_notices(source):
    # The embedded web client also contains JS libraries and the Inter font.
    # Fetch source archives only; never run third-party npm lifecycle scripts.
    entries = {}
    for block in re.split(r"\n(?=\S)", (source / "client/web/yarn.lock").read_text()):
        lines = block.strip().splitlines()
        if not lines or lines[0].startswith("#") or not lines[0].endswith(":"):
            continue
        aliases = next(csv.reader([lines[0][:-1]], skipinitialspace=True))
        item = {"dependencies": []}
        dependencies = False
        for line in lines[1:]:
            if line.startswith("  ") and not line.startswith("    "):
                parts = shlex.split(line)
                dependencies = parts[0] in ("dependencies:", "optionalDependencies:")
                if len(parts) == 2:
                    item[parts[0]] = parts[1]
            elif dependencies and line.startswith("    "):
                name, version = shlex.split(line)
                item["dependencies"].append(f"{name}@{version}")
        for alias in aliases:
            entries[alias] = item
    dependencies = json.loads((source / "client/web/package.json").read_text())["dependencies"]
    pending = [f"{name}@{version}" for name, version in dependencies.items()]
    seen = set()
    index = []
    missing = []
    while pending:
        alias = pending.pop()
        item = entries[alias]
        name = alias.rsplit("@", 1)[0]
        identity = f'{name}@{item["version"]}'
        if identity in seen:
            continue
        seen.add(identity)
        algorithm, digest = item["integrity"].split("-", 1)
        filename = "web/" + identity.replace("/", "_") + ".tgz"
        archive = fetch(filename, item["resolved"].split("#")[0],
                        base64.b64decode(digest).hex(), algorithm)
        unpacked = unpack(archive, CACHE / "web/source" / identity.replace("/", "_"))
        module = unpacked / "package"
        if not module.exists():
            module = next(p for p in unpacked.iterdir() if p.is_dir())
        metadata = json.loads((module / "package.json").read_text())
        if identity in ("wouter@2.12.1", "client-only@0.0.1"):
            # These exact archives declare their license in package.json but
            # omit its text. Retain that declaration and the standard terms.
            declared, notice = ("ISC", "wouter-ISC.txt") if name == "wouter" else ("MIT", "MIT.txt")
            if metadata["license"] != declared:
                raise RuntimeError(f"{name}'s license declaration changed")
            target = STAGE / "licenses/web" / identity
            target.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(module / "package.json", target / "package.json")
            shutil.copyfile(CACHE / notice, target / f"{declared}.txt")
            files = ["package.json", f"{declared}.txt"]
        elif name.startswith("@radix-ui/") or name in ("react-style-singleton", "react-remove-scroll-bar"):
            if metadata["license"] != "MIT":
                raise RuntimeError(f"{name}'s license declaration changed")
            target = STAGE / "licenses/web" / identity
            target.mkdir(parents=True, exist_ok=True)
            notice = "radix-LICENSE" if name.startswith("@radix-ui/") else f"{name}-LICENSE"
            shutil.copyfile(CACHE / notice, target / "LICENSE")
            shutil.copyfile(module / "package.json", target / "package.json")
            files = ["LICENSE", "package.json"]
        else:
            try:
                files = copy_notices(module, STAGE / "licenses/web" / identity)
            except RuntimeError:
                missing.append(identity)
                files = []
        index.append({"name": name, "version": item["version"], "integrity": item["integrity"],
                      "license": metadata.get("license"), "files": files})
        pending.extend(item["dependencies"])
    if missing:
        raise RuntimeError("Missing web licenses: " + ", ".join(missing))
    write(STAGE / "licenses/web.json", json.dumps(sorted(index, key=lambda p: p["name"]), indent=2) + "\n")
    shutil.copyfile(CACHE / "Inter-LICENSE.txt", STAGE / "licenses/Inter-LICENSE.txt")


def stage():
    for item in LOCK["inputs"]:
        path = fetch(item["filename"], item["url"], item["sha256"])
        if path.stat().st_size != item["size"]:
            raise RuntimeError(f"Size mismatch: {path.name}")
    version = LOCK["version"]
    binaries = unpack(CACHE / f"tailscale_{version}_arm.tgz", CACHE / "binary-input") / f"tailscale_{version}_arm"
    source = unpack(CACHE / f"tailscale-{version}-source.tar.gz", CACHE / "source-input") / f"tailscale-{version}"
    go_source = unpack(CACHE / f'{LOCK["go_version"]}.src.tar.gz', CACHE / "go-input") / "go"
    if STAGE.exists():
        shutil.rmtree(STAGE)
    STAGE.mkdir(parents=True)
    found = {}
    for name in ("tailscale", "tailscaled"):
        executable = binaries / name
        metadata = subprocess.check_output(["go", "version", "-m", str(executable)], text=True)
        if metadata.splitlines()[0].split()[-1] != LOCK["go_version"]:
            raise RuntimeError("Unexpected Go runtime version")
        for requirement in ("CGO_ENABLED=0", "GOARCH=arm", "GOOS=linux", "GOARM=5"):
            if requirement not in metadata:
                raise RuntimeError(f"Unexpected ARM binary settings: {requirement}")
        for line in metadata.splitlines():
            parts = line.split()
            if parts and parts[0] == "dep":
                found[parts[1]] = {"module": parts[1], "version": parts[2], "sum": parts[3]}
        target = STAGE / "libexec" / name
        target.parent.mkdir(exist_ok=True)
        shutil.copyfile(executable, target)
        target.chmod(0o755)
        write(STAGE / f"licenses/{name}-build-info.txt", metadata.replace(str(executable), name))
    if found != {m["module"]: m for m in LOCK["modules"]}:
        raise RuntimeError("Binary dependency metadata differs from the lock")
    env = dict(os.environ, GOPATH=str(CACHE / "go"), GOMODCACHE=str(CACHE / "go/pkg/mod"),
               GOCACHE=str(CACHE / "go-build"), GOWORK="off")
    if OFFLINE:
        env.update(GOPROXY="off", GOSUMDB="off", GOTOOLCHAIN="local")
    modules = subprocess.check_output(["go", "mod", "download", "-json"] +
                                     [f'{m["module"]}@{m["version"]}' for m in LOCK["modules"]],
                                     env=env, cwd=CACHE, text=True)
    index = []
    for item in json_stream(modules):
        expected = found[item["Path"]]
        if item.get("Sum") != expected["sum"]:
            raise RuntimeError(f'Dependency checksum mismatch: {item["Path"]}')
        directory = f'{item["Path"]}@{item["Version"]}'
        files = copy_notices(Path(item["Dir"]), STAGE / "licenses/modules" / directory)
        index.append({**expected, "files": files})
    if len(index) != len(found):
        raise RuntimeError("Incomplete dependency notices")
    write(STAGE / "licenses/modules.json", json.dumps(index, indent=2) + "\n")
    copy_notices(source, STAGE / "licenses/tailscale")
    copy_notices(go_source, STAGE / "licenses/go")
    web_notices(source)
    shutil.copyfile(source / "LICENSE", STAGE / "LICENSE")
    (STAGE / "licenses/inkline").mkdir()
    shutil.copyfile(ROOT / "LICENSE", STAGE / "licenses/inkline/LICENSE")
    for name in ("README.txt", "NOTICE.txt", "inputs.lock.json", "inkline-tailscale.service",
                 "pre-install.sh", "post-install.sh", "pre-uninstall.sh", "check.sh"):
        shutil.copyfile(RECIPES / name, STAGE / name)
        (STAGE / name).chmod(0o755 if name.endswith(".sh") else 0o644)
    for name in ("install-device.sh", "uninstall-device.sh"):
        shutil.copyfile(ROOT / "scripts/utilities" / name, STAGE / name)
        (STAGE / name).chmod(0o755)
    (STAGE / "bin").mkdir()
    for name in ("tailscale", "tailscale-ssh"):
        shutil.copyfile(RECIPES / name, STAGE / "bin" / name)
        (STAGE / "bin" / name).chmod(0o755)
    write(STAGE / "package-name", "tailscale\n")
    write(STAGE / "version", LOCK["package_version"] + "\n")
    write(STAGE / "commands", "tailscale\ntailscale-ssh\n")
    write(STAGE / "manifest.json", json.dumps({"name": "tailscale", "version": LOCK["package_version"],
          "architecture": "linux-arm-static-GOARM5", "model": "reMarkable 2", "firmware_line": "3.27",
          "tested_firmware": "3.27.3.0", "networking": "userspace", "upstream_binaries_modified": False}, indent=2) + "\n")
    checksums = []
    for path in sorted(STAGE.rglob("*")):
        if path.is_file():
            with path.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            checksums.append(f"{digest}  {path.relative_to(STAGE)}\n")
    write(STAGE / "SHA256SUMS", "".join(checksums))
    print(f"Staged Tailscale {version}; {len(index)} Go module notice sets plus Go and web licenses.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", action="store_true", help="Archive an already staged and device-tested package.")
    parser.add_argument("--offline", action="store_true", help="Use only cached inputs; fail instead of accessing the network.")
    parser.add_argument("--release", default="2026-09-14.1")
    args = parser.parse_args()
    OFFLINE = args.offline
    if args.archive:
        spec = importlib.util.spec_from_file_location("utility_packages", ROOT / "scripts/package-utilities.py")
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        helper.archive(["tailscale"], args.release)
    else:
        stage()
