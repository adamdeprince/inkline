#!/bin/sh
set -eu
umask 077
payload=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
"$payload/pre-install.sh"
root=/home/root/.local/share/inkline-utilities/tailscale
unit=/etc/systemd/system/inkline-tailscale.service
test "$(id -u)" = 0
test "$(cat "$root/.inkline-utility")" = tailscale
mkdir -p "$root/state"
chmod 0700 "$root/state"
if [ ! -L "$unit" ]; then
    ln -s "$root/current/inkline-tailscale.service" "$unit"
fi
wants=/etc/systemd/system/multi-user.target.wants
mkdir -p "$wants"
# Keep boot activation linked to the managed unit, not an old release path.
ln -s ../inkline-tailscale.service "$wants/.inkline-tailscale.$$"
mv -Tf "$wants/.inkline-tailscale.$$" "$wants/inkline-tailscale.service"
systemctl daemon-reload
systemctl restart inkline-tailscale.service
count=0
while [ "$count" -lt 20 ]; do
    if systemctl is-active --quiet inkline-tailscale.service &&
        "$root/current/bin/tailscale" status --json >/dev/null 2>&1; then
        printf '%s\n' 'Tailscale userspace service is running and enabled at boot.'
        printf '%s\n' 'Sign in: tailscale up --accept-dns=false --accept-routes=false --hostname=remarkable2'
        exit 0
    fi
    count=$((count + 1))
    sleep 1
done
printf '%s\n' 'Tailscale did not become ready. Check systemctl status inkline-tailscale.service.' >&2
exit 1
