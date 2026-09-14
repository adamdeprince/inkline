#!/bin/sh
# Run on reMarkable 2 against a staged Python bundle. Tests stay in RAM.
set -eu
test "$#" = 1 || { echo "Usage: test-python-bytecode.sh /path/to/python3-bundle" >&2; exit 2; }
payload=$(CDPATH= cd -- "$1" && pwd)
probe=$(mktemp -d /tmp/inkline-python-check.XXXXXX)
trap 'rm -rf -- "$probe"' EXIT
trap 'exit 130' HUP INT TERM
export TMPDIR="$probe" XDG_CACHE_HOME="$probe/cache"
cd "$probe"
printf 'value = 42\n' > cache_probe.py
"$payload/bin/python3" -c 'import sys, ssl, cache_probe; assert sys.dont_write_bytecode; assert cache_probe.value == 42; assert ssl.create_default_context().cert_store_stats()["x509_ca"] > 0'
env -u PYTHONDONTWRITEBYTECODE -u PIP_COMPILE "$payload/runtime/python/bin/python3.15" -c 'import sys, cache_probe; assert sys.dont_write_bytecode; assert cache_probe.value == 42'
"$payload/bin/python3" -m venv "$probe/venv"
env -u PYTHONDONTWRITEBYTECODE -u PIP_COMPILE "$probe/venv/bin/python" -c 'import sys, ssl, cache_probe; assert sys.dont_write_bytecode; assert sys.prefix != sys.base_prefix; assert ssl.create_default_context().cert_store_stats()["x509_ca"] > 0'
"$payload/bin/python3" - <<'PY'
from zipfile import ZipFile
files={
 'inkline_cache_probe.py':'value = 42\n',
 'inkline_cache_probe-0.0.0.dist-info/METADATA':'Metadata-Version: 2.1\nName: inkline-cache-probe\nVersion: 0.0.0\n',
 'inkline_cache_probe-0.0.0.dist-info/WHEEL':'Wheel-Version: 1.0\nGenerator: inkline-check\nRoot-Is-Purelib: true\nTag: py3-none-any\n',
}
files['inkline_cache_probe-0.0.0.dist-info/RECORD']=''.join(name+',,\n' for name in files)+'inkline_cache_probe-0.0.0.dist-info/RECORD,,\n'
with ZipFile('inkline_cache_probe-0.0.0-py3-none-any.whl','w') as archive:
 for name,text in files.items():archive.writestr(name,text)
PY
env -u PYTHONDONTWRITEBYTECODE -u PIP_COMPILE "$probe/venv/bin/python" -m pip install --no-index --no-deps "$probe/inkline_cache_probe-0.0.0-py3-none-any.whl"
"$probe/venv/bin/python" -c 'import inkline_cache_probe; assert inkline_cache_probe.value == 42'
find "$payload" "$probe" -name '*.pyc' -o -name __pycache__ > "$probe/cache-files.txt"
if test -s "$probe/cache-files.txt"; then cat "$probe/cache-files.txt"; exit 1; fi
"$payload/install-device.sh" --check
printf 'Python, bare interpreter, venv, and pip created no bytecode caches.\n'
