# Inkline

Inkline is a terminal for **reMarkable 2**, built with **libghostty-vt** and the
stock Qt e-paper backend. It is designed for Type Folio and external USB
keyboards, with kitty graphics and an initial sixel implementation.

**Version 0.4.2 is a preview.** The main artifact is the repeatable
[installation procedure](docs/install.md), including preflight, launch, recovery
and uninstall. It targets firmware **3.27**, tested on **3.27.3.0**. Other models
and firmware lines are not supported by this installer.

## Install

Download the **[Inkline 0.4.2 preview](https://github.com/adamdeprince/inkline/releases/tag/v0.4.2)**
and follow the [installation procedure](docs/install.md). The prebuilt ARM bundle
includes the installer, launcher, uninstall script, checksums, source archives
and licenses. No compiler, SDK or third-party package manager is needed to install it.

To build your own bundle, with SSH already configured and its host key verified:

```sh
scripts/build-tablet.sh
python3 scripts/package.py
scripts/install.sh root@10.11.99.1 --check
scripts/install.sh root@10.11.99.1
```

See the [full procedure](docs/install.md#building-from-source) for build dependencies.
The package is generated at `build/dist/inkline-rm2.tar.gz`. The
[project website](https://inkline.goblinreactor.com/) also hosts the preview download.

After installation, press **Opt+RightAlt+T on the tablet’s Type Folio**
to open Inkline: hold the separate Opt key and the Alt/Opt key right of the
spacebar, then tap T. On USB keyboards, use Windows/Command (Super) for Opt.
No second computer is needed for subsequent launches. Tap
**Quit**, press **Ctrl+Shift+Q**, or exit the shell to return to notebooks.
Quit asks for confirmation and `exit` closes only the current terminal.
The launcher temporarily switches from `xochitl` to Inkline and restores it when
Inkline stops. A small keyboard shortcut service starts at boot; the terminal itself opens only
on request, and the usual notebook interface remains the default.

The bundle includes a [PDF user guide](docs/Inkline%20Manual.pdf), and can import it
into My files through an already enabled USB web interface. See
[program shortcuts and emergency recovery](docs/global-shortcuts.md).

## Optional utilities

The [utility catalog](https://inkline.goblinreactor.com/utilities.html) offers
Goblin Mosh, Mosh, Emacs, GoblinView, Goblin Purrfect, Git, Python 3.15.0rc2,
and compact TeX Live
for this tablet. Follow the separate
[utility installation instructions](https://inkline.goblinreactor.com/install.html#utilities).
Build recipes, pinned inputs, and device checks are documented in
[`scripts/utilities/README.md`](scripts/utilities/README.md).

Compact TeX Live is installed on the tablet and Purrfect PDF export passes.
The full collection is optional and too large to recommend for internal storage.

## Keyboard controls

Hold the separate **Opt** key (between Ctrl and Alt on the Folio) and press
**1 through 0** for **F1 through F10**. The shortcut uses the keyboard's Meta
modifier; ordinary USB function keys also work.

Hold the **right Alt/Option** key for the other Folio and USB keyboard shortcuts:

| Keys with right Alt/Option | Action |
| --- | --- |
| Tab | Escape |
| Up / Down | PageUp / PageDown |
| Left / Right | Previous / next terminal, across nine slots |
| Space | Open or close the Unicode keyboard (either Alt key) |
| 1–9 | Select terminal slot 1–9 |
| Backspace | Confirm quitting all terminals |
| C / V | Copy selected text / paste the RAM clipboard |

**Ctrl+Shift+B** hides or shows the bottom bar. Its fifth button opens
**Settings**, where Caps Lock can act as **Control** (the default) or normal
**Caps Lock**. Active choices have a solid fill; keyboard focus has a dashed outline.
**Settings → Command shortcuts (page 2)** assigns commands to
**Opt+RightAlt+letter**, with explicit Save and Remove buttons. T is permanently
reserved. The installed `~/inkline shortcut` tool manages the same bindings.
Ordinary Ctrl+Alt combinations remain available to Emacs.
The **E-paper updates** row offers Fast, Balanced, Crisp, Mono, and Saver profiles;
**Crisp is the default**, while explicitly saved choices are preserved.
Fast uses the firmware's animation waveform and minimal batching; Crisp uses
the content waveform for the cleanest grayscale; Mono trades image grays for
fast black-and-white text; Saver batches output longer to request fewer screen
updates. All settings stay in RAM and are saved together on normal exit.
Unchanged sessions write nothing; finger lifts, closing Settings and suspend
do not write preferences to flash.
Pinch with two fingers to change font size (6–48 px), with slower movement and
one-pixel steps for finer control. Settings also has minus and plus buttons.
The size is saved when Inkline exits. Each terminal has its own
shell and scrollback; switching to an unused slot opens a shell there.
Drag two fingers down to reveal older output, or up to return toward the prompt.
Swipe two fingers left for the next terminal, or right for the previous one.
Single-finger drags select text; the pen reports mouse events to applications that enable them. Shift+pen forces selection.

Press and release the physical power button to suspend, then press again to
wake. Open shells and editor buffers remain in RAM; network connections may
need to reconnect. Quit and reopen Inkline after updating to enable this behavior.
New terminals print a short guide with a tiny Goblin logo and the current
Caps Lock setting. The logo is displayed through inline kitty graphics in RAM.
Settings also selects **Romaji, Pinyin, Zhuyin, Wubi**, or **US-International**
input. A candidate strip supports typing or tapping a choice. A bundled CJK
font makes Chinese and Japanese text readable on the stock firmware.

On the US Type Folio, **right Alt/Option + 0** types `+`, and **right Alt/Option +
the minus key immediately left of Backspace** types `=`. Keyboard zoom
combinations have been removed. With input
method **Off**, Shift+6 types a literal `^`, so `c^2` stays `c^2`.

Drag a finger across terminal text, then use right Option+C/V. Hold Shift while
using the pen or mouse if the application has enabled terminal mouse reporting.
The 128 KiB clipboard is shared by all nine terminals, supports OSC 52, and stays
in RAM. Terminal output is read-only: Option+X copies the selection and explains
that deletion must be performed by the running editor.
See [keyboard and session instructions](docs/keyboard.md) for details.

## Current source features

- Up to nine local interactive shells with controlling PTYs, resizing, scrollback and
  alternate-screen terminal state supplied by libghostty.
- Qt keyboard events encoded through libghostty, including Ctrl/Alt combinations,
  cursor modes and negotiated kitty key events. Touch controls provide Escape,
  history scrolling, Quit and Settings. Input uses Qt device discovery, not a fixed event
  number. On-device launch and typing are confirmed with Type Folio. Additional
  keyboard layouts and external USB hotplug still need user testing.
- Kitty inline/chunked and shared-memory images, PNG decoding, normal placement,
  scaling, cropping and deletion. Graphics are composed in memory and shown in
  grayscale. File and temporary-file image transports are disabled in the app.
- Sixel RGB/HLS palettes, repeats, raster attributes, transparency, fragmented
  streams, cursor-relative display, normal scrolling and full-screen clearing.
- Landscape or portrait display, adjustable font size, adaptive update batching,
  firmware waveform selection, and no idle cursor blinking. A retained grayscale
  surface redraws only rows marked dirty by libghostty; only those rows request
  an e-paper update during normal typing.

## Remaining compatibility work

This is not yet a complete replacement for a desktop kitty terminal. The first
renderer does not implement Unicode placeholder placements, animation playback,
all image z-order cases, or gray+alpha image rendering.

Sixel still needs DEC display modes, partial erasure, scrolling-region edge cases
and reflow refinements. It is therefore **not advertised in device attributes**
yet; use a client's explicit sixel output option for the preview. Rendering tests
exercise sixel directly and interleave it with kitty chunks; they do not establish
end-to-end compatibility with my Goblin Mosh workflow.

Kitty's separate file-transfer protocol is not implemented. The option to read
existing graphics files exists in the core API but is not exposed by the app.
The broader intended scope remains in [requirements](docs/requirements.md).

## Memory and storage

Each open terminal keeps **500 physical lines of scrollback in RAM**, excluding
the live screen, with a **4 MiB** storage ceiling. It caps libghostty allocations
at **64 MiB** and kitty image storage at **16 MiB per screen**. Sixel retains at
most **16 MiB** and 128 placements per terminal, plus at most 8 MiB of encoded
staging and a bounded decoded image. Slots allocate memory only when opened.
Qt display buffers, libpng scratch memory and programs running in the shells
need additional RAM. These limits do not reserve memory for nine large programs.
Each opened terminal also retains a grayscale display surface, about 2.3 MiB at
the usual landscape size, so incremental updates do not allocate and rasterize
the full screen for every character.
Off-screen images are reclaimed when their image budget reaches 75%, the
terminal allocation budget reaches 75%, or the tablet has less than 64 MiB of
available RAM. Visible images are kept where possible; hard image limits remain
the fallback. Sixel images whose anchors leave the 500-line history are removed.
Kitty images without surviving placements become eligible for reclamation.

There is no image-cache spill to flash. The launcher places runtime/cache files
in a service-owned RAM directory under `/run`, disables QML disk caching and shell history persistence for that
session, and suppresses application logs. Preflight requires tmpfs for `/tmp`
and `/dev/shm`, RAM-backed `/run`, and zero swap. Installed binaries and sources take normal
persistent storage; programs run inside the shell can also deliberately write
files. Text darkness, minimum contrast, and e-paper update profiles stay in RAM
until Inkline closes. Other settings are
written only when a preference changes, to
`~/.config/inkline/settings.ini`. Python uses the unmodified upstream runtime; optional Python and pip cache
preferences belong in the tablet’s `~/.bashrc`. See [Python configuration](docs/python.md).
The tablet launcher sets `HOME=/home/root`, so bare `cd`, `~`, and programs that
use the home directory work in every shell. A directly launched terminal also
supplies the account's home directory when HOME is missing or empty.

## USB keyboard and typewriter

Settings → USB (page 3) switches between USB Ethernet, outgoing keyboard and
USB host mode. Typewriter sends Folio input to a connected computer, including
committed text from the existing Japanese, Chinese and international input
methods. Choose US/ASCII, Mac, Linux or Windows (WinCompose) output. Escape or
Stop pauses; leaving Inkline restores networking, including after a crash.

Install the Python utility for outgoing keyboard mode. `inkline-usb` controls
the port; `inkline-type --file notes.txt --profile windows` types a UTF-8 file.
The sender streams files without a fixed size limit, spool files or bytecode
caches. The [PDF manual](docs/Inkline%20Manual.pdf), built from LaTeX and included
in installation, covers host setup, recovery and the `~/inkline usb/type` commands.

## Validation

For 0.4.2, shortcut configuration, keyboard and Settings tests pass on macOS
and reMarkable 2. ARM evdev tests exercise Opt plus right Alt, held
recovery, repeats and dropped events. T stays protected in the UI and CLI;
Ctrl+Alt input matches its normal terminal encoding. Physical testing of the
new chords remains separate from these automated checks.

For 0.3.8, eight isolated boot-service tests pass on macOS and reMarkable 2,
covering installation, legacy-link migration, reinstallation, removal and
unrelated-file protection. The repaired service launches Inkline through the
kernel keyboard input path on the tablet.
A fresh reboot confirmed automatic launcher startup and successful keyboard launch;
see the [device record](docs/device.md).

For 0.3.7, the graphics, stream and renderer suites pass on macOS and reMarkable 2.
They verify that the tablet’s BusyBox `clear` sequence removes Kitty and sixel
images from memory and the retained frame, including fragmented PTY input.
Partial and selective erases preserve images outside the cleared area.
The device graphics test reports zero process storage-write bytes.

For 0.3.6, all **ten ARM suites** pass on reMarkable 2, with the corresponding
host suites and an additional local control-socket integration test on macOS.
They cover graphics, sixel, stream parsing, PTYs, rendering, keyboard and global
shortcut configuration, sessions, clipboard, and input methods. The startup guide and logo render correctly
using the tablet's stock Qt libraries. Physical checks for the new shortcuts,
keyboard LEDs, physical pinch/pen behavior and USB hotplug remain outstanding.

On the reMarkable 2, a dense 1840 × 1280 diagnostic frame took about 554 ms to
rasterize in full. Updating a normal dirty text row on the retained surface took
about 0.37 ms. This removes the main application-side source of typing lag; the
selected e-paper waveform still determines the physical screen response.

See the [PDF manual](docs/Inkline%20Manual.pdf), [global shortcut guide](docs/global-shortcuts.md),
and [size-optimization measurements](docs/size-comparison.md) for the new controls,
recovery behavior and the measured limits of `-Os` savings.

The original five suites passed on reMarkable 2 running 3.27.3.0 for 0.1.0.
The device graphics suite measured **zero Linux
process storage-write bytes** across its image tests. This measurement applies
to that suite, not to arbitrary programs run inside Inkline.

The installer preflight, installation, timed e-paper demo and automatic return
to `xochitl` have been exercised on the tablet. See [device results](docs/device.md)
for the current validation record and manual checks still needed.

For host tests, install Qt 6 Quick and libpng development packages in addition to
Zig 0.16.0, CMake, Ninja and pkg-config:

```sh
cmake -S . -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Use `-DINKLINE_BUILD_APP=OFF` to build only the backend. To reuse a downloaded
Ghostty checkout, pass `-DFETCHCONTENT_SOURCE_DIR_GHOSTTY="$PWD/.cache/ghostty"`.
Run the built ARM suites entirely in tablet RAM with:

```sh
scripts/run-device-tests.sh root@10.11.99.1
```

## Source and dependencies

The SDK is pinned to **5.7.119**, published for the 3.27 firmware line. The Mac
cross build extracts its target libraries without running the Linux installer.
See [toolchain details](toolchains/README.md).

Libghostty-vt is pinned to commit
`448062571c5edf010b7490d06869b88b5ebf8f80`. Included patches fix ARM32 seeking,
select libc for Linux C embedding, bound history and graphics retention, and
remove images when BusyBox `clear` erases the screen. The core also handles
the pinned allocator ABI's logarithmic alignment.

Inkline includes GPL-3.0-or-later sixel code derived from Goblin Mosh and is
provided under that license. See [third-party notices](THIRD_PARTY.md) and
[LICENSE](LICENSE). Stock Qt and libpng are dynamically linked, not replaced or
redistributed by the installer.
