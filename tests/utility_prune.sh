#!/bin/sh
# Device-only cleanup regression: every fixture lives in RAM.
set -eu
script=${1:?Pass prune-releases.sh}
stage=$(mktemp -d /tmp/inkline-prune-test.XXXXXX)
child=
trap 'if [ -n "$child" ]; then kill "$child" 2>/dev/null || :; fi; rm -rf -- "$stage"' EXIT
export INKLINE_UTILITIES_ROOT="$stage/utilities"
root="$INKLINE_UTILITIES_ROOT/emacs"
mkdir -p "$root/releases"
echo emacs > "$root/.inkline-utility"
for id in 1111111111111111 2222222222222222 3333333333333333; do
    mkdir "$root/releases/$id"
    echo emacs > "$root/releases/$id/package-name"
    : > "$root/releases/$id/SHA256SUMS"
done
ln -s releases/1111111111111111 "$root/current"
mkdir "$root/releases/unrecognized"
ln -s "$stage" "$root/releases/4444444444444444"
cp /bin/sleep "$root/releases/3333333333333333/sleep"
"$root/releases/3333333333333333/sleep" 120 & child=$!
sh "$script" emacs --check
test -d "$root/releases/2222222222222222"
sh "$script" emacs
test ! -e "$root/releases/2222222222222222"
test -d "$root/releases/1111111111111111"
test -d "$root/releases/3333333333333333"
test -d "$root/releases/unrecognized" && test -L "$root/releases/4444444444444444"
kill "$child"; wait "$child" 2>/dev/null || :; child=
sh "$script" emacs
test ! -e "$root/releases/3333333333333333"
test -d "$root/releases/1111111111111111"
echo 'Cleanup preview, current protection, in-use protection and unknown-path protection passed.'
