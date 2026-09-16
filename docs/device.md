# Inkline device validation — 2026-09-13

The read-only probe succeeded over my SSH alias `remarkable`. The SSH
host key changed after the upgrade; I approved the new fingerprint before
the connection was made. The full local report is `device-info.txt` and is ignored
by version control.

| Property | Observed value |
| --- | --- |
| Model | reMarkable 2.0 |
| Firmware | 3.27.3.0 |
| OS release | Codex Linux 5.7.126, scarthgap |
| Kernel | 5.4.70-v1.6.3-rm11x, armv7l |
| RAM reported by kernel | 1,027,664 KiB, about 1 GiB |
| Swap | 0 KiB |
| `/tmp`, `/dev/shm` | tmpfs |
| Qt Core | 6.8.2 |
| libpng runtime | 1.6.41 |
| GNU C++ runtime | libstdc++.so.6.0.32 |
| E-paper plugins | `/usr/lib/plugins/platforms/libepaper.so`, `/usr/lib/plugins/scenegraph/libqsgepaper.so` |
| Main tablet UI | xochitl active |
| Type Folio | `rM_Keyboard`, bus 0019, vendor 2edd, product 0001 |
| External USB keyboard | Not present in this probe |

The Folio happened to be `/dev/input/event3`; this is an observation, not a fixed
device path for the application. Inkline uses Qt's device discovery so input
event numbering is not hardcoded. I confirmed on-device
launch and typing with Type Folio. External USB hotplug remains a manual
acceptance check.

