#!/bin/sh
# Explicit device integration test: switches the visible UI and uses test PTYs.
# Refuses to run over an existing Inkline session. Pass the ARM hotkey_probe path.
set -eu
ulimit -c 0
umask 077
probe=${1:?Pass the ARM hotkey_probe executable}
bundle=/home/root/.local/share/inkline/current
test -x "$probe" && test -x "$bundle/shortcut-launch.sh"
if systemctl is-active --quiet inkline.service; then
    echo 'Close existing Inkline terminals before running this integration test.' >&2
    exit 1
fi
if /home/root/inkline shortcut list | grep -q 'Opt+RightAlt+j '; then
    echo 'The test needs the unused Opt+RightAlt+J binding.' >&2
    exit 1
fi
stage=$(mktemp -d /tmp/inkline-shortcut-check.XXXXXX)
registered=false
cleanup() {
    if "$registered"; then /home/root/inkline shortcut deregister j >/dev/null 2>&1 || :; fi
    systemctl stop inkline-shortcut-app.service 2>/dev/null || :
    /home/root/inkline stop >/dev/null 2>&1 || :
    rm -rf -- "$stage"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
wait_for() {
    label=$1; shift; attempts=0
    until "$@"; do
        attempts=$((attempts + 1))
        test "$attempts" -lt 120 || { echo "Timed out: $label" >&2; exit 1; }
        sleep 0.1
    done
}
main_pid() { systemctl show -p MainPID --value inkline.service; }
running() { systemctl is-active --quiet inkline.service && test -S /run/inkline/control; }
resumed() { running && test "$(awk '{print $3}' /proc/"$(main_pid)"/stat)" != T; }
fresh() { resumed && test "$(main_pid)" != "$original_pid"; }
native_gone() {
    # is-active is already false while a service is still deactivating.
    case "$(systemctl show -p ActiveState --value inkline-shortcut-app.service)" in
        inactive|failed) return 0 ;;
        *) return 1 ;;
    esac
}

if /home/root/inkline shortcut register t /bin/true; then echo 'T was replaceable' >&2; exit 1; fi
if /home/root/inkline shortcut deregister T; then echo 'T was removable' >&2; exit 1; fi
"$probe" # Opt+RightAlt+T through the kernel input path.
wait_for 'terminal launch' running
original_pid=$(main_pid)

cat > "$stage/program with spaces.sh" <<'PROGRAM'
#!/bin/sh
printf '%s\n' "$1" "${PYTHONDONTWRITEBYTECODE:-unset}" "${PIP_NO_CACHE_DIR:-unset}" > "$2"
printf '%s\n' "$$" > "$3"
while :; do sleep 1; done
PROGRAM
chmod 700 "$stage/program with spaces.sh"
literal='literal $(not-a-command); *'
/home/root/inkline shortcut register j "$stage/program with spaces.sh" "$literal" "$stage/arguments" "$stage/program.pid"
registered=true
"$probe" 36
wait_for 'program launch' test -s "$stage/program.pid"
test "$(sed -n '1p' "$stage/arguments")" = "$literal"
if [ "${INKLINE_EXPECT_PYTHON_CACHE_PREFS:-0}" = 1 ]; then
    test "$(sed -n '2p' "$stage/arguments")" = 1
    test "$(sed -n '3p' "$stage/arguments")" = 1
fi
program_pid=$(cat "$stage/program.pid")
echo 'Registered program, literal arguments and Bash environment passed.'

cat > "$stage/native.sh" <<'PROGRAM'
#!/bin/sh
trap '' TERM
printf '%s\n' "$$" > "$1"
while :; do sleep 1; done
PROGRAM
chmod 700 "$stage/native.sh"
/home/root/inkline shortcut register j --epaper "$stage/native.sh" "$stage/native.pid"
"$probe" 36
wait_for 'native app' test -s "$stage/native.pid"
test "$(awk '{print $3}' /proc/"$original_pid"/stat)" = T
native_pid=$(cat "$stage/native.pid")
"$probe" # Opt+RightAlt+T must stop the hung app without losing terminal sessions.
wait_for 'terminal resume' resumed
wait_for 'native service exit' native_gone
test "$(main_pid)" = "$original_pid"
test -d "/proc/$program_pid"
test ! -e "/proc/$native_pid"
echo 'Native app suspension and Opt+RightAlt+T session preservation passed.'

rm "$stage/native.pid"
"$probe" 36
wait_for 'second native app' test -s "$stage/native.pid"
native_pid=$(cat "$stage/native.pid")
"$probe" 14 2400 # Hold Opt+RightAlt+Backspace beyond the two-second threshold.
wait_for 'emergency restart' fresh
wait_for 'emergency native exit' native_gone
test ! -e "/proc/$program_pid"
test ! -e "/proc/$native_pid"
original_pid=$(main_pid)
echo 'Emergency chord killed test apps and restarted a fresh terminal.'

/home/root/inkline shortcut register j --epaper /bin/sh -c 'exit 37'
"$probe" 36
wait_for 'crashed app exit' native_gone
wait_for 'resume after crash' resumed
test "$(main_pid)" = "$original_pid"
/home/root/inkline shortcut deregister j
registered=false
if /home/root/inkline shortcut list | grep -q 'Opt+RightAlt+j '; then exit 1; fi
echo 'Crash recovery, deregistration and protected T passed; restoring notebooks.'
