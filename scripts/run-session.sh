#!/bin/sh
set -eu
ulimit -c 0
umask 077
# System services may omit HOME even though they run as root.
export HOME=/home/root
# Bash invoked as sh skips ~/.bashrc. Use Bash's interactive startup path so
# user preferences (including Python and pip cache settings) stay in that file.
export SHELL=/bin/bash
# systemd owns and removes this RAM directory even after SIGKILL.
runtime=/run/inkline
test -d "$runtime"
export TMPDIR="$runtime" XDG_RUNTIME_DIR="$runtime" XDG_CACHE_HOME="$runtime/cache"
mkdir -p "$XDG_CACHE_HOME"
export QML_DISABLE_DISK_CACHE=1 QSG_RENDER_LOOP=basic QT_QUICK_BACKEND=epaper QT_QPA_PLATFORM=epaper
export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS=rotate=180:invertx
export HISTFILE=/dev/null
export PATH="/home/root/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin"
cd /home/root
set -- --rotate "${INKLINE_ROTATE:-90}"
if [ "${INKLINE_FONT_SIZE:-0}" -gt 0 ]; then set -- "$@" --font-size "$INKLINE_FONT_SIZE"; fi
if [ "${INKLINE_DEMO:-0}" = 1 ]; then set -- "$@" --demo; fi
if [ "${INKLINE_QUIT_AFTER:-0}" -gt 0 ]; then set -- "$@" --quit-after "$INKLINE_QUIT_AFTER"; fi
/usr/bin/systemd-inhibit --what=idle:handle-power-key:handle-suspend-key --mode=block --why='Inkline terminal session' \
    /home/root/.local/share/inkline/current/inkline "$@"
