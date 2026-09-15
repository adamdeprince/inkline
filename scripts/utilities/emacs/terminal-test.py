#!/usr/bin/env python3
"""Exercise the real ARM Emacs through a PTY; all editor writes stay in /tmp."""
import fcntl
import json
import os
from pathlib import Path
import pty
import select
import signal
import struct
import sys
import tempfile
import termios
import time

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="inkline-emacs-terminal-", dir="/tmp") as temporary:
    document = Path(temporary) / "unicode.txt"
    expression = "(progn (find-file " + json.dumps(str(document)) + ') (message "INKLINE-EMACS-READY"))'
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(temporary)
        os.execve(binary, [binary, "-Q", "--eval", expression],
                  dict(os.environ, HOME=temporary, TMPDIR=temporary,
                       XDG_CACHE_HOME=temporary, TERM="xterm-256color", TERM_PROGRAM="inkline"))
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 1000, 600))
    transcript = bytearray()
    try:
        deadline = time.monotonic() + 20
        while b"INKLINE-EMACS-READY" not in transcript and time.monotonic() < deadline:
            if select.select([fd], [], [], 0.1)[0]:
                try:
                    transcript.extend(os.read(fd, 65536))
                except OSError as error:
                    raise AssertionError(bytes(transcript[-2000:])) from error
        assert b"INKLINE-EMACS-READY" in transcript, bytes(transcript[-2000:])
        text = "Emacs 31.1 on Inkline: λ 日本語\n"
        os.write(fd, text.encode() + b"\x18\x13\x18\x03")  # save, then exit
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            done, status = os.waitpid(pid, os.WNOHANG)
            if done:
                pid = None
                assert os.waitstatus_to_exitcode(status) == 0, status
                break
            if select.select([fd], [], [], 0.1)[0]:
                try:
                    transcript.extend(os.read(fd, 65536))
                except OSError:
                    pass
        assert pid is None, bytes(transcript[-2000:])
        assert document.read_text() == text, document.read_bytes()
        print("Emacs PTY: no-X startup, Unicode editing, save and clean keyboard exit passed.")
    finally:
        os.close(fd)
        if pid is not None:
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            os.waitpid(pid, 0)
