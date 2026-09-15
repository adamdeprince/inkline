# Inkline

**A terminal for reMarkable 2**

User guide · version 0.3.6 preview · firmware 3.27

Inkline runs a local shell on your tablet. Type with a Type Folio or an external USB keyboard, work locally, or connect to another computer with SSH or Goblin Mosh. Text and graphics are rendered in grayscale for the e-paper display.

**Open:** press **Ctrl+Alt+T** on either keyboard.

**Return to notebooks:** tap **Quit**, or press **Ctrl+Shift+Q**, then confirm. Type `exit` to close just the current terminal; closing the last one returns to notebooks.

The first screen contains a short guide and a tiny Goblin logo. Run `clear` to erase both. The bottom bar has Escape, history up/down, Quit, and Settings buttons.

**If an app has taken over the screen:** press **Ctrl+Alt+T**. For emergency recovery, hold **Ctrl+Alt+Backspace for two seconds**. Recovery closes all Inkline terminals and restarts Inkline; unsaved work in those sessions is lost.

The normal notebook interface remains the default after reboot. Keep this PDF in My files so you can read it without opening the terminal.

Project and downloads: https://inkline.goblinreactor.com/

Source: https://github.com/adamdeprince/inkline

---

# Keys and terminals

The Type Folio has a separate **Opt** key between Ctrl and Alt, and a **right Alt/Opt** key. They have different jobs.

| Keys | Action |
| --- | --- |
| Separate Opt + 1 through 0 | F1 through F10 |
| Right Alt/Opt + 1 through 9 | Select terminal slot 1 through 9 |
| Right Alt/Opt + Left / Right | Previous / next slot |
| Right Alt/Opt + Tab | Escape |
| Right Alt/Opt + Up / Down | PageUp / PageDown |
| Right Alt/Opt + Backspace | Confirm quitting all terminals |
| Either Alt + Space | Unicode keyboard |
| Right Alt/Opt + C / V | Copy selection / paste |
| Ctrl+Shift+B | Hide or show the bottom bar |

On the **US Type Folio**, right Alt/Opt + **0** types `+`. Right Alt/Opt + the **minus key immediately left of Backspace** types `=`. With the input method off, Shift+6 types a literal caret: `c^2` stays `c^2`. There are no keyboard zoom shortcuts.

USB keyboards keep their ordinary function keys. The separate Opt shortcut uses the Meta modifier when a USB keyboard provides it.

There are at most **nine independent terminal slots**. Selecting an empty slot opens a shell. The indicator counts open terminals, not capacity. With only slots 1 and 9 open, slot 9 shows **Terminal 2 / 2 · slot 9**. Closing a shell frees its slot without renumbering shortcuts.

Each terminal has its own shell, working directory, input composition, graphics, and 500 lines of scrollback. Input-method and display settings apply to all terminals.

---

# Make the display comfortable

Tap the fifth bottom button, **Settings**. With the bar hidden, press **Alt+Space**, then **F2** or tap **Settings** in the Unicode keyboard.

**Caps Lock key:** choose Control (the default) or Caps Lock. A filled choice is active; a dashed outline is keyboard focus. Tab changes focus; arrows and Enter change choices.

**Text size:** pinch with two fingers, or use the minus and plus buttons in Settings. Slow movement changes the font in one-pixel steps, from **6 to 48 pixels**. The selected size is saved at the end of a pinch.

**Text darkness:** 50% is the original rendering. Higher values darken the smooth edges of letters. **Minimum contrast** increases the luminance separation of foreground and background when an application chooses similar colors. It defaults to 35%.

| E-paper updates | Tradeoff |
| --- | --- |
| Fast (default) | Least batching delay; animation waveform; more ghosting |
| Balanced | Normal UI waveform; 32 ms output batching |
| Crisp | Cleaner grayscale; content waveform; slower |
| Mono | Fast black-and-white output; discards image grays |
| Saver | 120 ms output batching; fewer updates during bursts |

