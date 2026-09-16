#!/bin/sh
# The keyboard daemon owns the physical key; logind owns actual system sleep.
# All state stays in /run. Suspend keeps shells and editor buffers in RAM.
set -eu
umask 077
bundle=/home/root/.local/share/inkline/current
case "${1:-}" in sleep|resume) ;; *) exit 2 ;; esac
# The daemon also runs while notebooks are open. Leave their power handling
# entirely with the notebook application.
systemctl is-active --quiet inkline.service || exit 0
if systemctl is-active --quiet xochitl.service; then exit 0; fi
if [ "$1" = resume ]; then
    # A native shortcut application still owns the screen until it exits.
    if ! systemctl is-active --quiet inkline-shortcut-app.service; then
        "$bundle/inkline" --redraw >/dev/null 2>&1 || :
    fi
    exit 0
fi
exec 8>/run/inkline-power.lock
flock -n -x 8 || exit 0
if ! systemctl is-active --quiet inkline-shortcut-app.service; then
    "$bundle/inkline" --pause-usb >/dev/null 2>&1 || :
fi
if [ -f /run/inkline-usb/type.lock ]; then
    "$bundle/usb/inkline-type" --stop >/dev/null 2>&1 || :
fi
# Respect other programs' sleep inhibitors. Our session inhibits idle and
# logind's key handling, but deliberately permits explicit system suspend.
systemctl --no-ask-password --check-inhibitors=yes suspend
