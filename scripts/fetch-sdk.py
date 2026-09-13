#!/usr/bin/env python3
"""Download or verify the pinned official SDK. Does not execute the installer."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
from urllib.error import URLError
from urllib.request import urlopen


ROOT = Path(__file__).resolve().parent.parent


def verify(path, metadata):
    if path.stat().st_size != metadata["size_bytes"]:
        raise ValueError(f"Unexpected size for {path}")
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != metadata["sha256"]:
        raise ValueError(f"SHA-256 mismatch for {path}")


def main():
    metadata = json.loads((ROOT / "toolchains/sdk.lock.json").read_text())
    destination = ROOT / metadata["local_path"]
    if destination.exists():
        verify(destination, metadata)
        print(f"Verified cached SDK: {destination}")
        return

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=destination.parent, prefix=destination.name + ".", suffix=".part", delete=False
        ) as stream:
            temporary = Path(stream.name)
            print(f"Downloading {metadata['download_url']}", flush=True)
            with urlopen(metadata["download_url"], timeout=60) as response:
                total = 0
                next_report = 32 * 1024 * 1024
                while chunk := response.read(1024 * 1024):
                    total += len(chunk)
                    if total > metadata["size_bytes"]:
                        raise ValueError("Download exceeds the pinned SDK size")
                    stream.write(chunk)
                    if total >= next_report:
                        print(f"Downloaded {total // (1024 * 1024)} MiB", flush=True)
                        next_report += 32 * 1024 * 1024
        verify(temporary, metadata)
        temporary.replace(destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(f"Downloaded and verified SDK: {destination}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, URLError, ValueError, KeyError) as error:
        print(f"SDK error: {error}", file=sys.stderr)
        sys.exit(1)
