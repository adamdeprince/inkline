#!/usr/bin/env python3
"""Exercise command delivery and malformed requests through a real local socket."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="inkline-control-") as directory:
    address = str(Path(directory) / "control")
    env = dict(os.environ, INKLINE_CONTROL_SOCKET=address, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", XDG_CONFIG_HOME=directory)
    server = subprocess.Popen([binary, "--demo", "--quit-after", "20"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        deadline = time.monotonic() + 10
        while not Path(address).exists() and time.monotonic() < deadline and server.poll() is None:
            time.sleep(0.05)
        assert Path(address).exists(), "control server failed to start"
        def request(value):
            with socket.socket(socket.AF_UNIX) as client:
                client.settimeout(3)
                client.connect(address)
                client.sendall(json.dumps(value).encode() + b"\n")
                return client.recv(1024)
        for invalid in ([], ["unknown"], ["--open-program", None], ["--open-program", ""], ["--open-program", "a\0b"], ["--open-program", "/missing-inkline-test-program"], ["--open-program", "/bin/echo", *(["x"] * 68)]):
            assert request(invalid) != b"OK\n", invalid
        assert request(["--redraw"]) == b"OK\n"
        # A maximum-length binding plus Bash's four fixed argv entries.
        assert request(["--open-program", "/bin/bash", "-ic", 'exec "$@"', "inkline-shortcut", "echo", *(["x"] * 63)]) == b"OK\n"
        for _ in range(7):
            result = subprocess.run([binary, "--open-program", "/bin/echo", "literal argument"], env=env, capture_output=True, timeout=10)
            assert result.returncode == 0, result.stderr
        assert request(["--open-program", "/bin/echo"]) != b"OK\n", "full terminal slots were not enforced"
        assert request(["--redraw"]) == b"OK\n", "invalid requests broke the server"
        print("Control socket: literal arguments, malformed requests, redraw and nine-slot limit passed.")
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
