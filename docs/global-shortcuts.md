# Global program shortcuts

All global shortcuts use **Opt+RightAlt**, including T and emergency recovery.
On the Folio, hold the separate Opt key between Ctrl and Alt and the Alt/Opt
key to the right of the spacebar, then tap the letter. Do not hold Ctrl.
On a USB keyboard, hold Windows/Command (Super) and right Alt.
Plain Super and ordinary right-Alt shortcuts remain available.
Ordinary Ctrl+Alt goes to terminal programs: Emacs uses it for Ctrl+Meta
commands such as C-M-t (`transpose-sexps`) and C-M-f (`forward-sexp`).

Open **Settings → Command shortcuts (page 2)**, choose a letter, enter a command,
and press **Save**. Filled keys show the selected letter; a dot marks a saved
binding. Choose Terminal or Native e-paper app. **Remove** asks for confirmation;
**Reset draft** discards unsaved edits. T is visibly locked and cannot be changed.
Tab moves keyboard focus, arrow keys or letters choose a key, and Ctrl+A/C/X/V
edit command text. PageDown opens page 2; PageUp returns to page 1.

The command field accepts single/double quotes and backslash escapes for literal
arguments. It does not expand variables, wildcards, pipes or redirections.
For example, `/bin/sh -c 'date | cat'` explicitly requests a shell. Drafts stay in
RAM; only Save and confirmed Remove change stored bindings. The UI and the
installed command-line tool below share the same files and notify the launcher.

The small `inkline-hotkey.service` watches Folio and USB keyboards even when
Inkline is closed. It never grabs them or records ordinary typing.
It also discovers dedicated power-key devices. While Inkline owns the screen,
a press and release requests systemd suspend; the notebook application retains
its own power handling while it is active. See [power handling](power.md).

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

**Opt+RightAlt+T is permanent** and cannot be registered or deregistered. It stops
the native shortcut app and opens or resumes Inkline, preserving terminal sessions.

Hold **Opt+RightAlt+Backspace for two seconds** for emergency recovery. The launcher
kills the native app's service control group, closes every Inkline terminal,
and opens a fresh Inkline. Unsaved work in those sessions is lost. This works
independently of Inkline's event loop; a root app can still defeat it by grabbing
input exclusively, changing the launcher, or exhausting the whole system.
Apps should use Qt input without exclusive evdev grabs. A device restart is
the final fallback if neither chord responds.

Bindings live in `~/.config/inkline/shortcuts.d`. Registration writes a private
atomic file only when the binding changes. The daemon reloads on SIGHUP and
publishes a numeric key index in tmpfs; Inkline uses that to avoid forwarding
registered chords to terminal programs. Caps-as-Control is local to Inkline. Extra Ctrl, Shift, left Alt or a held
Caps Lock excludes a chord from the global launcher.

Native apps, the launcher, and ordinary terminal programs all run under the
tablet's root account. This feature is a launcher, not an application sandbox.

The Folio's separate Opt key is evdev 107 (Qt Meta, native scan 115), although
Linux calls that code KEY_END. The daemon recognizes the Folio by its input
identity and name before interpreting that key as Opt. USB keyboards use
KEY_LEFTMETA/KEY_RIGHTMETA; USB End remains an ordinary key.

Existing bindings migrate automatically to the new chord without rewriting
files. Quit and reopen Inkline after upgrading to get the new Settings page
and Ctrl+Alt passthrough. Open terminal sessions are preserved by installation.
