#!/bin/sh
# Exercise the real helper with fake services; NEVER suspend the test machine.
set -eu
helper=${1:?Pass scripts/power-control.sh}
stage=$(mktemp -d /tmp/inkline-power-test.XXXXXX)
trap 'rm -rf -- "$stage"' EXIT
mkdir "$stage/bin" "$stage/bundle"
export INKLINE_POWER_TEST="$stage"
export PATH="$stage/bin:$PATH"
cat > "$stage/bin/systemctl" <<'SH'
#!/bin/sh
if [ "$1" = is-active ]; then
    test -f "$INKLINE_POWER_TEST/$3"
else
    printf '%s\n' "$*" >> "$INKLINE_POWER_TEST/actions"
fi
SH
cat > "$stage/bundle/inkline" <<'SH'
#!/bin/sh
printf 'inkline %s\n' "$*" >> "$INKLINE_POWER_TEST/actions"
SH
chmod 755 "$stage/bin/systemctl" "$stage/bundle/inkline"
sed -e "s|^bundle=.*|bundle=$stage/bundle|" \
    -e "s|/run/inkline-power.lock|$stage/power.lock|" "$helper" > "$stage/helper.sh"
if sh "$stage/helper.sh" invalid; then echo 'Invalid action accepted' >&2; exit 1; fi
sh "$stage/helper.sh" sleep
test ! -e "$stage/actions" # No terminal: leave notebooks and logind alone.
: > "$stage/inkline.service"
: > "$stage/xochitl.service"
sh "$stage/helper.sh" sleep
test ! -e "$stage/actions" # During service handoff, notebooks retain the key.
rm "$stage/xochitl.service"
sh "$stage/helper.sh" sleep
test "$(cat "$stage/actions")" = '--no-ask-password --check-inhibitors=yes suspend'
rm "$stage/actions"
exec 8>"$stage/power.lock"
flock -x 8
sh "$stage/helper.sh" sleep
test ! -e "$stage/actions" # A concurrent request must not enqueue another sleep.
flock -u 8
: > "$stage/inkline-shortcut-app.service"
sh "$stage/helper.sh" resume
test ! -e "$stage/actions" # Do not repaint over a native shortcut application.
rm "$stage/inkline-shortcut-app.service"
sh "$stage/helper.sh" resume
test "$(cat "$stage/actions")" = 'inkline --redraw'
echo 'Power helper: foreground ownership, inhibitor checks, locking and wake redraw passed.'
