#!/bin/sh
set -eu
ulimit -c 0
umask 077
# Pause the full cgroup so child programs cannot keep painting behind the app.
# ExecStopPost always resumes it, including after a crash or forced termination.
systemctl kill --kill-whom=all --signal=STOP inkline.service 2>/dev/null || :
systemctl stop xochitl.service
export HOME=/home/root SHELL=/bin/bash
export PATH=/home/root/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin
export TMPDIR=/run/inkline-shortcut-app XDG_RUNTIME_DIR=/run/inkline-shortcut-app
export XDG_CACHE_HOME="$TMPDIR/cache" QML_DISABLE_DISK_CACHE=1
mkdir -p "$XDG_CACHE_HOME"
export QSG_RENDER_LOOP=basic QT_QUICK_BACKEND=epaper QT_QPA_PLATFORM=epaper
export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS=rotate=180:invertx
cd "$HOME"
exec /usr/bin/systemd-inhibit --what=idle:handle-power-key:handle-suspend-key --mode=block --why='Inkline e-paper app' \
    /bin/bash -ic 'exec "$@"' inkline-shortcut "$@"
