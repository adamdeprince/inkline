#!/bin/sh
set -eu
root=/home/root/.local/share/inkline
test "$(id -u)" = 0
exec 9>/run/inkline-manage.lock
flock -x 9
test ! -L "$root"
test "$(cat "$root/.inkline-managed")" = inkline-v1
systemctl stop inkline-hotkey.service 2>/dev/null || :
if [ -L /etc/systemd/system/inkline-hotkey.service ] &&
    [ "$(readlink /etc/systemd/system/inkline-hotkey.service)" = "$root/current/inkline-hotkey.service" ]; then
    systemctl disable inkline-hotkey.service
fi
if systemctl is-active --quiet inkline.service; then systemctl stop inkline.service; fi
systemctl start xochitl.service
systemctl is-active --quiet xochitl.service
if [ -L /run/systemd/system/inkline.service ]; then
    case "$(readlink /run/systemd/system/inkline.service)" in
        "$root/"*) rm /run/systemd/system/inkline.service ;;
    esac
fi
rm -f /run/inkline-launch.env
systemctl daemon-reload
if [ -f /home/root/inkline ] && [ ! -L /home/root/inkline ] &&
    [ "$(sed -n '2p' /home/root/inkline)" = '# Inkline managed launcher' ]; then
    rm /home/root/inkline
fi
rm -rf -- "$root"
echo 'Inkline removed. The notebook interface is running.'
