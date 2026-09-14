#!/usr/bin/env python3
"""Check a packaged GoblinView through a real PTY, including its Inkline default."""
import fcntl
import os
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time

binary = sys.argv[1]
label = "inkline-mono-check-" + str(os.getpid())
answers = [(b"\x1b[16t", b"\x1b[6;20;10t"), (b"\x1b[18t", b"\x1b[8;30;100t"),
           (b"\x1b[?u", b"\x1b[?0u"), (b"\x1b[c", b"\x1b[?62;22c")]
paint = "printf '\\033[31;44mRED\\033[0m \\033[92mGREEN\\033[0m'; sleep 15"
for terminal, flags, mono in [("xterm", ["-m"], True), ("inkline", [], True), ("xterm", [], False)]:
    env = dict(os.environ, TERM="xterm-256color", TERM_PROGRAM=terminal)
    env.pop("GOBLIN_VIEW_MONO", None)
    pid, fd = pty.fork()
    if pid == 0:
        os.execve(binary, [binary, *flags, "-L", label, "new-session", "--", "/bin/sh", "-c", paint], env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 1000, 600))
    data = b""
    try:
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if not select.select([fd], [], [], 0.05)[0]: continue
            try: part = os.read(fd, 65536)
            except OSError: break
            if not part: break
            data += part
            for query, answer in answers:
                for _ in range(part.count(query)): os.write(fd, answer)
        parameters = []
        for match in re.finditer(rb"\x1b\[([0-9;:]*)m", data):
            parameters += [int(p.split(b":")[0] or 0) for p in match.group(1).split(b";")]
        colour = [p for p in parameters if p not in (0, 1, 3, 4, 7, 9, 39, 49)]
        assert b"RED" in data and b"GREEN" in data, "painted text is missing"
        assert bool(colour) != mono, (terminal, flags, colour)
        print(terminal, " ".join(flags) or "default", "monochrome" if mono else "colour", "passed", flush=True)
    finally:
        os.close(fd)
        subprocess.run([binary, "-L", label, "kill-server"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
        try: os.kill(pid, signal.SIGHUP)
        except ProcessLookupError: pass
        os.waitpid(pid, 0)
