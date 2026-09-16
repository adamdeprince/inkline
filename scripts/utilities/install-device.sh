#!/bin/sh
# Install one verified utility bundle; no stock system libraries are replaced.
set -eu
umask 077
ulimit -c 0
payload=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fail() { printf 'Inkline utilities: %s\n' "$*" >&2; exit 1; }
case "${1:-}" in ''|--check) ;; *) fail 'Usage: install-device.sh [--check]' ;; esac
test "$(id -u)" = 0 || fail 'Run as root on the tablet.'
test "$(uname -m)" = armv7l || fail 'This package requires ARMv7 reMarkable 2.'
test "$(cat /sys/devices/soc0/machine)" = 'reMarkable 2.0' || fail 'This package requires reMarkable 2.'
firmware=$(sed -n 's/^REMARKABLE_RELEASE_VERSION=//p' /usr/share/remarkable/update.conf | tr -d '"')
case "$firmware" in 3.27.*) ;; *) fail "Unsupported firmware $firmware; tested on 3.27.3.0." ;; esac
for point in /tmp /run; do
    awk -v p="$point" '$2 == p && $3 == "tmpfs" { ok=1 } END { exit !ok }' /proc/mounts || fail "$point must be RAM backed."
done
for command in sha256sum flock readlink mktemp; do
    command -v "$command" >/dev/null || fail "Missing command: $command"
done
name=$(cat "$payload/package-name")
case "$name" in ''|[-.]*|*[!a-z0-9-]*) fail 'Invalid package name.' ;; esac
base=/home/root/.local/share/inkline-utilities
root="$base/$name"
bin=/home/root/.local/bin
test ! -L "$base" && test ! -L "$root" && test ! -L "$bin" || fail 'Install directories must not be symbolic links.'
exec 9>/run/inkline-utilities.lock
flock -x 9
if [ -e "$root" ]; then
    test -f "$root/.inkline-utility" && test "$(cat "$root/.inkline-utility")" = "$name" || fail "Refusing to replace an unrelated $root."
fi
cd "$payload"
sha256sum -c SHA256SUMS >/dev/null || fail 'Package checksums did not match.'
if [ -x "$payload/pre-install.sh" ]; then
    "$payload/pre-install.sh" || fail 'Package-specific preflight failed; nothing installed.'
fi
while IFS= read -r command; do
    case "$command" in ''|[-.]*|*[!a-zA-Z0-9._+-]*) fail 'Invalid command name.' ;; esac
    test -x "$payload/bin/$command" || fail "Missing command: $command"
    if [ -e "$bin/$command" ] || [ -L "$bin/$command" ]; then
        test -L "$bin/$command" && test "$(readlink "$bin/$command")" = "$root/current/bin/$command" || fail "An unrelated $bin/$command already exists."
    fi
done < commands
scratch=$(mktemp -d /tmp/inkline-utility-check.XXXXXX)
trap 'rm -rf -- "$scratch"' EXIT
trap 'exit 130' HUP INT TERM
env TMPDIR="$scratch" XDG_CACHE_HOME="$scratch" PYTHONDONTWRITEBYTECODE=1 "$payload/check.sh" || fail 'Runtime compatibility check failed; nothing installed.'
if [ "${1:-}" = --check ]; then
    printf '%s preflight passed on firmware %s. Nothing installed.\n' "$name" "$firmware"
    exit 0
fi
needed=$(du -sk "$payload" | awk '{print $1 + 10240}')
available=$(df -Pk /home/root | awk 'END {print $4}')
test "$available" -ge "$needed" || fail 'Insufficient space in /home.'
mkdir -p "$root/releases" "$bin"
printf '%s\n' "$name" > "$root/.inkline-utility"
release=$(sha256sum SHA256SUMS | cut -c1-16)
stage=$(mktemp -d "$root/.stage.XXXXXX")
trap 'rm -rf -- "$scratch" "$stage"' EXIT
cp -R "$payload/." "$stage/"
(cd "$stage" && sha256sum -c SHA256SUMS >/dev/null) || fail 'Installed file verification failed.'
if [ -d "$root/releases/$release" ]; then
    (cd "$root/releases/$release" && sha256sum -c "$payload/SHA256SUMS" >/dev/null) || fail 'The existing release was modified.'
    rm -rf -- "$stage"
else
    mv "$stage" "$root/releases/$release"
fi
ln -s "releases/$release" "$root/.current.$$"
mv -Tf "$root/.current.$$" "$root/current"
while IFS= read -r command; do
    ln -s "$root/current/bin/$command" "$bin/.inkline-$command.$$"
    mv -Tf "$bin/.inkline-$command.$$" "$bin/$command"
done < commands
if [ -x "$root/current/post-install.sh" ]; then
    "$root/current/post-install.sh" || fail "Files installed, but service setup failed. Retry $root/current/post-install.sh."
fi
printf '%s installed. Commands are available inside Inkline.\n' "$name"
printf 'Remove: %s/current/uninstall-device.sh\n' "$root"
# Emacs carries a large Lisp tree. Remove obsolete releases only after the new
# release and post-install steps succeeded; a running old Emacs keeps its files.
if [ "$name" = emacs ]; then
    flock -u 9
    "$root/current/prune-releases.sh" "$name" || printf 'Old-release cleanup failed; retry ~/inkline cleanup emacs after closing Emacs.\n'
fi
