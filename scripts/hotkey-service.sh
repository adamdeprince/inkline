#!/bin/sh
# Install a boot-visible unit: systemd cannot discover a symlink into /home
# before that filesystem mounts. Keep only the executable in the home bundle.
set -eu
payload=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# Allows installation/removal tests in an isolated directory, without systemd.
units=${INKLINE_SYSTEMD_DIR:-/etc/systemd/system}
unit=$units/inkline-hotkey.service
wants=$units/multi-user.target.wants
enabled=$wants/inkline-hotkey.service
legacy=/home/root/.local/share/inkline/current/inkline-hotkey.service
marker='# Inkline managed boot service'
fail() { printf 'Inkline: %s\n' "$*" >&2; exit 1; }

# Check both names before replacing anything, including dangling symlinks.
if [ -L "$unit" ]; then
    test "$(readlink "$unit")" = "$legacy" || fail 'An unrelated keyboard launcher service already exists.'
elif [ -e "$unit" ]; then
    test -f "$unit" && test "$(sed -n '1p' "$unit")" = "$marker" || fail 'An unrelated keyboard launcher service already exists.'
fi
if [ -e "$enabled" ] || [ -L "$enabled" ]; then
    test -L "$enabled" || fail 'An unrelated keyboard launcher enable file already exists.'
    case "$(readlink "$enabled")" in
        ../inkline-hotkey.service|"$unit"|"$legacy") ;;
        *) fail 'An unrelated keyboard launcher enable link already exists.' ;;
    esac
fi

case "${1:-}" in
    check) ;;
    install)
        test "$(sed -n '1p' "$payload/inkline-hotkey.service")" = "$marker" || fail 'Unrecognized bundled keyboard service.'
        test ! -d "$unit" && test ! -d "$enabled" || fail 'A service path is a directory.'
        mkdir -p "$wants"
        temporary= temporary_link=
        cleanup() {
            test -z "$temporary" || rm -f -- "$temporary"
            test -z "$temporary_link" || rm -f -- "$temporary_link"
        }
        trap cleanup EXIT
        trap 'exit 130' HUP INT TERM
        temporary=$(mktemp "$units/.inkline-hotkey.XXXXXX")
        cp "$payload/inkline-hotkey.service" "$temporary"
        chmod 644 "$temporary"
        mv -f "$temporary" "$unit"
        temporary_link=$(mktemp "$wants/.inkline-hotkey.XXXXXX")
        rm "$temporary_link"
        ln -s ../inkline-hotkey.service "$temporary_link"
        mv -f "$temporary_link" "$enabled"
        ;;
    remove)
        rm -f -- "$enabled" "$unit"
        ;;
    *) fail 'Usage: hotkey-service.sh check|install|remove' ;;
esac
