# USB implementation

Adapted with the owner's permission from Adam de Prince's Inkline Outpost
`/home/root/usb-modes` on the tablet, snapshot 2026-09-16. The FunctionFS
descriptors and control handler originate there. The original files remain
untouched. Inkline does not bundle Outpost's modem, PPP, or serial utilities.

`inkline-usb` owns a separate `inkline_keyboard` gadget and service. It shares
Outpost's mode lock so the two switchers cannot change the controller at once.
Mode changes reject active satellite PPP and unknown bound gadgets. Installation
does not switch USB modes or enable Wi-Fi access. Boot still creates the stock
Ethernet gadget. Runtime state and locks are on `/run`; typed text is never
logged. All bundled Python entry points use `-B`.

The Qt UI and command-line sender use the same encoder. `--stream` accepts
bounded JSON lines on stdin (one `text` or `key` member), acknowledges each
completed request on stdout, and never interprets text as shell commands.
Only one sender holds the endpoint lock. EOF, cancellation, errors and exit
attempt a release-all report. The UI terminates this subprocess when hidden,
paused or closed; no keystrokes are automatically replayed after an error.

Files have no fixed size limit. A small read buffer is validated and encoded
incrementally. Seekable input gets a full preflight and rewind before opening
USB; pipes are checked as they arrive, without spooling. The second pass also
validates text, but callers should keep source files unchanged during a send.
Universal newline handling spans buffers; an optional UTF-8 BOM is removed.

User instructions are maintained in `docs/manual/*.tex`, compiled into the
single nine-language `docs/Inkline Manual.pdf` and imported into My files by
the installer when the stock USB web interface is available.

Run `python3 -B tests/usb_keyboard_tests.py -v`,
`python3 -B tests/usb_mode_tests.py -v`, and the `usb_typewriter_ui` CTest.
Use `/tmp` for tablet test files. Physical HID tests must exclusively capture
the matching keyboard on the receiving host before transmitting test text.
