#!/bin/sh
set -eu
root=/home/root/.local/share/inkline-utilities/tailscale
unit=/etc/systemd/system/inkline-tailscale.service
expected="$root/current/inkline-tailscale.service"
wants=/etc/systemd/system/multi-user.target.wants
fail() { printf 'Tailscale: %s\n' "$*" >&2; exit 1; }
command -v systemctl >/dev/null || fail 'systemd is required.'
test -x /usr/bin/ssh || fail 'The stock SSH client is required.'
test -d /etc/systemd/system && test -w /etc/systemd/system || fail '/etc/systemd/system must be writable.'
if [ -e "$unit" ] || [ -L "$unit" ]; then
    test -L "$unit" && test "$(readlink "$unit")" = "$expected" || fail "An unrelated $unit already exists."
fi
fragment=$(systemctl show -p FragmentPath --value inkline-tailscale.service)
test ! -L "$wants" || fail 'The service enable directory must not be a symbolic link.'
if [ -e "$wants/inkline-tailscale.service" ] || [ -L "$wants/inkline-tailscale.service" ]; then
    test -L "$wants/inkline-tailscale.service" || fail 'An unrelated service enable file exists.'
    case "$(readlink "$wants/inkline-tailscale.service")" in
        ../inkline-tailscale.service|"$unit"|"$expected"|"$root"/releases/*/inkline-tailscale.service) ;;
        *) fail 'An unrelated service enable link exists.' ;;
    esac
fi
case "$fragment" in ''|"$unit"|"$expected"|"$root"/releases/*/inkline-tailscale.service) ;;
    *) fail "An unrelated service already exists at $fragment." ;;
esac
if systemctl is-active --quiet tailscaled.service; then
    fail 'Another tailscaled service is running. Stop it before installing this package.'
fi
test ! -L "$root/state" || fail 'The state directory must not be a symbolic link.'
if [ -e "$root/state" ]; then
    test -d "$root/state" || fail 'The state path is not a directory.'
fi
