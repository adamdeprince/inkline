# Keyboard and terminal settings

These controls are implemented in the **0.2.0 source build**. The published
0.1.0 preview does not include them yet. Host tests and the ARM build pass;
physical Folio and USB checks for the new controls are pending.

Press **Ctrl+Alt+T** to open Inkline on the tablet. Hold the **right Alt/Option**
key to reach keys missing from the Folio:

| Right Alt/Option + | Result |
| --- | --- |
| 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 | F1, F2, F3, F4, F5, F6, F7, F8, F9, F10 |
| Tab | Escape |
| Up / Down | PageUp / PageDown, sent to the program |
| Left / Right | Previous / next terminal |
| Space | Open or close Settings |
| Backspace | Ask to quit Inkline |

The number shortcuts use the physical top number row. USB numeric keypads keep
their usual behavior. Left Alt remains available for ordinary terminal input,
including Emacs Meta commands. Ctrl and Shift can combine with the function keys.
Right Alt is reserved for the listed shortcuts, including on layouts that call
it AltGr; other keys retain their layout's normal input.

## Six terminals

Left and Right wrap through six numbered slots. Visiting an empty slot starts a
shell. Each slot keeps its own programs, working directory, screen, graphics and
scrollback, and background terminals continue receiving output. The Settings
button shows the active slot; when the bar is hidden, a brief label appears after
switching terminals.

Type `exit` or press Ctrl+D at a shell prompt to close that terminal. Inkline
switches to another open terminal, or returns to notebooks after the last one
closes. **Right Alt+Backspace**, **Ctrl+Shift+Q**, and the **Quit** button ask
before closing all terminals. The prompt starts on **Cancel**. Press Escape to
cancel, or select Quit with Tab/Right and press Enter to confirm.

## Caps Lock and the bottom bar

Tap the fifth button, **Settings**, or press **Right Alt+Space**. Tab or Up/Down
selects a setting; Enter or Space changes it. Escape closes Settings. Touch also
works throughout the panel.

- **Caps Lock key:** choose Control (the default) or Caps Lock. In Control mode,
  hold Caps Lock while pressing a letter, such as Caps Lock+C for Ctrl+C. This
  applies inside Inkline; the choice does not change notebook keyboard settings.
- **Bottom bar:** choose Shown or Hidden. **Ctrl+Shift+B** toggles it directly.
  Hiding the bar gives its rows back to the terminal. Right Alt+Space remains
  available while it is hidden.

Preferences are stored in `~/.config/inkline/settings.ini` and survive restarts.
Only a changed preference writes this file; typing, graphics and switching
terminals do not save it. Ordinary Caps Lock's on/off state starts off each time.

The touch Page up/Page down buttons scroll terminal history. On a keyboard,
use **Shift+PageUp/PageDown** or **Shift+Right Alt+Up/Down** for history.
Right Alt+Up/Down without Shift sends page keys to the current application.

## Wired USB keyboards

Connect the keyboard through a suitable USB host adapter or hub. Inkline uses
Qt's keyboard discovery, so no event-device number needs configuration. The same
shortcuts and Caps Lock setting apply to Folio and USB input. USB hotplug,
keyboard LEDs and layout-specific combinations remain physical acceptance checks.
When the keyboard occupies the USB port, use the tablet's Wi-Fi address for SSH.

The 0.2.0 source installer preserves open terminals during an update.
Quit and reopen Inkline once to start the new version. The older release stays
on storage for the existing process and rollback.
