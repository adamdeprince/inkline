# Power-button suspend

While Inkline is active, the physical power button suspends the tablet through
systemd. The next press wakes it. Shells, editor buffers, images and settings
stay in RAM. Sleep does not stop Inkline or save preferences. Network sessions
may reconnect or time out according to the remote program.

The target tablet's logind `HandlePowerKey` setting is `ignore`; xochitl
normally handles the button itself. Its SNVS power-key input is a separate
evdev device with no keyboard modifier keys. The global launcher discovers
devices with `KEY_POWER` or `KEY_SLEEP` as well as shortcut-capable keyboards,
without assuming a fixed event number or grabbing input exclusively.

`PowerKey` accepts a release only after an observed press, ignores autorepeat,
and resets after dropped events. `ResumeGuard` compares `CLOCK_BOOTTIME` with
`CLOCK_MONOTONIC`, which excludes suspended time. A resume resets pending
presses and blocks power actions for two seconds before processing queued
events. This prevents the wake gesture from putting the tablet straight back
to sleep, including a wake button held through the debounce period.

`power-control.sh` checks that Inkline is active and xochitl is inactive,
serializes requests with a `/run` lock and calls `systemctl suspend`. On wake,
the daemon requests a full redraw, except while a native shortcut app still
owns the display. No persistent runtime state or logs are created by the helper.
Existing system sleep hooks and other programs' inhibitors remain in effect.

The terminal and native app wrappers inhibit `idle`, `handle-power-key` and
`handle-suspend-key`; they no longer hold a `sleep` inhibitor. This retains
control of the physical key while permitting explicit suspend. An installed
update preserves open terminals, so the old wrapper's sleep inhibitor persists
until the user quits and reopens Inkline. Do not terminate user sessions merely
to activate an update.

`power_key_tests` exercises key release, repeat, dropped-event resets, request
debounce, resumed clock offsets, held wake keys and subsequent deliberate
presses. The device test runner executes it from tmpfs without actually
suspending the tablet. A physical sleep/wake check is separate: after saving
work and opening the new version, press power, wake it, and verify the same
shell/editor and a repainted screen. Do not claim this hardware check passed
solely because the state-machine tests passed.
