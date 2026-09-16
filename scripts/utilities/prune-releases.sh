#!/bin/sh
# Remove only obsolete, managed utility releases. Never remove current or in-use files.
set -eu
umask 077
name=${1:?Usage: prune-releases.sh UTILITY [--check]}
case "$name" in ''|[-.]*|*[!a-z0-9-]*) echo 'Invalid utility name.' >&2; exit 2 ;; esac
case "${2:-}" in '') preview=false ;; --check) preview=true ;; *) exit 2 ;; esac
test "$#" -le 2
# The override lets the device integration test use a disposable tmpfs fixture.
base=${INKLINE_UTILITIES_ROOT:-/home/root/.local/share/inkline-utilities}
root="$base/$name"
test -d /proc/1 && test ! -L "$base" && test ! -L "$root" && test ! -L "$root/releases"
test -f "$root/.inkline-utility" && test "$(cat "$root/.inkline-utility")" = "$name"
exec 9>/run/inkline-utilities.lock
flock -x 9
current=$(readlink -f "$root/current")
case "$current" in "$root/releases/"*) ;; *) echo 'Unrecognized current release.' >&2; exit 1 ;; esac
test -d "$current" && test -f "$current/SHA256SUMS" && test "$(cat "$current/package-name")" = "$name"
for old in "$root"/releases/*; do
    test -d "$old" && test ! -L "$old" || continue
    test "$old" != "$current" || continue
    id=${old##*/}
    case "$id" in *[!0-9a-f]*|'') continue ;; esac
    test "${#id}" -eq 16 || continue
    test -f "$old/SHA256SUMS" && test -f "$old/package-name" || continue
    test "$(cat "$old/package-name")" = "$name" || continue
    busy=false
    for proc in /proc/[0-9]*; do
        test -d "$proc" || continue
        for link in "$proc/exe" "$proc/cwd" "$proc"/fd/*; do
            target=$(readlink "$link" 2>/dev/null || :)
            case "$target" in "$old"|"$old/"*) busy=true; break ;; esac
        done
        "$busy" && break
        if grep -qF "$old/" "$proc/maps" "$proc/cmdline" 2>/dev/null; then busy=true; break; fi
    done
    if "$busy"; then
        printf 'Keeping in-use %s release %s; close its programs and run cleanup again.\n' "$name" "$id"
    elif "$preview"; then
        du -sk "$old"
    else
        size=$(du -sk "$old" | awk '{print $1}')
        rm -rf -- "$old"
        printf 'Removed old %s release %s (%s KiB).\n' "$name" "$id" "$size"
    fi
done
