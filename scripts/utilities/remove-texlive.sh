#!/bin/sh
set -eu
state=${INKLINE_TEXLIVE_STATE_DIR:-/home/root/.local/share/inkline-utilities/texlive/state}
test "$(id -u)" = 0
test -f "$state/prefix" || { echo 'No completed Inkline TeX Live installation was recorded.' >&2; exit 1; }
exec 9>"$state/install.lock"
flock -x 9
prefix=$(cat "$state/prefix")
case "$prefix" in /) exit 1 ;; /*) ;; *) exit 1 ;; esac
test -d "$prefix" && test ! -L "$prefix" && test -f "$prefix/.inkline-texlive"
test "$(cat "$prefix/.inkline-texlive")" = 'Inkline TeX Live 2026'
rm -rf -- "$prefix"
rm "$state/prefix"
printf 'TeX Live removed. Documents and /home/root/texmf were left in place.\n'
