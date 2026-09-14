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
