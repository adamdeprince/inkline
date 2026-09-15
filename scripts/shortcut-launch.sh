#!/bin/sh
# Input handling stays in the daemon; potentially slow service operations run here.
set -eu
umask 077
bundle=/home/root/.local/share/inkline/current
action=${1:-show}
if [ "$#" -gt 0 ]; then shift; fi
case "$action" in show|terminal|epaper|recover) ;; *) exit 2 ;; esac
exec 8>/run/inkline-shortcut-launch.lock
# A stuck ordinary launcher must not block the emergency chord.
if [ "$action" = recover ]; then
    systemctl kill --kill-whom=all --signal=KILL inkline-shortcut-app.service 2>/dev/null || :
    systemctl kill --kill-whom=all --signal=CONT inkline.service 2>/dev/null || :
    systemctl kill --kill-whom=all --signal=KILL inkline.service 2>/dev/null || :
    systemctl stop inkline-shortcut-app.service inkline.service 2>/dev/null || :
    exec /home/root/inkline start
fi
# The stock BusyBox flock has no -w timeout option.
attempts=0
until flock -n -x 8; do
    attempts=$((attempts + 1))
    test "$attempts" -lt 80 || exit 1
    sleep 0.1
done
systemctl stop inkline-shortcut-app.service 2>/dev/null || :
case "$action" in
    show|terminal)
        /home/root/inkline start
        if [ "$action" = terminal ]; then
            test "$#" -gt 0
            attempts=0
            while [ ! -S /run/inkline/control ] && [ "$attempts" -lt 30 ]; do
                sleep 0.1; attempts=$((attempts + 1))
            done
            # Match a normal Inkline shell: read ~/.bashrc, then exec the
            # literal argument vector. User text is never parsed as shell code.
            exec "$bundle/inkline" --open-program /bin/bash -ic 'exec "$@"' inkline-shortcut "$@"
        fi
        ;;
    epaper)
        test "$#" -gt 0
        exec systemd-run --quiet --collect --service-type=exec --expand-environment=no \
            --unit=inkline-shortcut-app --description='Inkline e-paper shortcut' \
            --property=KillMode=control-group --property=TimeoutStopSec=2s \
            --property=StandardOutput=null --property=StandardError=null --property=LimitCORE=0 \
            --property=RuntimeDirectory=inkline-shortcut-app --property=RuntimeDirectoryMode=0700 \
            --property="ExecStopPost=$bundle/shortcut-restore.sh" \
            -- "$bundle/shortcut-epaper.sh" "$@"
        ;;
esac
