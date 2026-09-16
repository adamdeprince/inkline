# Install Inkline on reMarkable 2

Inkline is an early terminal application for reMarkable 2. This procedure is the
supported way to install, verify, launch and remove it. The current target is
firmware **3.27**, with device testing on **3.27.3.0**. It does not support
reMarkable 1, Paper Pro or Move. Read the current feature limits in the README
before relying on it for daily work.

## 1. Establish SSH access

Connect the tablet to your computer by USB and obtain its root password from
Settings → General → Help → About → Copyrights and licenses. reMarkable 2 does
not require Paper Pro's developer-mode reset; the manufacturer's
[developer documentation](https://developer.remarkable.com/documentation/developer-mode)
explicitly distinguishes the first two tablet generations.
On reMarkable 2, USB SSH normally uses `10.11.99.1`:

```sh
ssh root@10.11.99.1
exit
```

Verify the host fingerprint before accepting it. Inkline's installer refuses
unknown or changed host keys and never edits `known_hosts`. An SSH config alias
can replace `root@10.11.99.1` in all commands below. The alias `remarkable` is only
a convenience in the development setup, not an installer requirement.

## 2. Download the preview

On your computer, download `inkline-rm2.tar.gz` and
`inkline-rm2.tar.gz.sha256` from the
[0.4.1 preview release](https://github.com/adamdeprince/inkline/releases/tag/v0.4.1).
Use the attached Inkline bundle; GitHub's automatically generated source-code
archives do not contain the compiled application. The bundle is about 79 MiB
and includes the installer, uninstall script, source archives and licenses.
Installation needs SSH and SCP on your computer, with no SDK, compiler or
additional tablet package manager.

Alternatively, download both files from a terminal on your computer:

```sh
curl -fLO https://github.com/adamdeprince/inkline/releases/download/v0.4.1/inkline-rm2.tar.gz
curl -fLO https://github.com/adamdeprince/inkline/releases/download/v0.4.1/inkline-rm2.tar.gz.sha256
```

The same files are mirrored under
[`inkline.goblinreactor.com/downloads/v0.4.1/`](https://inkline.goblinreactor.com/downloads/v0.4.1/inkline-rm2.tar.gz).
To compile the application yourself, see [building from source](#building-from-source).

## 3. Check, then install

From the directory containing the two downloaded files, upload them to the
tablet's RAM and connect:

```sh
scp -o StrictHostKeyChecking=yes inkline-rm2.tar.gz inkline-rm2.tar.gz.sha256 root@10.11.99.1:/tmp/
ssh -o StrictHostKeyChecking=yes root@10.11.99.1
```

In the tablet's SSH session, run this block. It verifies the download, unpacks
it into a fresh RAM directory, runs preflight, installs, and cleans up the
temporary files. A failed checksum or preflight stops installation:

```sh
sh <<'INKLINE_INSTALL'
set -eu
cd /tmp
sha256sum -c inkline-rm2.tar.gz.sha256
inkline_stage=$(mktemp -d /tmp/inkline-install.XXXXXX)
trap 'rm -rf -- "$inkline_stage"' EXIT
trap 'exit 130' HUP INT TERM
tar -xzf inkline-rm2.tar.gz -C "$inkline_stage"
"$inkline_stage/inkline/install-device.sh" --check
"$inkline_stage/inkline/install-device.sh"
rm inkline-rm2.tar.gz inkline-rm2.tar.gz.sha256
INKLINE_INSTALL
```

Preflight checks the hardware, firmware, stock Qt plugins, font, available
memory policy and application startup. `/tmp`, `/dev/shm` and `/run` must be tmpfs and
swap must be disabled. The bundle is uploaded into RAM and its manifest is
checked before any persistent application files are created. `--check` leaves
the notebook interface running and makes no persistent installation changes.

Installation creates:

- `/home/root/.local/share/inkline/`: versioned application bundles, sources and licenses.
- `/home/root/inkline`: the command used to launch, stop and uninstall Inkline.
- A managed `/etc/systemd/system/inkline-hotkey.service` file and a relative
  enable link in `multi-user.target.wants/`. The unit is readable before `/home`
  mounts; `RequiresMountsFor` waits for the application bundle before starting
  the keyboard launcher. Updates migrate the old links into `/home` automatically.

An unrelated file at either location makes installation stop. Reinstalling the
same verified bundle is supported. Upgrades retain the previous release
directory, and the `current` symlink switches only after the new files verify.
The installer preserves an already running Inkline session; quit
and reopen Inkline when ready to use the update.
The notebook interface remains the default application. System libraries and
notebook files are left alone. The installer needs writable systemd configuration
for the shortcut service; it does not remount a read-only system partition.

When installation succeeds, type `exit` to return to your computer's shell.

## 4. Verify and launch

First run the display demonstration. It exits after ten seconds and restores
the notebook interface automatically:

```sh
ssh root@10.11.99.1 '~/inkline demo --quit-after 10'
```

Then press **Opt+RightAlt+T on your Type Folio or external USB keyboard** to launch
a shell directly on the tablet. The shortcut remains available after reboot.
The small launcher observes the shortcut without grabbing the keyboard or
recording typed text.

SSH remains available as a fallback:

```sh
ssh root@10.11.99.1 '~/inkline start'
```

The shell runs on the tablet. Use SSH or your preferred mosh client from that
shell to reach another machine. Inkline does not install a mosh client.

Use Type Folio or a USB keyboard. Qt discovers input devices; Inkline does not
assume a particular `/dev/input/eventN`. For a USB keyboard, a suitable USB host
adapter or hub is required. When the keyboard occupies the USB port, use the on-device shortcut. Wi-Fi SSH
using the tablet's Wi-Fi address is another option. Actual USB host wiring and keyboard
layout/hotplug behavior should be checked with your hardware.

Useful controls:

- Tap **Quit** or press **Ctrl+Shift+Q**, then confirm, to return to notebooks.
  Type `exit` to close one terminal; closing the last one also returns to notebooks.
- Tap **Esc** or use **Ctrl+[** when your Folio has no Escape key.
- **Shift+PageUp/PageDown**, or the bottom touch buttons, scroll history.
- Drag two fingers vertically to scroll the 500-line history; swipe them left
  or right to switch terminals. One finger selects text. The pen reports mouse
  events to applications that request them; hold Shift to select text instead.
- `~/inkline start --rotate 270` reverses landscape orientation; `--rotate 0` uses portrait.
- `~/inkline start --font-size 32` overrides the saved text size for this launch (6–48 pixels).
- Pinching and Settings adjust text size inside the terminal. Pinch in one-pixel
  steps for finer control; keyboard zoom combinations have been removed.
- Alt+Space opens the Unicode keyboard; tap Settings there to select Asian input methods; Option+C/V uses the RAM clipboard.
- `~/inkline stop` restores notebooks from another SSH session.
- Press and release the physical power button to suspend; press it again to
  wake. Shells and editor buffers stay in RAM. Network connections may need
  to reconnect. Quit and reopen Inkline after upgrading to activate this behavior.

Inkline 0.4.1 includes right Alt/Option shortcuts, nine terminal slots,
quit confirmation, a Settings button, and a saved Caps Lock/Control toggle.
Each new terminal shows a brief guide and a tiny Goblin logo.
Inkline sets `HOME=/home/root` for tablet sessions, including shells opened by
the keyboard shortcut, so `cd` without arguments returns home.
See [keyboard settings](keyboard.md) for the complete controls.

### Program shortcuts and the manual

Register a program with `~/inkline shortcut register e emacs`, then launch it
with Opt+RightAlt+E. Use `~/inkline shortcut list` and `~/inkline shortcut deregister e`
to manage bindings. Opt+RightAlt+T is permanent. Hold Opt+RightAlt+Backspace for two
seconds to stop a malfunctioning shortcut app and restart Inkline; emergency
recovery closes all terminal sessions. See [global shortcuts](global-shortcuts.md)
for native e-paper apps, session preservation, and recovery limits.

The bundle includes one [Inkline Manual.pdf](Inkline%20Manual.pdf), with all nine
website languages and a linked language index at the beginning. Installation
tries to import it into **My files** through the USB web interface when that
interface is enabled and notebooks are running. Otherwise the PDF stays in the
bundle. Enable the tablet's USB web interface, quit Inkline, and run
`~/inkline manual` from SSH to retry. The same PDF can be imported through the
USB browser interface or reMarkable's apps. Installation never writes notebook
metadata directly, and removal keeps an imported manual.

Opening Inkline temporarily stops `xochitl`, the notebook interface. A service
loaded only into `/run` restores it on normal exit and process failure. The
terminal survives the SSH connection closing. A separate shortcut service is enabled at boot. Rebooting starts the usual
notebook interface with the shortcut available in the background.

### Display response, contrast, and shell preferences

Open Settings with Alt+Space followed by F2 and drag **Text darkness**, or focus
it with Tab and use Left/Right. 50% is the original rendering; darker values
strengthen antialiased letter edges. Text colors and graphics retain their
original values. **Minimum contrast** keeps foreground and background luminance
apart when a terminal program chooses hard-to-read colors.

The **E-paper updates** row offers five tradeoffs. **Crisp is the default** and
uses the higher-quality content mode. Fast uses the low-latency animation mode,
Balanced uses normal UI mode, Mono favors black-and-white text, and Saver batches
command output for 120 ms so bursts request fewer screen updates. Existing
explicitly saved modes are preserved. All settings stay in RAM across terminals,
Settings visits and sleep. Inkline saves changed preferences together at normal
exit, including a service stop; unchanged sessions write nothing. Forced kills,
crashes and power loss can discard changes made since launch.

Normal typing redraws only libghostty's dirty rows on a retained grayscale
surface. This avoids the previous full-screen raster and comparison on every
character; e-paper waveform time remains and depends on the chosen profile.

Inkline starts interactive Bash so the tablet's `~/.bashrc` is loaded. Optional
Python/pip cache preferences belong in that file; see [Python setup](python.md).

## Recovery and removal

To reclaim older utility releases without uninstalling the current version,
preview with `~/inkline cleanup emacs --check`, then run
`~/inkline cleanup emacs`. Replace `emacs` with another installed utility name
as needed. Current releases, releases used by running programs and unrecognized
paths are kept. Close an old program and retry to remove its retained files.
Newly built Emacs installers run this cleanup after a successful upgrade.

From SSH:

```sh
~/inkline stop
~/inkline uninstall
```

Removal stops and disables the shortcut service, stops Inkline, starts and
checks `xochitl`, removes the temporary service link, and deletes only Inkline's marked application directory and launcher. It
does not remove files you created elsewhere from the shell.

If the launcher itself is unavailable, restore the stock interface with:

```sh
systemctl stop inkline.service
systemctl start xochitl.service
```

Firmware updates can change Qt or the display plugin ABI. After updating, run
the installer's preflight again before launching. The current installer refuses
firmware outside the 3.27 line rather than guessing compatibility.

## Building from source

From an Inkline source checkout, install **Zig 0.16.0**, **CMake 3.24+**, **Ninja**,
**pkg-config**, **Git**, and **Python 3.12+**. The tablet build uses the official
SDK's Qt, libpng and GNU C++ libraries. Host Qt is only needed for desktop tests.
The cross build has been exercised on Apple Silicon macOS; Linux host support
has not yet been tested. Leave several gigabytes of free space for the SDK,
extracted headers, source and compiler caches.

```sh
git clone https://github.com/adamdeprince/inkline.git
cd inkline
scripts/build-tablet.sh
python3 scripts/package.py
scripts/install.sh root@10.11.99.1 --check
scripts/install.sh root@10.11.99.1
```

The build downloads the pinned SDK (about 400 MB), verifies its SHA-256, and
extracts the target headers and libraries. It does not execute the Linux SDK
installer. It also fetches the pinned libghostty-vt source and applies the
included ARM/libc patches. The result is `build/dist/inkline-rm2.tar.gz`, with an
adjacent SHA-256 file. The host-side script uploads this bundle into a fresh RAM
directory and uses the same included device installer as the prebuilt release.

The single nine-language PDF is built from `docs/manual.tex` and its language
chapters with **LuaLaTeX**: `python3 scripts/build-manual.py`. See the
[manual build instructions](manual/README.md) for matching macOS/Linux tools,
fonts and temporary cache paths. The finished PDF is committed, so installers
need no TeX or PDF generation tools. LaTeX utility installation instructions
remain on the website.

Global launchers use the separate Folio Opt key; on USB keyboards its equivalent
is Windows/Command (Super). Settings → Command shortcuts is page 2 of Settings.
The terminal launcher, custom commands and held emergency recovery all require
Opt+RightAlt. Ordinary Ctrl+Alt remains available to Emacs. After upgrading, quit
and reopen Inkline to load the new input handling and Settings page.
