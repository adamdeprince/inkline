#!/bin/sh
# Host-side installation: the upload and preflight run in tablet RAM.
set -eu
project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo 'Usage: scripts/install.sh root@TABLET_IP [--check]' >&2
    echo 'Build and package first: scripts/build-tablet.sh && python3 scripts/package.py' >&2
    exit 2
fi
host=$1
case "$host" in -*) echo 'Invalid SSH host.' >&2; exit 2 ;; esac
mode=${2:-install}
case "$mode" in install|--check) ;; *) exit 2 ;; esac
archive="$project/build/dist/inkline-rm2.tar.gz"
test -r "$archive" || { echo 'Run python3 scripts/package.py first.' >&2; exit 1; }
# The SSH host key is never accepted or replaced automatically. Establish SSH
# access and verify the device fingerprint before running this script.
if [ "$mode" = --check ]; then action='./install-device.sh --check'; else action='./install-device.sh'; fi
ssh -T -o StrictHostKeyChecking=yes -o ConnectTimeout=10 -o ServerAliveInterval=5 \
    -o ServerAliveCountMax=2 "$host" "
    set -eu
    umask 077
    awk '\$2 == \"/tmp\" && \$3 == \"tmpfs\" { ok=1 } END { exit !ok }' /proc/mounts
    stage=\$(mktemp -d /tmp/inkline-install.XXXXXX)
    trap 'rm -rf -- \"\$stage\"' EXIT
    trap 'exit 130' HUP INT TERM
    tar -xzf - -C \"\$stage\"
    cd \"\$stage/inkline\"
    $action
" < "$archive"