The software redraws changed text rows instead of rasterizing the whole terminal for every keystroke. The physical panel still takes time to update. Saver reduces update requests; its battery benefit has not been measured.

Darkness, minimum contrast, and the update profile stay in **RAM** until Inkline closes. Finger lifts and closing Settings do not write these values to flash. Font size, input method, Caps Lock mode, and bottom-bar visibility are saved when changed.

---

# Unicode and Asian input

Press **Alt+Space** to open the Unicode keyboard. Tap a character to type it; the keyboard stays open for further input. Tap **Done** or press Escape to close it.

Characters are divided into letters, marks, numbers, punctuation, math/currency, symbols, spaces, format characters, and private use. Drag the grid vertically to scroll. Use arrows, PageUp/PageDown, Home/End, or a mouse wheel to browse. **Tab** changes categories; Enter inserts the selected character.

You can also type a hexadecimal Unicode value, such as `03bb`, then press Enter to insert λ. Backspace edits that value. Combining marks are shown with a dotted circle as a visual aid; only the mark is inserted. Space and format characters have visible labels in the picker.

The catalog uses the Unicode tables in the installed Qt runtime. It excludes controls, unassigned values, surrogates, and noncharacters. A selectable character may still lack a glyph in the installed fonts. Private-use characters depend on a matching font.

For an input method, press **F2** in the picker or tap **Settings**, then select **Input method**:

| Method | Input |
| --- | --- |
| Off — direct keyboard | Normal US typing |
| Romaji | Japanese hiragana from romanized input |
| Pinyin | Chinese from pinyin |
| Zhuyin | Chinese with the basic Zhuyin map |
| Wubi 86 | Chinese shape codes |
| US-International | Common Latin accents |

Use the candidate strip to select a result. Return to **Off — direct keyboard** in the same menu to resume ordinary US input. This choice is saved and applies to all open terminals.

---

# Touch, pen, clipboard, and graphics

**Two fingers up/down:** scroll the terminal's history. Drag down to reveal older output and up to return toward the prompt. Each terminal retains 500 physical lines in RAM, excluding the live screen.

**Two fingers left/right:** switch terminal slots. A sideways gesture changes one terminal; lift your fingers before switching again. Spread or close your fingers to resize text.

**One finger:** drag to select terminal text. Press right Alt/Opt+C to copy and right Alt/Opt+V to paste. The clipboard is shared by all terminals and holds up to 128 KiB in RAM.

**Pen:** applications that enable terminal mouse reporting receive pen press, movement, and release as left-mouse events. This works with an application's negotiated mouse protocol, including SGR. Otherwise, pen drags select text. Hold **Shift** while using the pen to force local text selection in a mouse-enabled application.

Inkline supports **OSC 52** clipboard requests from terminal programs, including programs over SSH. This is Inkline's RAM clipboard, not the notebook application's clipboard. Applications can read or replace it. Right Alt/Opt+X copies a selection and explains that deletion must happen inside the running editor; OSC 52 cannot delete an editor's text.

Kitty inline and shared-memory graphics remain in RAM. File and temporary-file image transport is disabled. Sixel output is supported explicitly but is not yet advertised automatically. Kitty placeholders and animation playback, and some sixel display modes, remain unfinished.

`clear` and full-screen erase remove visible graphics as well as text. Off-screen graphics are reclaimed under memory pressure. Graphics do not spill to a disk cache; large images and multiple programs still share the tablet's limited RAM.

---

# Launch programs with a shortcut

Register shortcuts from an Inkline shell. Arguments are literal; register a shell explicitly for shell syntax.

Shortcut programs read `~/.bashrc` through Bash, so your shell environment preferences apply.

```sh
~/inkline shortcut register e emacs
~/inkline shortcut register p python3
~/inkline shortcut list
~/inkline shortcut deregister p
```

**Ctrl+Alt+E** opens Emacs in a free terminal slot. `~/inkline run PROGRAM ARG...` launches without a binding. Registering replaces a binding; deregistering leaves running programs alone.