The framebuffer reports 260 × 23936 at 32 bpp. These are not ordinary screen-sized
bitmap dimensions. Use the installed Qt e-paper integration as described in the
[manufacturer's guide](https://developer.remarkable.com/documentation/qt_epaper).

The SDK is 5.7.119 and the tablet OS is 5.7.126. Both use Qt 6.8.2; the SDK's PNG
headers report 1.6.42, while the tablet library is 1.6.41. All five ARM suites
passed using those stock libraries; no device libraries have been replaced.

The device runner uploaded all five original suites over USB SSH into a
fresh tmpfs directory, ran them, and removed the test files. Core graphics,
sixel decoding, stream framing, PTY behavior, and Qt renderer/keyboard encoding
all passed. The graphics suite measured zero process `write_bytes` throughout
its image tests, including 1,000 replacements.

Two startup defects found by ARM testing are fixed reproducibly: the allocator
bridge uses logarithmic alignment, and the Linux static build must link libc
to avoid Wuffs' weak `calloc`/`free` placeholders overriding glibc.

The reusable installer passed its read-only preflight and installed into
`/home/root/.local/share/inkline` with a `/home/root/inkline` launcher. A ten-second
demo launched using the stock epaper platform/backend, exited with status 0,
and automatically restored active `xochitl`. The uninstall procedure then
removed the application directory, launcher and temporary service link and
verified that `xochitl` was active.

The application's offscreen render and graphics pixel checks pass. I
confirmed that the terminal works on the physical tablet with Type Folio.
Additional keyboard layouts, external USB keyboard hotplug, sustained graphics
memory use, and my Goblin Mosh workflow still need acceptance testing.

The on-device launcher is installed and enabled as `inkline-hotkey.service`.
It uses about 1.3 MiB RSS and monitors the connected Folio without grabbing it.
A temporary Linux uinput keyboard successfully triggered Ctrl+Alt+T, opening
Inkline and stopping xochitl. I confirmed that pressing Ctrl+Alt+T
on the physical Folio opens Inkline and that typing works. The service is enabled
for boot, though a reboot test has not been performed.

## Utility installation

All seven program bundles are installed: Goblin Mosh, Mosh, Emacs, GoblinView,
Goblin Purrfect, Git and Python 3.15.0rc2. Their combined measured footprint is
499.2 MiB after the Python bytecode policy update. Compact TeX Live is also
installed; Purrfect created a nonempty PDF using LuaLaTeX. The TeX tree takes
187.8 MiB, with 80.0 MiB for its private installer/runtime. The eight current
utility releases total about 767 MiB; retained older releases take extra space.
The [public catalog](https://inkline.goblinreactor.com/utilities.html) records
individual package sizes and links separate installation instructions.

The revised Python bytecode policy passes the complete tablet startup, import,
virtualenv/ensurepip and local wheel pip-install check with no `.pyc` files or
`__pycache__` directories created. It is installed, along with the corrected TeX
bootstrap package. LuaLaTeX and tlmgr still use the existing compact TeX tree.
Both utility updates preserved the running Inkline process (PID 2795).

## 0.2.0 keyboard and session changes

All seven host and ARM suites pass, including actual independent shell PTYs, background
output, six slot navigation, saved settings and quit confirmation. The new
shortcut and session suites also pass with address/undefined behavior sanitizers.
The ARM build passes. The Settings and quit dialogs have been inspected in host
snapshots, and the startup guide and 64-pixel Goblin logo render correctly in an
offscreen snapshot produced on the tablet.

The new shortcuts, Caps Lock LED handling, settings touch targets and USB hotplug
still need checks on the physical tablet. The update installer preserves open
terminals; the new version takes effect after quitting and reopening Inkline.

## 0.3.0 input, clipboard and display changes — September 14

All nine host and ARM suites pass. The new checks include OSC 52 writes and
queries, clipboard limits, bracketed and kitty paste modes, Unicode selection
and reflow, a finger selection copied into another terminal, all five input
methods, rotated pinch events, and saved font size without replacing PTYs.
The affected host suites also pass with address/undefined-behavior sanitizers.

Settings and the Chinese candidate strip have been inspected in snapshots
rendered on the tablet. A bundled Noto CJK font supplies glyphs missing from the
stock font set. Physical finger/pen behavior and the new Folio/USB combinations
remain acceptance checks.

GoblinView `0.1.0+20260914.rm2.2` is installed and takes 13,012 KiB (12.7 MiB).
Its six upstream ARM suites and detached client/server smoke test pass. Real
PTY tests verify explicit `-m`, automatic monochrome inside Inkline, and normal
colour outside Inkline. Installation preserved the active Inkline process.

## 0.3.2 literal punctuation and finer pinch sizing — September 14

All nine host and ARM suites pass. New regressions cover both Alt keys passing
punctuation, literal `c^2` with input method Off, preserved Unicode, explicit
US-International composition, single-pixel pinch changes at reduced sensitivity,
cancellation, both font limits, and preservation of the six terminals.

A separate offscreen probe exercised the stock e-paper plugin's US keyboard
mapper with synthetic events, without reading or injecting input into the active
terminal. Right Alt plus the key immediately left of Backspace produces `=`;
Shift+6 produces a dead-circumflex event containing U+0302. Inkline now converts
that standalone combining accent to `^` before its selected input method runs.
Physical testing remains for pinch feel, additional layouts, and USB hotplug.

## 0.3.3 separate Opt function keys — September 14

The stock US e-paper keymap reports the Folio's separate Opt key as Qt Meta,
using evdev 107 / native scan 115. Its right Alt/Opt key reports AltGr, and
right Alt/Opt + 0 reports a printable `+` with no remaining modifier. An
offscreen probe verified those events without reading or injecting input into
the active terminal. Function shortcuts now use Opt/Meta, freeing the right
Alt/Opt number row for its printed symbols.

All nine host and ARM suites pass, including a real shell receiving literal
plus and F10 from the captured firmware event shapes. Tests retain Ctrl/Shift/Alt
function modifiers, key repeat/release and session routing, numeric keypads,
and ordinary Alt input. Physical Folio and USB layout checks remain.

## Incremental display and waveform controls — September 15

Firmware 3.27.3.0's stock `libqsgepaper.so` exposes an
`EPScreenModeItem::Mode` enum with Pen, Mono, Animation, UI, Content, and Sleep.
An ARM probe loaded the installed plugin from `/tmp`, constructed its screen-mode
item, and successfully selected Animation, UI, and Content. Inkline resolves
that firmware interface at runtime and falls back to Qt's normal UI mode if it
is unavailable on a later firmware build.

The renderer now uses libghostty's global and per-row dirty state. A retained
1840 × 1280 grayscale surface on the tablet averaged 0.374 ms for a dirty text
row versus 554.340 ms for a dense full redraw. The previous 80 ms application
timer has also been replaced by profile-specific adaptive batching. All nine ARM
suites pass, including checks that an unchanged frame performs no drawing and a
normal text update covers at most two terminal rows. Physical waveform response,
ghosting, and battery behavior still need interactive observation.

## 0.3.6 Unicode, shortcuts, and recovery — September 15

Eleven host suites and ten ARM suites pass. Coverage includes the Unicode
keyboard, nine terminal slots and actual open counts, native and synthesized
pen mouse events at three rotations, Shift selection, graphics clearing, and
control-socket requests. The installed build passes its startup preflight on
firmware 3.27.3.0.

The explicit `tests/shortcut_services.sh` device test creates a temporary
kernel keyboard and test programs in `/tmp`. It verified registration and
deregistration, literal arguments containing shell syntax, Bash cache
preferences, and the permanent Ctrl+Alt+T binding. A deliberately hung native
app was stopped with Ctrl+Alt+T while existing PTYs survived. Holding
Ctrl+Alt+Backspace for two seconds killed the test programs and started a fresh
terminal. A native app exiting with an error also restored Inkline. Cleanup
returned the device to notebooks and removed the temporary binding.

Device testing found that stock BusyBox `flock` lacks `-w`. The shortcut
launcher now uses bounded nonblocking attempts. The service test waits through
the full deactivating state before checking that a hung app is gone.

The original eight-page English PDF was bundled. Automatic My files import was unavailable
with this tablet's current USB web interface setting; the installer left the
PDF available and printed retry instructions. Actual document import remains
an acceptance check after enabling that interface. New physical pen gestures,
Folio combinations and USB hotplug remain separate acceptance checks.

## GNU Emacs 31.1 in terminal mode — September 15

GNU Emacs 31.1 replaces the earlier Debian Emacs 28 bundle. The Linux build
uses the matching ARM SDK and excludes graphical backends and native
compilation. The `emacs` wrapper selects terminal mode. Org, TRAMP, TeX mode,
Unicode editing, JSON, TLS, SQLite and tree-sitter pass checks on the tablet.
An actual PTY session edited and saved Unicode text and exited with C-x C-c.

The existing personal Emacs configuration was preserved and loaded successfully.
The updated ghostty-tex integration handles Emacs without native compilation;
its LaTeX, BibTeX, PDF/DVI and configuration-preservation checks pass with build
files in RAM. AUCTeX 12.2 still emits upstream deprecation warnings.
The installed Emacs release occupies 138.1 MiB, versus about 185.4 MiB for the
old bundle. Older retained releases use additional storage.

## Suspend, saved settings and multilingual manual — September 15

The ARM build passes eleven test executables, including power release/repeat,
dropped-event recovery, resumed-clock detection and wake-key suppression.
Separate shell tests use a mocked `systemctl` in tmpfs to verify foreground
ownership, explicit inhibitor checking, concurrent-request locking and redraw
after wake. They never suspend the device. Physical sleep/wake still needs a
check after quitting and reopening the installed version; the existing session
retains its old `sleep:idle` inhibitor until then.

Crisp is the new default. All settings remain in RAM and persist together on
normal exit, including SIGTERM/SIGINT through Qt's event loop. The view tests
cover deferred writes, saved contrast/update settings and unchanged sessions.
The old-release cleanup fixture verifies that current, in-use and unrecognized
paths survive and that obsolete managed releases are removed.

The manual is now one 74-page LuaLaTeX PDF: a cover, a linked nine-language
index, and eight pages per language. English, Japanese, Simplified Chinese,
Traditional Chinese, French, Spanish, German, Italian and Portuguese are included.
The build rejects missing glyphs, unresolved references and overfull boxes.
Every page passed text-bound checks; representative Latin and CJK pages were
visually reviewed. All fonts are embedded with Unicode mappings. The installer
still preserves live terminal sessions and uses the supported USB document
importer when available; it does not write notebook metadata directly.

## 0.3.7 BusyBox clear — September 16

The tablet was running 0.3.6 when its startup Goblin survived `clear`. Stock
BusyBox emits `ESC [ H ESC [ J`: cursor home followed by erase-below (ED0).
The previous graphics cleanup handled ED2 and reset, so text disappeared while
the Kitty placement remained in memory.

A pinned-source patch now removes visible Kitty placements and unused image
payloads when an unprotected ED0 covers the entire screen. It preserves the
original text-erasure behavior without invoking ED2's scrollback heuristic.
The sixel renderer handles the same full-screen clear, including explicit zero
parameters. Normal partial erases and selective erase sequences are unchanged.

The core, stream and renderer regression suites pass on macOS and reMarkable 2.
Tests check image storage, exact blank rendered pixels, dirty-region coverage,
byte-at-a-time input and preservation on partial, selective and history-only
erases. Device tests run offscreen in tmpfs without interrupting active sessions;
the graphics suite reports zero process storage-write bytes.

## 0.3.8 Keyboard launcher after reboot — September 16

After a reboot, systemd reported `inkline-hotkey.service` as enabled but could
not load the unit. Both its unit link and its enable link pointed into `/home`,
which is a separate filesystem. The Folio keyboard and application bundle were
present after boot; the service had never started.

The installer now places a small managed unit file directly in
`/etc/systemd/system`, with a relative enable link in `multi-user.target.wants`.
`RequiresMountsFor` orders the executable after its home filesystem is mounted.
Existing installations migrate automatically; removal handles both layouts and
rejects unrelated unit files or links. This follows systemd's requirement that
[linked unit files be accessible at manager startup](https://github.com/systemd/systemd/blob/v255/man/systemctl.xml).

Eight isolated installer tests pass on macOS and reMarkable 2, covering new installation,
reinstallation, legacy-link migration, removal, unit visibility without `/home`
and preservation of unrelated files. The device fixtures ran in tmpfs over Wi-Fi.

The 0.3.8 package installed successfully as release `8d8b7090ea43a657`.
Systemd reports the launcher enabled and active, loaded directly from `/etc`,
with both `Requires=home.mount` and `After=home.mount`. A temporary uinput
keyboard sent Ctrl+Alt+T through evdev; Inkline started and its control socket
became ready. The daemon also has the physical Folio input open. The terminal
was left running, and an interactive Ctrl+Alt+T check also opened Inkline.
Serial devices were not accessed or reconfigured.

A fresh reboot was subsequently requested and completed. Boot ID changed from
`56699e52-9beb-445e-915f-caa4976447c7` to
`aafac064-fde4-4fe1-896b-cda26201c2aa`. Without manually starting it, the launcher
was enabled and active at 10.82 seconds, after the home mount at 10.65 seconds.
Notebooks remained the default. I confirmed that the physical keyboard opened
Inkline after reboot; its control socket was ready and the launcher stayed active.


## 0.4.0 Shortcut settings and Ctrl+Opt+Alt — September 16

The shortcut editor, parser and keyboard regression suites pass on macOS and
reMarkable 2. On ARM, the production evdev state machine also passes ordinary
Ctrl+Alt non-triggering, Folio Opt and USB Super recognition, repeat suppression,
recovery at two seconds (once per hold), and dropped-event reset checks.
The actual ARM CLI registered, listed and removed an isolated binding and
rejected both registration and removal of T. Tests ran under `/tmp` over Wi-Fi;
the installed terminal and launcher remained active.

The UI test verifies no binding directory is created while editing, quoted and
empty arguments survive Save, removal requires confirmation, and the UI reads
commands and native-app modes registered through the shared CLI storage API.
The nine-language LuaLaTeX manual builds without missing glyphs, unresolved
references, or overfull boxes (79 pages). The new three-modifier physical Folio
and USB checks are still pending.


The 0.4.0 bundle installed as release `36adcca3380dfb13`. Its launcher is enabled
and the existing terminal process (PID 621) was preserved. Temporary kernel
keyboards then sent Ctrl+Opt+Alt+T twice: one USB profile using Super, and one
Folio profile using its actual identity and Opt code. Both triggered the
installed launcher's `show` helper and preserved PID 621 and the control socket.
The destructive recovery integration test was not run over the open sessions.
Quit and reopen to load the new UI; the global daemon uses the new chord already.
