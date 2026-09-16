#!/bin/sh
set -eu
root=/home/root/.local/share/inkline
test "$(id -u)" = 0
exec 9>/run/inkline-manage.lock
flock -x 9
test ! -L "$root"
test "$(cat "$root/.inkline-managed")" = inkline-v1
sh "$root/current/hotkey-service.sh" check
sh "$root/current/usb/install.sh" check
sh "$root/current/usb/install.sh" remove
systemctl stop inkline-hotkey.service 2>/dev/null || :
systemctl stop inkline-shortcut-app.service 2>/dev/null || :
systemctl kill --kill-whom=all --signal=CONT inkline.service 2>/dev/null || :
sh "$root/current/hotkey-service.sh" remove
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
