#!/bin/sh
set -eu
unit=/etc/systemd/system/inkline-tailscale.service
expected=/home/root/.local/share/inkline-utilities/tailscale/current/inkline-tailscale.service
if [ -e "$unit" ] || [ -L "$unit" ]; then
    if [ ! -L "$unit" ] || [ "$(readlink "$unit")" != "$expected" ]; then
        printf '%s\n' 'Refusing to remove an unrelated inkline-tailscale.service.' >&2
        exit 1
    fi
    systemctl disable --now inkline-tailscale.service
    rm -f "$unit"
    systemctl daemon-reload
fi
printf '%s\n' 'Removing local Tailscale sign-in state. Remove the old device entry in the Tailscale admin console if desired.'
