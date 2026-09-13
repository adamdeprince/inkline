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

## 2. Build and package

From an Inkline source checkout, install **Zig 0.16.0**, **CMake 3.24+**, **Ninja**,
**pkg-config**, **Git**, and **Python 3.12+**. The tablet build uses the official
SDK's Qt, libpng and GNU C++ libraries. Host Qt is only needed for desktop tests.
The cross build has been exercised on Apple Silicon macOS; Linux host support
has not yet been tested. Leave several gigabytes of free space for the SDK,
extracted headers, source and compiler caches.

```sh
scripts/build-tablet.sh
python3 scripts/package.py
```

The build downloads the pinned SDK (about 400 MB), verifies its SHA-256, and
extracts the target headers and libraries. It does not execute the Linux SDK
installer. It also fetches the pinned libghostty-vt source and applies the
included ARM/libc patches. The result is `build/dist/inkline-rm2.tar.gz`, with an
adjacent SHA-256 file. A preview bundle and checksum are also available from the
[Inkline website](https://inkline.goblinreactor.com/).

## 3. Check, then install

```sh
scripts/install.sh root@10.11.99.1 --check
scripts/install.sh root@10.11.99.1
```

Preflight checks the hardware, firmware, stock Qt plugins, font, available
memory policy and application startup. `/tmp`, `/dev/shm` and `/run` must be tmpfs and
swap must be disabled. The bundle is uploaded into RAM and its manifest is
checked before any persistent application files are created. `--check` leaves
the notebook interface running and makes no persistent installation changes.

Installation creates:

- `/home/root/.local/share/inkline/`: versioned application bundles, sources and licenses.
- `/home/root/inkline`: the command used to launch, stop and uninstall Inkline.
- Two systemd symlinks under `/etc/systemd/system/` that enable the small
  `inkline-hotkey.service` keyboard launcher at boot.

An unrelated file at either location makes installation stop. Reinstalling the
same verified bundle is supported. Upgrades retain the previous release
directory, and the `current` symlink switches only after the new files verify.
The notebook interface remains the default application. System libraries and
notebook files are left alone. The installer needs writable systemd configuration
for the shortcut service; it does not remount a read-only system partition.

To install an already built bundle without this checkout, copy it and its checksum
to the tablet's `/tmp`, verify, unpack, and run its included installer:

```sh
scp inkline-rm2.tar.gz inkline-rm2.tar.gz.sha256 root@10.11.99.1:/tmp/
ssh root@10.11.99.1
cd /tmp
sha256sum -c inkline-rm2.tar.gz.sha256
work=$(mktemp -d /tmp/inkline-install.XXXXXX)
tar -xzf inkline-rm2.tar.gz -C "$work"
"$work/inkline/install-device.sh" --check
"$work/inkline/install-device.sh"
rm -rf -- "$work"
rm /tmp/inkline-rm2.tar.gz /tmp/inkline-rm2.tar.gz.sha256
```

## 4. Verify and launch

First run the display demonstration. It exits after ten seconds and restores
the notebook interface automatically:

```sh
ssh root@10.11.99.1 '~/inkline demo --quit-after 10'
```

Then press **Ctrl+Alt+T on your Type Folio or external USB keyboard** to launch
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

- Tap **Quit**, type `exit`, or press **Ctrl+Shift+Q** to return to notebooks.
- Tap **Esc** or use **Ctrl+[** when your Folio has no Escape key.
- **Shift+PageUp/PageDown**, or the bottom touch buttons, scroll history.
- `~/inkline start --rotate 270` reverses landscape orientation; `--rotate 0` uses portrait.
- `~/inkline start --font-size 32` increases text size (valid range: 16–48 pixels).
- `~/inkline stop` restores notebooks from another SSH session.

Opening Inkline temporarily stops `xochitl`, the notebook interface. A service
loaded only into `/run` restores it on normal exit and process failure. The
terminal survives the SSH connection closing. A separate shortcut service is enabled at boot. Rebooting starts the usual
notebook interface with the shortcut available in the background.

## Recovery and removal

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
