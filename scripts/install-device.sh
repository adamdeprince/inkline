#!/bin/sh
# Run from a verified, unpacked Inkline bundle on the tablet.
set -eu
ulimit -c 0
umask 077
payload=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=/home/root/.local/share/inkline
check_only=false
case "${1:-}" in --check) check_only=true ;; '') ;; *) echo 'Usage: install-device.sh [--check]' >&2; exit 2 ;; esac
fail() { printf 'Inkline: %s\n' "$*" >&2; exit 1; }
test "$(id -u)" = 0 || fail 'Install as root on the tablet.'
test "$(uname -m)" = armv7l || fail 'This bundle requires reMarkable 2 (ARMv7).'
test "$(cat /sys/devices/soc0/machine)" = 'reMarkable 2.0' || fail 'This bundle supports reMarkable 2 only.'
firmware=$(sed -n 's/^REMARKABLE_RELEASE_VERSION=//p' /usr/share/remarkable/update.conf | tr -d '"')
case "$firmware" in 3.27.*) ;; *) fail "Firmware $firmware is outside the supported 3.27 line; tested on 3.27.3.0." ;; esac
for mountpoint in /tmp /dev/shm /run; do
    awk -v p="$mountpoint" '$2 == p && $3 == "tmpfs" { ok=1 } END { exit !ok }' /proc/mounts || fail "$mountpoint must be RAM backed."
done
awk '/^SwapTotal:/ { ok=($2 == 0) } END { exit !ok }' /proc/meminfo || fail 'Disable swap before using this RAM-only graphics configuration.'
for required in /usr/lib/plugins/platforms/libepaper.so /usr/lib/plugins/scenegraph/libqsgepaper.so \
    /usr/lib/plugins/platforms/libqoffscreen.so /usr/share/fonts/ttf/noto/NotoMono-Regular.ttf; do
    test -r "$required" || fail "Missing firmware dependency: $required"
done
for command in systemctl systemd-inhibit sha256sum mktemp tar flock; do command -v "$command" >/dev/null || fail "Missing command: $command"; done
test -x /bin/bash || fail 'Bash is required to load ~/.bashrc for interactive shells.'
test -w /etc/systemd/system || fail '/etc/systemd/system must be writable to install the keyboard launcher.'
exec 9>/run/inkline-manage.lock
flock -x 9
test ! -L "$root" || fail 'The install directory must not be a symlink.'
if [ -e "$root" ]; then
    test -f "$root/.inkline-managed" && test "$(cat "$root/.inkline-managed")" = inkline-v1 || fail "Refusing to overwrite an unrecognized directory: $root"
fi
if [ -e /home/root/inkline ] || [ -L /home/root/inkline ]; then
    test ! -L /home/root/inkline && test "$(sed -n '2p' /home/root/inkline)" = '# Inkline managed launcher' || fail 'An unrelated /home/root/inkline already exists.'
fi
cd "$payload"
sha256sum -c SHA256SUMS >/dev/null || fail 'Bundle checksum verification failed.'
sh "$payload/hotkey-service.sh" check
scratch=$(mktemp -d /tmp/inkline-check.XXXXXX)
trap 'rm -rf -- "$scratch"' EXIT
trap 'exit 130' HUP INT TERM
mkdir "$scratch/cache"
env TMPDIR="$scratch" XDG_RUNTIME_DIR="$scratch" XDG_CACHE_HOME="$scratch/cache" \
    QML_DISABLE_DISK_CACHE=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QSG_RENDER_LOOP=basic \
    "$payload/inkline" --check || fail 'Application compatibility check failed; nothing was installed.'
if "$check_only"; then
    printf 'Inkline prerequisites passed on firmware %s. No installation changes made.\n' "$firmware"
    exit 0
fi
required_kb=$(du -sk "$payload" | awk '{print 2 * $1 + 10240}')
available_kb=$(df -Pk /home/root | awk 'END {print $4}')
test "$available_kb" -ge "$required_kb" || fail 'Insufficient space in /home for installation and rollback.'
# Existing processes keep using their immutable release. The new current link
# takes effect on the next launch, preserving open shells during an update.
running=false
if systemctl is-active --quiet inkline.service; then running=true; fi
mkdir -p "$root/releases"
printf 'inkline-v1\n' > "$root/.inkline-managed"
release=$(sha256sum SHA256SUMS | cut -c1-16)
stage=$(mktemp -d "$root/.stage.XXXXXX")
trap 'rm -rf -- "$scratch" "$stage"' EXIT
cp -R "$payload/." "$stage/"
(cd "$stage" && sha256sum -c SHA256SUMS >/dev/null) || fail 'Installed file verification failed.'
if [ -d "$root/releases/$release" ]; then
    (cd "$root/releases/$release" && sha256sum -c "$payload/SHA256SUMS" >/dev/null) || fail 'Existing release has been modified.'
    rm -rf -- "$stage"
else
    mv "$stage" "$root/releases/$release"
fi
ln -s "releases/$release" "$root/.current.$$"
mv -Tf "$root/.current.$$" "$root/current"
cp "$root/current/inkline-launcher" /home/root/.inkline-launcher.new
chmod 700 /home/root/.inkline-launcher.new
mv -f /home/root/.inkline-launcher.new /home/root/inkline
if systemctl is-active --quiet inkline-hotkey.service; then systemctl stop inkline-hotkey.service; fi
sh "$root/current/hotkey-service.sh" install
systemctl daemon-reload
systemctl start inkline-hotkey.service
systemctl is-active --quiet inkline-hotkey.service || fail 'The keyboard launcher did not start.'
"$root/current/install-manual.sh" || printf 'Manual import skipped; the PDF is included in the installed bundle.\n'
printf 'Inkline installed on firmware %s.\nLaunch on the tablet: Opt+RightAlt+T\nSSH fallback: ~/inkline start\nRemove: ~/inkline uninstall\n' "$firmware"
if "$running"; then printf 'Your current terminals are still running. Quit and reopen Inkline to use this update.\n'; fi
