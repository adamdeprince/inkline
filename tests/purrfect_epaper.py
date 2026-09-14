#!/usr/bin/env python3
"""Check the installed ARM editor's palette and launcher through real PTYs."""
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time


def check(binary, directory, arguments, monochrome):
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(directory)
        env = dict(os.environ, HOME=directory, XDG_CACHE_HOME=directory,
                   TERM="xterm-kitty", TERM_PROGRAM="inkline")
        os.execve(binary, [binary, *arguments], env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 1000, 600))
    data = b""
    try:
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if not select.select([fd], [], [], 0.05)[0]:
                continue
            try:
                part = os.read(fd, 65536)
            except OSError:
                break
            if not part:
                break
            data += part
            for query, answer in ((b"\x1b[16t", b"\x1b[6;20;10t"),
                                  (b"\x1b[18t", b"\x1b[8;30;100t")):
                for _ in range(part.count(query)):
                    os.write(fd, answer)
        colours = {tuple(map(int, match)) for match in
                   re.findall(rb"\x1b\[(?:38|48);2;(\d+);(\d+);(\d+)m", data)}
        assert colours, (arguments, "no editor palette", data[-500:])
        if monochrome:
            assert colours == {(0, 0, 0), (255, 255, 255)}, (arguments, colours)
        else:
            assert any(len(set(rgb)) > 1 for rgb in colours), (arguments, colours)
        os.write(fd, b"\x1b[18~")  # F7: exit the unmodified document.
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            done, status = os.waitpid(pid, os.WNOHANG)
            if done:
                assert os.waitstatus_to_exitcode(status) == 0, (arguments, status)
                pid = None
                break
            if select.select([fd], [], [], 0.05)[0]:
                try:
                    os.read(fd, 65536)
                except OSError:
                    pass
        assert pid is None, (arguments, "F7 did not exit")
        print("Purrfect", arguments or "no arguments", "epaper" if monochrome else "override", "passed", flush=True)
    finally:
        os.close(fd)
        if pid is not None:
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            os.waitpid(pid, 0)


binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="inkline-purrfect-check-", dir="/tmp") as directory:
    document = str(Path(directory) / "check.wp")
    env = dict(os.environ, HOME=directory, XDG_CACHE_HOME=directory)
    for args in (["new", document], ["inspect", document], ["verify-roundtrip", document],
                 ["export-latex", document, str(Path(directory) / "check.tex")], ["--version"]):
        subprocess.run([binary, *args], env=env, check=True, stdout=subprocess.DEVNULL, timeout=20)
    before = Path(document).read_bytes()
    for arguments, monochrome in (([], True), (["edit", document], True), ([document], True),
                                  (["edit", document, "--display", "amber"], False)):
        check(binary, directory, arguments, monochrome)
    assert Path(document).read_bytes() == before, "Display selection modified the document"
    print("Non-editing commands and document preservation passed.")
