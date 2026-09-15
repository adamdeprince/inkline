# Global program shortcuts

The small `inkline-hotkey.service` watches Folio and USB keyboards even when
Inkline is closed. It never grabs them or records ordinary typing.

```sh
~/inkline shortcut register e emacs
~/inkline shortcut register p python3
~/inkline shortcut list
~/inkline shortcut deregister p
~/inkline run emacs notes.txt
```

Programs open in a new Inkline PTY, using an empty slot. At most nine terminals
can be open. Arguments are stored literally, without shell interpretation.
Register a shell explicitly if you want shell syntax. Keys refer to US physical
letters, digits or punctuation; quote punctuation in your shell. A new
registration replaces the old one. Deregistration does not kill a running app.

Shortcut programs start through Bash, which reads `~/.bashrc`
before executing the program. Shell environment preferences, including optional
Python cache settings, therefore apply to these launches too.

For an app that draws its own e-paper screen:

```sh
~/inkline shortcut register a --epaper /home/root/my-app
```

Only one native shortcut app runs at once. Its transient systemd service pauses
Inkline and its shells, gives the app the stock Qt e-paper environment and a
private `/run/inkline-shortcut-app` runtime/cache directory, and discards logs.
After the app exits or crashes, Inkline resumes and repaints. If Inkline was
closed, notebooks return. App documents still write normally.

**Ctrl+Alt+T is permanent** and cannot be registered or deregistered. It stops
the native shortcut app and opens or resumes Inkline, preserving terminal sessions.

Hold **Ctrl+Alt+Backspace for two seconds** for emergency recovery. The launcher
kills the native app's service control group, closes every Inkline terminal,
and opens a fresh Inkline. Unsaved work in those sessions is lost. This works
independently of Inkline's event loop; a root app can still defeat it by grabbing
input exclusively, changing the launcher, or exhausting the whole system.
Apps should use Qt input without exclusive evdev grabs. A device restart is
the final fallback if neither chord responds.

Bindings live in `~/.config/inkline/shortcuts.d`. Registration writes a private
atomic file only when the binding changes. The daemon reloads on SIGHUP and
publishes a numeric key index in tmpfs; Inkline uses that to avoid forwarding
registered chords to terminal programs. Caps-as-Control is local to Inkline;
use a physical Ctrl key for the global launcher.

Native apps, the launcher, and ordinary terminal programs all run under the
tablet's root account. This feature is a launcher, not an application sandbox.

The earlier Ctrl+Alt+T launcher was tested with a synthetic keyboard and
confirmed on Type Folio. Physical Folio and USB testing of the new bindings
and emergency chord remains separate from automated evdev/service checks.
