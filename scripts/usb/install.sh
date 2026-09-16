#!/bin/sh
# Install namespaced commands without replacing the separate Outpost tools.
set -eu
ROOT=/home/root/.local/share/inkline
UNIT=/etc/systemd/system/inkline-usb-keyboard.service
BIN=/home/root/.local/bin
fail() { echo "Inkline USB install: $*" >&2; exit 1; }
check() {
    if [ -e "$UNIT" ] || [ -L "$UNIT" ]; then
        [ ! -L "$UNIT" ] && [ "$(sed -n '2p' "$UNIT")" = '# Inkline managed USB keyboard service' ] || fail "Unrelated service: $UNIT"
    fi
    for name in inkline-usb inkline-type; do
        if [ -e "$BIN/$name" ] || [ -L "$BIN/$name" ]; then
            [ -L "$BIN/$name" ] && [ "$(readlink "$BIN/$name")" = "$ROOT/current/usb/$name" ] || fail "Unrelated command: $BIN/$name"
        fi
    done
}
check
case "${1:-check}" in
    check) ;;
    install)
        mkdir -p "$BIN"
        if ! cmp -s "$ROOT/current/usb/inkline-usb-keyboard.service" "$UNIT"; then
            temporary=$(mktemp /etc/systemd/system/.inkline-usb.XXXXXX)
            trap 'rm -f -- "$temporary"' EXIT
            cp "$ROOT/current/usb/inkline-usb-keyboard.service" "$temporary"
            chmod 644 "$temporary"
            mv -f "$temporary" "$UNIT"
        fi
        for name in inkline-usb inkline-type; do
            [ -L "$BIN/$name" ] || ln -s "$ROOT/current/usb/$name" "$BIN/$name"
        done
        ;;
    remove)
        "$ROOT/current/usb/inkline-usb" network
        rm -f "$UNIT"
        for name in inkline-usb inkline-type; do
            if [ -L "$BIN/$name" ]; then rm "$BIN/$name"; fi
        done
        ;;
    *) fail 'Expected check, install or remove';;
esac
