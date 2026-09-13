#!/usr/bin/env python3
"""Resolve and fetch private ARM runtimes from Debian's Bookworm indexes.

No Debian maintainer script runs, and no Debian package manager is installed on
the tablet. libc, libgcc, libstdc++, zlib and OpenSSL use the stock firmware ABI.
The generated lock records each downloaded binary and its matching source.
"""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import io
import json
import lzma
import os
from pathlib import Path
import re
import shutil
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / ".cache/utilities/debian"
SYSTEM = {"libc6", "libgcc-s1", "libstdc++6", "zlib1g", "libssl3"}
PERL = ["perl-base", "perl-modules-5.36", "libperl5.36"]
GROUPS = {"perl": PERL, "mosh": ["mosh", *PERL],
          "emacs": ["emacs-nox", "emacs-common", "emacs-el", "emacs-bin-common"],
          "git": ["git", "git-man", "liberror-perl", *PERL],
          "texlive": ["curl", *PERL],
          "locale": ["libc-bin"], "licenses": ["base-files"]}


def paragraphs(path):
    for block in lzma.open(path, "rt").read().split("\n\n"):
        fields, key = {}, None
        for line in block.splitlines():
            if line.startswith(" ") and key:
                fields[key] += "\n" + line[1:]
            elif ":" in line:
                key, value = line.split(":", 1)
                fields[key] = value.lstrip()
        if fields:
            yield fields


def version_key(version):
    # All candidates come from the same stable release; its security updates
    # retain the upstream version and increment the Debian update revision.
    epoch, separator, rest = version.partition(":")
    if not separator:
        epoch, rest = "0", epoch
    return (int(epoch), tuple((1, int(p)) if p.isdigit() else (0, p)
                            for p in re.split(r"(\d+)", rest)))


def metadata():
    binaries, sources = {}, {}
    for suite, base in [("bookworm", "https://deb.debian.org/debian/"),
                        ("bookworm-security", "https://security.debian.org/debian-security/")]:
        for fields in paragraphs(ROOT / f".cache/debian-{suite}-armhf-Packages.xz"):
            name = fields["Package"]
            fields["base_url"] = base
            if name not in binaries or version_key(fields["Version"]) > version_key(binaries[name]["Version"]):
                binaries[name] = fields
        for fields in paragraphs(ROOT / f".cache/debian-{suite}-Sources.xz"):
            fields["base_url"] = base
            sources[fields["Package"], fields["Version"]] = fields
    return binaries, sources


def resolve(seeds, binaries):
    selected, pending = set(), list(seeds)
    while pending:
        name = pending.pop()
        if name in selected or name in SYSTEM:
            continue
        fields = binaries[name]
        selected.add(name)
        for dependency in (fields.get("Depends", "") + "," + fields.get("Pre-Depends", "")).split(","):
            candidates = [re.split(r"[ :(]", value.strip())[0] for value in dependency.split("|")]
            for candidate in candidates:
                if candidate in binaries and not candidate.endswith("-dev") and (candidate.startswith("lib") or candidate in PERL):
                    pending.append(candidate)
                    break
    return sorted(selected)


def fetch(item):
    destination = CACHE / "downloads" / item["filename"]
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists():
        temporary = destination.with_name(destination.name + ".partial")
        with urllib.request.urlopen(item["url"], timeout=60) as response, temporary.open("wb") as output:
            while block := response.read(1024 * 1024):
                output.write(block)
        temporary.replace(destination)
    with destination.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != item["sha256"]:
            raise RuntimeError("Checksum mismatch: " + str(destination))
    return destination


def extract_deb(archive, destination):
    with archive.open("rb") as stream:
        if stream.read(8) != b"!<arch>\n":
            raise ValueError("Invalid Debian archive")
        while header := stream.read(60):
            name = header[:16].decode().strip().rstrip("/")
            size = int(header[48:58])
            data = stream.read(size)
            if size % 2:
                stream.read(1)
            if name.startswith("data.tar"):
                with tarfile.open(fileobj=io.BytesIO(data)) as contents:
                    def private_tree(member, path):
                        # Debian's absolute links refer to the system prefix.
                        # Resolve them inside this private runtime instead.
                        if member.issym() and member.linkname.startswith("/"):
                            member = member.replace(linkname=os.path.relpath(
                                member.linkname.lstrip("/"), str(Path(member.name).parent)))
                        elif member.islnk():
                            member = member.replace(linkname=member.linkname.lstrip("/"))
                        return tarfile.data_filter(member, path)
                    contents.extractall(destination, filter=private_tree)
                return
    raise ValueError("Missing Debian payload: " + str(archive))


def make_lock():
    binaries, sources = metadata()
    groups = {name: resolve(seeds, binaries) for name, seeds in GROUPS.items()}
    packages, source_files = {}, {}
    for name in sorted(set().union(*map(set, groups.values()))):
        fields = binaries[name]
        source_name, _, source_version = fields.get("Source", name).partition(" ")
        source_version = source_version.strip("()") or fields["Version"]
        source = sources[source_name, source_version]
        packages[name] = {"version": fields["Version"], "filename": Path(fields["Filename"]).name,
                          "url": fields["base_url"] + fields["Filename"], "sha256": fields["SHA256"],
                          "source": source_name, "source_version": source_version}
        for entry in source["Checksums-Sha256"].splitlines():
            if not entry.strip():
                continue
            checksum, size, filename = entry.split()
            source_files[filename] = {"filename": filename, "sha256": checksum, "size": int(size),
                                      "url": source["base_url"] + source["Directory"] + "/" + filename}
    lock = {"architecture": "armhf", "groups": groups, "stock_runtime_packages": sorted(SYSTEM),
            "packages": packages, "sources": source_files}
    return lock


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--refresh-lock", action="store_true", help="Resolve new versions from current Bookworm indexes.")
    args = parser.parse_args()
    lock_path = ROOT / "scripts/utilities/debian.lock.json"
    if args.refresh_lock or not lock_path.exists():
        for suite, base in [("bookworm", "https://deb.debian.org/debian/dists/bookworm/main/"),
                            ("bookworm-security", "https://security.debian.org/debian-security/dists/bookworm-security/main/")]:
            for local, remote in [("armhf-Packages", "binary-armhf/Packages.xz"), ("Sources", "source/Sources.xz")]:
                target = ROOT / f".cache/debian-{suite}-{local}.xz"
                if args.refresh_lock or not target.exists():
                    with urllib.request.urlopen(base + remote, timeout=60) as response, target.open("wb") as output:
                        shutil.copyfileobj(response, output)
        lock = make_lock()
        lock_path.write_text(json.dumps(lock, indent=2) + "\n")
    else:
        lock = json.loads(lock_path.read_text())
    CACHE.mkdir(parents=True, exist_ok=True)
    (CACHE / "lock.json").write_text(json.dumps(lock, indent=2) + "\n")
    packages, source_files, groups = lock["packages"], lock["sources"], lock["groups"]
    print(f"Fetching {len(packages)} binary packages and {len(source_files)} matching source files.", flush=True)
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(fetch, [*packages.values(), *source_files.values()]))
    for group, names in groups.items():
        destination = CACHE / "groups" / group
        if destination.exists():
            shutil.rmtree(destination)
        destination.mkdir(parents=True, exist_ok=True)
        for name in names:
            extract_deb(CACHE / "downloads" / packages[name]["filename"], destination)
        print(f"Prepared {group}: {len(names)} packages.", flush=True)


if __name__ == "__main__":
    main()
