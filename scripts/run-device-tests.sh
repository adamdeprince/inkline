#!/bin/sh
# Upload only the test executables to a fresh RAM-backed directory, run them,
# then remove that directory. Leaves the tablet UI and installed files alone.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
device_host=${1:-remarkable}
test_binaries='core_tests sixel_tests stream_tests pty_tests'
if [ -f build/tablet/ui_tests ]; then test_binaries="$test_binaries ui_tests"; fi
for test_binary in $test_binaries; do
    test -f "build/tablet/$test_binary" || {
        printf 'Missing build/tablet/%s; run scripts/build-tablet.sh first.\n' "$test_binary" >&2
        exit 1
    }
done
tar -C build/tablet -cf build/device-tests.tar $test_binaries
ssh -T -o BatchMode=yes -o StrictHostKeyChecking=yes -o ConnectTimeout=10 \
    -o ServerAliveInterval=5 -o ServerAliveCountMax=2 "$device_host" '
    set -eu
    test "$(uname -m)" = armv7l
    awk '\''$2 == "/tmp" && $3 == "tmpfs" { found=1 } END { exit !found }'\'' /proc/mounts
    umask 077
    test_dir=$(mktemp -d /tmp/rmt-tests.XXXXXX)
    trap '\''rm -rf -- "$test_dir"'\'' EXIT
    trap '\''exit 130'\'' HUP INT TERM
    tar -xf - -C "$test_dir"
    cd "$test_dir"
    export TMPDIR="$test_dir"
    export XDG_RUNTIME_DIR="$test_dir" XDG_CACHE_HOME="$test_dir/cache"
    mkdir "$XDG_CACHE_HOME"
    export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_DISABLE_DISK_CACHE=1
    export RMT_CHECK_NO_FLASH_WRITES=1
    for test_binary in core_tests sixel_tests stream_tests pty_tests; do
        printf "\n[%s]\n" "$test_binary"
        "./$test_binary"
    done
    if [ -x ./ui_tests ]; then ./ui_tests; fi
' < build/device-tests.tar
