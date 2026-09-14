# Inkline

Inkline is a terminal for **reMarkable 2**, built with **libghostty-vt** and the
stock Qt e-paper backend. It is designed for Type Folio and external USB
keyboards, with kitty graphics and an initial sixel implementation.

**The published preview is 0.1.0; this checkout contains 0.2.0 development work.**
The new keyboard controls and multiple terminals pass host tests and cross-build
for ARM; their device checks are pending. The main artifact is the repeatable
[installation procedure](docs/install.md), including preflight, launch, recovery
and uninstall. It targets firmware **3.27**, tested on **3.27.3.0**. Other models
and firmware lines are not supported by this installer.

## Install

Download the **[Inkline 0.1.0 preview](https://github.com/adamdeprince/inkline/releases/tag/v0.1.0)**
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

After installation, press **Ctrl+Alt+T on the tablet’s Type Folio or USB keyboard**
to open Inkline. No second computer is needed for subsequent launches. Tap
**Quit**, press **Ctrl+Shift+Q**, or exit the shell to return to notebooks.
In 0.2.0, Quit asks for confirmation and `exit` closes only the current terminal.
The launcher temporarily switches from `xochitl` to Inkline and restores it when
Inkline stops. A small keyboard shortcut service starts at boot; the terminal itself opens only
on request, and the usual notebook interface remains the default.

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

## Keyboard controls in 0.2.0

Hold the **right Alt/Option** key for these Folio and USB keyboard shortcuts:

| Keys with right Alt/Option | Action |
| --- | --- |
| `1` through `0` | F1 through F10 |
| Tab | Escape |
| Up / Down | PageUp / PageDown |
| Left / Right | Previous / next terminal, across six slots |
| Space | Open or close Settings |
| Backspace | Confirm quitting all terminals |

**Ctrl+Shift+B** hides or shows the bottom bar. Its fifth button opens
**Settings**, where Caps Lock can act as **Control** (the default) or normal
**Caps Lock**. Both settings persist across launches. Each terminal has its own
shell and scrollback; switching to an unused slot opens a shell there.
See [keyboard and session instructions](docs/keyboard.md) for details.

## Current source features

- Up to six local interactive shells with controlling PTYs, resizing, scrollback and
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
- Landscape or portrait display, adjustable font size, coalesced updates, and no
  idle cursor blinking. Only changed pixel rows request a screen update.

## Remaining compatibility work

This is not yet a complete replacement for a desktop kitty terminal. The first
renderer does not implement Unicode placeholder placements, animation playback,
all image z-order cases, or gray+alpha image rendering. Mouse reporting,
selection/copy/paste and clipboard integrations are also unfinished.

Sixel still needs DEC display modes, partial erasure, scrolling-region edge cases
and reflow refinements. It is therefore **not advertised in device attributes**
yet; use a client's explicit sixel output option for the preview. Rendering tests
exercise sixel directly and interleave it with kitty chunks; they do not establish
end-to-end compatibility with my Goblin Mosh workflow.

Kitty's separate file-transfer protocol is not implemented. The option to read
existing graphics files exists in the core API but is not exposed by the app.
The broader intended scope remains in [requirements](docs/requirements.md).

## Memory and storage

Each open terminal caps libghostty allocations at **64 MiB**, kitty image
storage at **16 MiB per screen**, and scrollback at **4 MiB**. Sixel retains at
most **16 MiB** and 128 placements per terminal, plus at most 8 MiB of encoded
staging and a bounded decoded image. Slots allocate memory only when opened.
Qt display buffers, libpng scratch memory and programs running in the shells
need additional RAM. These limits do not reserve memory for six large programs.

There is no image-cache spill to flash. The launcher places runtime/cache files
in a service-owned RAM directory under `/run`, disables QML disk caching and shell history persistence for that
session, and suppresses application logs. Preflight requires tmpfs for `/tmp`
and `/dev/shm`, RAM-backed `/run`, and zero swap. Installed binaries and sources take normal
persistent storage; programs run inside the shell can also deliberately write
files. Settings are written only when a preference changes, to
`~/.config/inkline/settings.ini`. The revised Python package disables automatic
bytecode caches; the installer does not constrain other programs' own caches.

## Validation

All **seven** suites pass on the development Mac: core graphics, sixel decoding,
mixed-stream parsing, shell PTY, renderer/keyboard integration, shortcut mapping,
and terminal sessions. The two new suites also pass with address and undefined
behavior sanitizers. The 0.2.0 ARM build passes; its device tests are pending.

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
`448062571c5edf010b7490d06869b88b5ebf8f80`. Two included patches fix ARM32 seeking
and select libc for Linux C embedding, avoiding Wuffs allocator stubs overriding
glibc. The core also handles the pinned allocator ABI's logarithmic alignment.

Inkline includes GPL-3.0-or-later sixel code derived from Goblin Mosh and is
provided under that license. See [third-party notices](THIRD_PARTY.md) and
[LICENSE](LICENSE). Stock Qt and libpng are dynamically linked, not replaced or
redistributed by the installer.
