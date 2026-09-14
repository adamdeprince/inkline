# Keyboard and terminal settings

These controls are included in **Inkline 0.3.0**. All nine host and ARM test
suites pass; physical Folio and USB checks for the new controls are pending.

Each new terminal prints a short shortcut guide with a small Goblin logo and the
current Caps Lock setting before starting the shell. Switching back to an open
terminal preserves its contents and does not repeat the guide.

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
| C / V | Copy selection / paste clipboard |
| X | Copy selection and explain how to cut in the editor |
| + / − | Grow / shrink terminal text |

The number shortcuts use the physical top number row. USB numeric keypads keep
their usual behavior. Left Alt remains available for ordinary terminal input,
including Emacs Meta commands, except that Alt +/− also resizes text. Ctrl and Shift can combine with the function keys.
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
moves keyboard focus; Left/Right changes a value, and Enter or Space activates it.
A **solid black fill and filled circle** mark the active choice. A **dashed outline**
marks keyboard focus, independently of the chosen value. Escape closes Settings. Touch also
works throughout the panel.

- **Caps Lock key:** choose Control (the default) or Caps Lock. In Control mode,
  hold Caps Lock while pressing a letter, such as Caps Lock+C for Ctrl+C. This
  applies inside Inkline; the choice does not change notebook keyboard settings.
- **Bottom bar:** choose Shown or Hidden. **Ctrl+Shift+B** toggles it directly.
  Hiding the bar gives its rows back to the terminal. Right Alt+Space remains
  available while it is hidden.

Preferences are stored in `~/.config/inkline/settings.ini` and survive restarts.
Font changes are saved after the pinch ends or key repeats pause for 700 ms.
Only a changed preference writes this file; typing, graphics and switching
terminals do not save it. Ordinary Caps Lock's on/off state starts off each time.

The touch Page up/Page down buttons scroll terminal history. On a keyboard,
use **Shift+PageUp/PageDown** or **Shift+Right Alt+Up/Down** for history.
Right Alt+Up/Down without Shift sends page keys to the current application.

## Text size

Hold **Alt/Option** and press **+** (or `=`) or **−** to resize text in two-pixel
steps, from 16 to 48 pixels. Both Alt keys work for resizing. You can also pinch
with two fingers anywhere in Inkline, or use Settings' minus and plus buttons.
The size applies to all six terminals, and existing shells receive a resize
without restarting. A cancelled pinch restores the previous size.

## Text selection and clipboard

Drag one finger, the pen, or a mouse across text. The selected cells invert.
Dragging past the top or bottom scrolls history; a tap clears the selection.
Hold **right Alt/Option+C** to copy and **right Alt/Option+V** to paste. Soft-wrapped
lines copy as one line, and Unicode characters remain intact. The clipboard is
shared by all six terminals and is cleared when Inkline exits. It is private to
Inkline, not the notebook application's clipboard.

Applications can set or query this clipboard through **OSC 52**. The clipboard
holds UTF-8 text up to **128 KiB**; oversized writes are rejected without
truncating or replacing existing contents. Its contents and selection are never
saved to disk. Programs in any open terminal can query it, including a remote
program when the connection forwards OSC 52. Paste respects bracketed-paste and
kitty clipboard paste-event modes and strips unsafe control bytes. An explicit
paste can contain multiple lines; at an unbracketed shell prompt these can run
as commands.

**Cut needs the editor's cooperation.** The terminal displays output but has no
way to delete the corresponding document text. **Option+X** copies a terminal
selection and displays this explanation. Use the editor's own Cut command to
remove text; an editor with OSC 52 clipboard integration can send that cut text
to Inkline. OSC 52 itself does not request deletion.

## Asian and accented input

Open **Option+Space → Input method**. Tap a method or select it with Up/Down and
Enter. **Off** restores direct keyboard input. The selection is saved for all
terminals; each terminal keeps its own unfinished composition.

| Method | Entry |
| --- | --- |
| Romaji | Japanese hiragana, such as `nihongo` → `にほんご`; no kanji conversion |
| Pinyin | Chinese syllable candidates, such as `ni` + Space → `你` |
| Zhuyin | GoblinView's basic Bopomofo keyboard mapping, such as `su` + Space → `你`; not a complete Taiwanese IME |
| Wubi 86 | Chinese shape-code candidates, such as `a` + Space → `工` |
| US-International | Common dead-key accents, such as `'e` → `é` and `~n` → `ñ` |

The strip above the bottom bar shows composition and numbered candidates.
**Space/Enter** selects the first candidate, **1–9** or a tap selects another,
and **[ / ]** (also `, / .` or `− / =`) changes pages. Zhuyin gives its letter
keys priority, including digit keys assigned to Bopomofo; tapping candidates
works there too. **Backspace** edits composition; **Escape** cancels it. Ctrl/Alt
commands still reach the running program. Switch to Off for literal ASCII entry.

The engine and dictionaries are adapted from GoblinView. The dictionaries load
on demand in RAM; there is no typing history, learned dictionary, or input cache.
Inkline includes a private Noto CJK font, without changing system fonts.

## Wired USB keyboards

Connect the keyboard through a suitable USB host adapter or hub. Inkline uses
Qt's keyboard discovery, so no event-device number needs configuration. The same
shortcuts and Caps Lock setting apply to Folio and USB input. USB hotplug,
keyboard LEDs and layout-specific combinations remain physical acceptance checks.
When the keyboard occupies the USB port, use the tablet's Wi-Fi address for SSH.

The installer preserves open terminals during an update.
Quit and reopen Inkline once to start the new version. The older release stays
on storage for the existing process and rollback.
