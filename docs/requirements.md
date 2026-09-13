# Inkline requirements

## Confirmed scope

Build a terminal for reMarkable 2. Support both the reMarkable Type Folio and
external USB keyboards. Aim for broad kitty compatibility wherever it is useful
on this device, and add sixel graphics.

The device now runs **3.27.3.0**, upgraded from 3.9.3.1986. Use that version to
validate the build and display integration.

These are implementation requirements. Current implementation status is in the README.

## Terminal engine

Use **[libghostty-vt](https://github.com/ghostty-org/ghostty)**. It provides an
embeddable C API for terminal state, input
encoding and decoded kitty images. The local libkitty-vt project is still an
incomplete extraction with no public C API, so it is not the initial engine.

The revision is pinned in CMake. A small patch selects Linux's 64-bit `llseek`
syscall on ARM32 when building without libc, fixing the pinned engine's
cross-compilation failure. The project wrapper configures image transports and
memory budgets through the public API.

## Graphics and storage

| Feature | Requirement or decision |
| --- | --- |
| Kitty inline image data (`t=d`) | Support, including chunked data over SSH |
| Kitty shared-memory image data (`t=s`) | Support for local clients |
| Kitty existing-file images (`t=f`) | Off by default; explicit option allows reading existing images |
| Kitty temporary-file images (`t=t`) | Disabled by default |
| Automatic image-cache writes to flash | Exclude; keep bounded storage in memory |
| Kitty file-transfer protocol | Separate from graphics; user is reconsidering whether to include it |
| Sixel | Initial decoding, framing, placement, scrolling and rendering work; DEC modes and full erase semantics remain |

The [kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/)
defines four image transmission media. File image mode reads an existing local
file. Temporary-file mode reads and deletes a file created by the client. The
terminal does not inherently create those source files. Client-created temporary
files and a terminal's own disk cache are separate sources of potential flash
writes. Shared-memory transport is local; remote clients use inline data.

Kitty also defines a [file-transfer protocol](https://sw.kovidgoyal.net/kitty/file-transfer-protocol/)
for copying files over a terminal session. That could be useful for deliberate
copies of scripts, fonts or documents. Making it opt-in is a proposal, not a
confirmed decision. Do not treat graphics file modes and general file transfer
as a single feature.

Use a common image renderer for kitty and sixel. Preserve protocol colors
internally and convert the composed image to grayscale at the display boundary.
Bound decoded pixels, compressed input, animation frames and total image memory.
Handle limits without silently spilling to flash. Report unsupported kitty
transports accurately so clients can fall back to inline data.

My graphics-heavy programs run over a modified mosh that efficiently
transfers kitty images. The local `/Users/adam/dev/mosh` checkout identifies it
as Goblin Mosh. Its image-output helpers use inline kitty chunks. Keep those
streams compatible and audit the complete client/terminal path for automatic
image-cache writes, temporary images and repeated logging. Deliberate file
copies are separate from that requirement.

The mosh checkout documents general remote sixel integration as unfinished.
Our decoder was adapted from its bounded in-memory codec; direct terminal sixel
tests and later mosh integration tests must be kept distinct.

Sixel support includes stream framing across PTY reads, palette definitions,
repeat runs, raster attributes, transparency, placement, scrolling and erase
behavior. Advertise sixel only when the complete path works. Verify malformed
input and oversized images as well as valid images.

The stream adapter currently supplies decoded bitmaps to a synchronous consumer
at the original point in the VT stream. It does not inject synthetic kitty image
transfers, which would disturb a real kitty transfer between its chunks. The
consumer still needs image placement and erase/scroll handling; DEC sixel display
modes and private/shared palette selection also remain to be integrated. Use the
[XTerm control-sequence reference](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)
when implementing those modes and their queries.

## Keyboard input

- Discover keyboards by evdev capabilities, without fixed `/dev/input/eventN`
  numbers or a Type Folio-only name filter.
- Support both keyboards independently and when attached together.
- Handle attachment, removal, held modifiers and repeat without stuck keys.
- Support keyboard layouts and accessible Escape, Control, Alt and function
  keys on the Type Folio.
- Preserve ordinary terminal input and support the negotiated
  [kitty keyboard protocol](https://sw.kovidgoyal.net/kitty/keyboard-protocol/).
- Verify USB host operation on the actual device and adapter.

## E-paper behavior

- Landscape operation with Type Folio; configurable orientation for USB input.
- Refresh changed regions and coalesce rapid updates.
- Keep terminal state accurate when display updates are throttled.
- Provide manual full refresh and tune ghosting cleanup on the device.
- Keep idle redraws low; cursor blinking and animation presentation need
  e-paper defaults.
- Restore normal tablet display/input ownership when the terminal exits.

## Implementation sequence

1. Collect device/input metadata for confirmed firmware 3.27.3.0.
2. Verify the pinned terminal engine's ARM32 build and device ABI.
3. Build a display/input prototype using the matching official SDK.
4. Connect a PTY, terminal state and renderer; verify shell and TUI behavior.
5. Connect kitty image rendering and enforce the agreed transport policy.
6. Add sixel through the same image rendering path.
7. Exercise both keyboards, hotplug, rotation, scrolling and image memory limits
   on the tablet.

The manufacturer's [Qt/e-paper guide](https://developer.remarkable.com/documentation/qt_epaper)
is the first display-integration reference for current software. If a community
display manager is needed, verify its compatibility with the actual firmware.
Four host test suites and the ARM32 cross-build now pass. The first device probe
confirmed the Type Folio and RAM-backed temporary/shared-memory filesystems with
no swap. Device ABI, display and keyboard interaction tests await restored SSH
access; see [device findings](device.md).
