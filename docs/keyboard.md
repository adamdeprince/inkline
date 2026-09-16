# Keyboard and terminal settings

These controls are included in **Inkline 0.3.6**. Host and ARM integration tests
pass. Physical Folio/USB shortcut and touch checks for the new controls remain
separate from those automated tests.

Each new terminal prints a short shortcut guide with a small Goblin logo and the
current Caps Lock setting before starting the shell. Switching back to an open
terminal preserves its contents and does not repeat the guide.

Press **Ctrl+Opt+Alt+T** to open Inkline on the tablet.

Hold the separate **Opt** key, between Ctrl and Alt on the Folio, and press
**1, 2, 3, 4, 5, 6, 7, 8, 9, 0** for **F1–F10**. This uses the keyboard's Meta
modifier. On USB keyboards, the shortcut follows the active layout's Meta key;
physical function keys continue to work normally.

Hold the **right Alt/Option** key for these other shortcuts:

| Right Alt/Option + | Result |
| --- | --- |
| Tab | Escape |
| Up / Down | PageUp / PageDown, sent to the program |
| Left / Right | Previous / next terminal |
| Space | Open or close the Unicode keyboard (either Alt key) |
| 1–9 | Select terminal slot 1–9 |
| Backspace | Ask to quit Inkline |
| C / V | Copy selection / paste clipboard |
| X | Copy selection and explain how to cut in the editor |

The number shortcuts use the physical top number row. USB numeric keypads keep
their usual behavior. Left Alt remains available for ordinary terminal input,
including Emacs Meta commands, except for Alt+Space. Ctrl, Shift, and Alt can combine with Opt's
function keys.
Right Alt is reserved for the listed shortcuts, including on layouts that call
it AltGr; other keys retain their layout's normal input.

On the **US Type Folio**, **right Alt/Option + 0** types `+`. The right-hand
modifier no longer turns the number row into function keys.
Hold **right Alt/Option** and press the **minus key
immediately left of Backspace** (marked `-`, `_`, and `=`) to type `=`.
With input method **Off**, **Shift+6** types a literal `^`: typing `c^2` produces
those three characters. The Folio's standalone accent keys are converted to
literal punctuation before Inkline's selected input method processes them.
US-International still composes accents when explicitly selected; pasted and
committed Unicode text is preserved.

## Nine terminals

Left and Right wrap through nine numbered slots. Visiting an empty slot starts a
shell. Each slot keeps its own programs, working directory, screen, graphics and
scrollback, and background terminals continue receiving output. The Settings
button shows the active terminal among the number currently open, with a slot label when slots are nonconsecutive; when the bar is hidden, a brief label appears after
switching terminals.

Swipe **two fingers left** for the next terminal or **right** for the previous
one. Each swipe changes one slot and wraps through the same nine slots as the
keyboard. One-finger dragging remains text selection.

Type `exit` or press Ctrl+D at a shell prompt to close that terminal. Inkline
switches to another open terminal, or returns to notebooks after the last one
closes. **Right Alt+Backspace**, **Ctrl+Shift+Q**, and the **Quit** button ask
before closing all terminals. The prompt starts on **Cancel**. Press Escape to
cancel, or select Quit with Tab/Right and press Enter to confirm.

## Caps Lock and the bottom bar

Tap the fifth button, **Settings**, or press **Alt+Space**, then **F2** in the Unicode keyboard. Tab or Up/Down
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

Keyboard, input-method, font-size, and bottom-bar preferences are stored in
`~/.config/inkline/settings.ini` and survive restarts. Display tuning stays in RAM.
Font changes remain in RAM and are saved when Inkline exits normally.
Only a changed preference writes this file; typing, graphics and switching
terminals do not save it. Ordinary Caps Lock's on/off state starts off each time.

The touch Page up/Page down buttons scroll terminal history. On a keyboard,
use **Shift+PageUp/PageDown** or **Shift+Right Alt+Up/Down** for history.
Right Alt+Up/Down without Shift sends page keys to the current application.

Drag **two fingers down** to reveal older output or **up** to return toward the
prompt. Inkline distinguishes the shared movement from a pinch and locks the
gesture once it starts, so small spacing changes do not resize the text. These
gestures work in portrait and either landscape orientation.

Each terminal retains **500 physical lines of history**, excluding the live
screen, entirely in RAM. Older lines are discarded; wrapping and font changes
can change how many physical lines the same text occupies. Off-screen graphics
can disappear from retained history under memory pressure. Typing returns to
the live prompt, and full-screen applications keep their usual alternate screen.

## Text size

**Pinch with two fingers** to resize text from **6 to 48 pixels**. Pinching has
about half its previous proportional response and moves in **one-pixel steps**
for finer control. Settings' minus and plus buttons still adjust by two pixels.
The size applies to all nine terminals, and existing shells receive a resize
without restarting. A cancelled pinch restores the previous size.

