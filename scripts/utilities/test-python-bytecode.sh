#!/bin/sh
# Run on reMarkable 2 against a disposable bundle staged in RAM.
set -eu
test "$#" = 1 || { echo "Usage: test-python-bytecode.sh /tmp/staging/python3" >&2; exit 2; }
payload=$(CDPATH= cd -- "$1" && pwd)
test "$(stat -f -c %T "$payload")" = tmpfs || { echo 'Stage the bundle on tmpfs first: stock-Python tests deliberately create bytecode.' >&2; exit 1; }
test -z "$(find "$payload/runtime/python" -name '*.pyc' -o -name __pycache__)" || { echo 'Use a pristine staged bundle without bytecode caches.' >&2; exit 1; }
probe=$(mktemp -d /tmp/inkline-python-check.XXXXXX)
cleanup() {
    find "$payload/runtime/python" -type d -name __pycache__ -prune -exec rm -rf -- '{}' +
    rm -rf -- "$probe"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
export TMPDIR="$probe" XDG_CACHE_HOME="$probe/cache"
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
unset PYTHONPYCACHEPREFIX PIP_COMPILE PIP_NO_COMPILE
cd "$probe"
printf 'value = 42\n' > cache_probe.py
export PYTHONDONTWRITEBYTECODE=1 PIP_NO_CACHE_DIR=1
"$payload/bin/python3" -c 'import sys, ssl, cache_probe; assert sys.dont_write_bytecode; assert cache_probe.value == 42; assert ssl.create_default_context().cert_store_stats()["x509_ca"] > 0'
test ! -e __pycache__
# No hidden policy: both the wrapper and the bare interpreter keep stock semantics.
env -u PYTHONDONTWRITEBYTECODE "$payload/bin/python3" -X "pycache_prefix=$probe/stock-cache" -c 'import sys, cache_probe; assert not sys.dont_write_bytecode'
test -n "$(find "$probe/stock-cache" -name 'cache_probe*.pyc')"
"$payload/runtime/python/bin/python3.15" -I -X "pycache_prefix=$probe/isolated-cache" -c 'import sys; assert not sys.dont_write_bytecode and sys.flags.isolated'
# Upstream ensurepip uses isolated mode: its bytecode writes are expected here.
"$payload/bin/python3" -m venv "$probe/venv"
"$probe/venv/bin/python" -c 'import sys, ssl, cache_probe; assert sys.dont_write_bytecode; assert sys.prefix != sys.base_prefix; assert ssl.create_default_context().cert_store_stats()["x509_ca"] > 0'
test ! -e __pycache__
"$payload/bin/python3" - <<'PY'
from zipfile import ZipFile
files = {
 'inkline_cache_probe.py': 'value = 42\n',
 'inkline_cache_probe-0.0.0.dist-info/METADATA': 'Metadata-Version: 2.1\nName: inkline-cache-probe\nVersion: 0.0.0\n',
 'inkline_cache_probe-0.0.0.dist-info/WHEEL': 'Wheel-Version: 1.0\nGenerator: inkline-check\nRoot-Is-Purelib: true\nTag: py3-none-any\n',
}
files['inkline_cache_probe-0.0.0.dist-info/RECORD'] = ''.join(name+',,\n' for name in files)+'inkline_cache_probe-0.0.0.dist-info/RECORD,,\n'
with ZipFile('inkline_cache_probe-0.0.0-py3-none-any.whl', 'w') as archive:
    for name, text in files.items(): archive.writestr(name, text)
from pip._internal.commands import create_command
options, _ = create_command('install').parse_args([])
assert not options.cache_dir
PY
"$probe/venv/bin/python" -m pip install --no-index --no-deps --no-compile "$probe/inkline_cache_probe-0.0.0-py3-none-any.whl"
"$probe/venv/bin/python" -c 'import inkline_cache_probe; assert inkline_cache_probe.value == 42'
test -z "$(find "$probe/venv" -name 'inkline_cache_probe*.pyc')"
# The two environment settings do not disable pip's explicit install-time compilation.
"$probe/venv/bin/python" -m pip install --no-index --no-deps --force-reinstall "$probe/inkline_cache_probe-0.0.0-py3-none-any.whl"
test -n "$(find "$probe/venv" -name 'inkline_cache_probe*.pyc')"
"$payload/install-device.sh" --check
printf 'Stock Python: optional import cache policy, isolated mode, venv TLS, pip cache preference and --no-compile passed in RAM.\n'
