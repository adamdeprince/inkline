#!/bin/sh
set -eu
umask 077
payload=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
name=$(cat "$payload/package-name")
case "$name" in ''|[-.]*|*[!a-z0-9-]*) echo 'Invalid package name.' >&2; exit 1 ;; esac
root="/home/root/.local/share/inkline-utilities/$name"
test "$(id -u)" = 0
test ! -L "$root"
test "$(cat "$root/.inkline-utility")" = "$name"
if [ "$name" = texlive ] && [ -f "$root/state/prefix" ]; then
    echo 'Run texlive-remove first to remove the TeX collection, then repeat this command.' >&2
    exit 1
fi
exec 9>/run/inkline-utilities.lock
flock -x 9
while IFS= read -r command; do
    case "$command" in ''|[-.]*|*[!a-zA-Z0-9._+-]*) exit 1 ;; esac
    link="/home/root/.local/bin/$command"
    if [ -L "$link" ] && [ "$(readlink "$link")" = "$root/current/bin/$command" ]; then
        rm "$link"
    fi
done < "$payload/commands"
rm -rf -- "$root"
printf '%s removed. Documents and other utilities were left in place.\n' "$name"