Keyboard zoom combinations have been removed from both Alt keys. Punctuation
and ordinary Alt input now reach the running program.

## Text selection and clipboard

Drag one finger, the pen, or a mouse across text. When an application requests
mouse input, hold Shift to select locally with the pen or mouse instead.
The selected cells invert.
Dragging past the top or bottom scrolls history; a tap clears the selection.
Hold **right Alt/Option+C** to copy and **right Alt/Option+V** to paste. Soft-wrapped
lines copy as one line, and Unicode characters remain intact. The clipboard is
shared by all nine terminals and is cleared when Inkline exits. It is private to
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

Open **Alt+Space → Settings → Input method**. Tap a method or select it with Up/Down and
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

## E-paper updates

The **E-paper updates** row changes both output batching and the stock firmware
waveform used for Inkline's full-screen region:

| Profile | Firmware mode | Behavior |
| --- | --- | --- |
| Fast | Animation | Immediate interactive redraws and 16 ms output batching. Lowest delay, with more visible ghosting. |
| Balanced | UI | Clear UI updates, 4 ms interactive delay and 32 ms output batching. |
| Crisp (default) | Content | The cleanest grayscale waveform, with 12 ms interactive delay and 60 ms batching. Physical updates take longer. |
| Mono | Mono | Fast black-and-white text with 24 ms output batching. Graphics lose their gray shades. |
| Saver | UI | 40 ms interactive delay and 120 ms batching, requesting fewer updates during bursts. |

The setting applies to the whole Inkline window and all nine terminals. It stays
in RAM and returns to Fast when Inkline closes. Fast or Balanced suits typing;
Crisp suits static graphics and dense pages; Saver suits long command output
when immediate feedback matters less.

Inkline also keeps one grayscale surface per open terminal. Libghostty marks
changed rows, so typing redraws those rows directly instead of rebuilding and
comparing the whole screen. On the reMarkable 2 test device, a dirty text-row
render averaged about 0.37 ms while a dense full 1840 × 1280 render averaged
about 554 ms. The physical e-paper update follows that application render.

## Text darkness and minimum contrast

In Settings (Alt+Space, then F2), drag **Text darkness** with a finger, pen
or mouse. Keyboard users can focus the slider with Tab or Up/Down and adjust
with Left/Right; Home/End selects the endpoints. 50% preserves the original
rendering. Normal text is already black; higher values darken its smoothed
edges without changing glyph positions, graphics, backgrounds, or app colors.

**Minimum contrast** prevents terminal foreground and background luminance from
being too close. 0% disables the floor; higher values move only the text color
toward black or white until the requested gap is reached. Backgrounds and
graphics are unchanged.

The surrounding terminal previews both changes. All preferences stay in RAM
when you lift your finger, close Settings, switch terminals or suspend. Changed
values are saved together on normal Inkline exit (including a service stop);
unchanged sessions write nothing. A forced kill, crash or power loss can lose
changes since launch. Darkness defaults to 50%, minimum contrast to 35%, and
the update mode to Crisp; explicitly saved values take precedence.

## Power button and sleep

Press and release the tablet's physical power button to suspend, and press it
again to wake. Inkline and its shells remain in RAM. No notebook handoff or
settings save occurs during sleep; network connections may need to reconnect.
The launcher ignores the wake press and release to avoid immediately sleeping
again. Leave about two seconds after waking before requesting another sleep.
After an upgrade, quit and reopen Inkline once: an already running old launcher
retains its previous sleep inhibitor until it exits.

## Unicode keyboard and pen mouse

**Either Alt+Space** opens a category-based Unicode keyboard. Tap a character
to type it; drag vertically or use the wheel, arrows and page keys to browse.
Tab changes category. Typing a hexadecimal codepoint followed by Enter inserts
that character. F2 opens Settings; Escape or Done closes the keyboard.
The catalog follows Qt's Unicode version and excludes control characters,
unassigned values, surrogates and noncharacters. Font coverage varies.

The pen sends left-button mouse events when the program has enabled terminal
mouse reporting. Hold Shift to select local text instead. Otherwise pen drags
select text normally. Switching sessions or losing focus releases a held
button in the original terminal.

See [global program shortcuts](global-shortcuts.md) for Ctrl+Opt+Alt bindings and
emergency recovery, and the [PDF manual](Inkline%20Manual.pdf) for a complete guide.

Global launchers use the separate Folio Opt key; on USB keyboards its equivalent
is Windows/Command (Super). Settings → Command shortcuts is page 2 of Settings.
The terminal launcher, custom commands and held emergency recovery all require
Ctrl+Opt+Alt. Ordinary Ctrl+Alt remains available to Emacs. After upgrading, quit
and reopen Inkline to load the new input handling and Settings page.