Keys use US physical positions. Quote punctuation; the program must be installed. Use full paths for custom programs.

For a native Qt/e-paper application that draws its own screen:

```sh
~/inkline shortcut register a --epaper /home/root/my-app
```

One native app runs at a time, pausing Inkline and its shells. The launcher supplies Qt e-paper settings and a RAM cache, discards logs, and resumes Inkline when the app exits. Use Qt input without exclusive keyboard grabs.

**Ctrl+Alt+T is permanent.** Registration and deregistration reject T. It stops the native shortcut app and returns to Inkline while preserving open terminals.

**Emergency:** hold **Ctrl+Alt+Backspace for two seconds**. The independent launcher stops the native app and its descendants, closes Inkline's terminals, and starts a fresh Inkline. Save work before testing this. An app running as root can still defeat recovery by grabbing input exclusively, changing services, or exhausting the system; a device restart remains the last resort.

---

# Installation, updates, and the manual

The prebuilt bundle targets **reMarkable 2 on firmware 3.27**, using the stock Qt 6.8 e-paper libraries. Follow the current, checksummed procedure at:

https://inkline.goblinreactor.com/install.html

Installation checks hardware, firmware, fonts, Qt startup, RAM-backed temporary directories, and disabled swap before changing persistent application files. It stores versioned releases under `/home/root/.local/share/inkline` and enables the small keyboard launcher at boot.

Updates preserve already open terminals. **Quit and reopen Inkline to use the new binary.** Previous releases remain on storage for rollback; they take additional space.

This PDF is included in every bundle. During installation, Inkline tries to import it through the tablet's USB web interface when that interface is already enabled and notebooks are running. It records a successful import to avoid duplicate imports of the same manual.

If automatic import is unavailable, connect USB, quit Inkline, and enable **USB web interface** in the notebook interface's storage settings. From your computer's SSH session, run:

```sh
~/inkline manual
```

You can also download the PDF from the project website and import it through the USB browser interface or reMarkable's desktop/mobile app. Automatic import never edits the notebook database directly or interrupts open terminals. Imported manuals are ordinary documents and remain in My files after uninstalling Inkline.

To return to notebooks or remove Inkline through SSH:

```sh
~/inkline stop
~/inkline uninstall
```

---

# Storage and how Inkline works

Keyboard events enter Inkline through Qt. A shared input mapper applies shortcuts and Caps Lock behavior. libghostty-vt encodes terminal keys; a separate pseudo-terminal (PTY) connects each slot to its shell or program. The shell receives `HOME=/home/root`. Interactive Bash reads `~/.bashrc`.

Program output passes through the terminal parser and sixel decoder. libghostty maintains terminal cells, cursor state, alternate screens, scrollback, and kitty image storage. The renderer composes a retained grayscale image and redraws dirty rows; Qt's stock e-paper backend updates the panel.

Each terminal limits core allocations to 64 MiB, kitty image storage to 16 MiB per screen, and sixel retained images to 16 MiB. The renderer and application memory are additional. Unopened slots allocate no terminal. Nine large applications may exceed available RAM.

Temporary graphics, caches, and session state use RAM. Installed software, saved documents, explicit preference changes, and files written by programs still use persistent storage. There is no blanket prevention of application writes.

The optional Python package uses stock Python. To reduce cache writes, add these preferences to the tablet's `~/.bashrc`:

```sh
export PYTHONDONTWRITEBYTECODE=1
export PIP_NO_CACHE_DIR=1
```

Run `source ~/.bashrc` or open a new shell. Use `python3 -m pip install --no-compile PACKAGE` to prevent pip's separate install-time bytecode compilation. Python's isolated mode ignores Python environment variables. Full details are in the project's Python guide.

Inkline is GPL-3.0-or-later software. Each bundle contains source archives and dependency notices. The project README, keyboard guide, and device test record distinguish automated tests from physical checks that still need testing.
