#!/bin/sh
# Exercise the actual ARM binaries without logging in or writing to flash.
set -eu
umask 077
utility_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
"$utility_root/libexec/tailscale" version | head -n 1 | grep -qx '1.102.4'
"$utility_root/libexec/tailscaled" --version | head -n 1 | grep -qx '1.102.4'
scratch=$(mktemp -d /tmp/inkline-tailscale-check.XXXXXX)
daemon_pid=
cleanup() {
    if [ -n "$daemon_pid" ]; then
        kill "$daemon_pid" 2>/dev/null || :
        wait "$daemon_pid" 2>/dev/null || :
    fi
    rm -rf -- "$scratch"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
env HOME=/home/root TMPDIR="$scratch" TS_LOGS_DIR="$scratch" \
    TS_NO_LOGS_NO_SUPPORT=true TS_DISABLE_TAILDROP=true \
    "$utility_root/libexec/tailscaled" --tun=userspace-networking \
    --state=mem: --statedir="$scratch" --socket="$scratch/tailscaled.sock" \
    --port=0 --socks5-server=127.0.0.1:0 --outbound-http-proxy-listen=127.0.0.1:0 \
    --no-logs-no-support >"$scratch/daemon.log" 2>&1 &
daemon_pid=$!
count=0
while [ "$count" -lt 20 ]; do
    if "$utility_root/libexec/tailscale" --socket="$scratch/tailscaled.sock" \
        status --json >"$scratch/status.json" 2>/dev/null &&
        grep -q '"BackendState": "NeedsLogin"' "$scratch/status.json"; then
        printf '%s\n' 'Tailscale 1.102.4 ARM: userspace daemon and local API passed (RAM-only test).'
        exit 0
    fi
    kill -0 "$daemon_pid" 2>/dev/null || break
    count=$((count + 1))
    sleep 1
done
cat "$scratch/daemon.log" >&2
printf '%s\n' 'Tailscale userspace compatibility check failed.' >&2
exit 1
