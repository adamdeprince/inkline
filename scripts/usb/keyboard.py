#!/usr/bin/env python3
"""Inkline USB keyboard over FunctionFS; bounded host-specific Unicode input."""
import argparse
import contextlib
import errno
import fcntl
import io
import json
import math
import os
from pathlib import Path
import select
import signal
import struct
import sys
import time

RUN = Path('/run/inkline-usb')
TEXT_CHUNK_CHARS = 16384  # Read buffer, not a limit on input size.
MAX_STREAM_BYTES = 8192
PROFILES = ('us', 'mac', 'linux', 'windows')
RELEASE = bytes(8)
REPORT = bytes.fromhex(
    '05 01 09 06 a1 01 05 07 19 e0 29 e7 15 00 25 01 75 01 95 08 81 02 '
    '95 01 75 08 81 01 95 05 75 01 05 08 19 01 29 05 91 02 95 01 75 03 '
    '91 01 95 06 75 08 15 00 25 65 05 07 19 00 29 65 81 00 c0')
HID = struct.pack('<BBHBBBH', 9, 0x21, 0x111, 0, 1, 0x22, len(REPORT))


def descriptors():
    interface = bytes([9, 4, 0, 0, 1, 3, 1, 1, 1])
    # Full-speed 1 ms / high-speed 1 ms interrupt interval.
    fs = interface + HID + struct.pack('<BBBBHB', 7, 5, 0x81, 3, 8, 1)
    hs = interface + HID + struct.pack('<BBBBHB', 7, 5, 0x81, 3, 8, 4)
    return struct.pack('<IIIII', 3, 20 + len(fs) + len(hs), 3, 3, 3) + fs + hs


def strings():
    body = struct.pack('<H', 0x409) + b'Inkline Keyboard\0'
    return struct.pack('<IIII', 2, 16 + len(body), 1, 1) + body


def report(mod, key):
    return bytes([mod, 0, key, 0, 0, 0, 0, 0])


def text_reports(text):
    result = []
    plain = "1234567890-=[]\\;'`,./"
    shifted = '!@#$%^&*()_+{}|:"~<>?'
    codes = list(range(30, 40)) + [45, 46, 47, 48, 49, 51, 52, 53, 54, 55, 56]
    mapping = {char: (0, code) for char, code in zip(plain, codes)}
    mapping.update({char: (2, code) for char, code in zip(shifted, codes)})
    mapping.update({' ': (0, 44), '\n': (0, 40), '\t': (0, 43)})
    text = text.replace('\r\n', '\n').replace('\r', '\n')
    for pos, char in enumerate(text):
        if 'a' <= char <= 'z':
            mod, code = 0, ord(char) - ord('a') + 4
        elif 'A' <= char <= 'Z':
            mod, code = 2, ord(char) - ord('A') + 4
        elif char in mapping:
            mod, code = mapping[char]
        else:
            raise ValueError(f'Unsupported character {char!r} at position {pos + 1}; use US-layout ASCII text.')
        result.append(report(mod, code))
    return result


def key_report(chord):
    mods = {'ctrl': 1, 'control': 1, 'shift': 2, 'alt': 4,
            'super': 8, 'cmd': 8, 'meta': 8}
    keys = {'enter': 40, 'esc': 41, 'escape': 41, 'backspace': 42, 'tab': 43,
            'space': 44, 'capslock': 57, 'insert': 73, 'home': 74, 'pageup': 75,
            'delete': 76, 'end': 77, 'pagedown': 78, 'right': 79, 'left': 80,
            'down': 81, 'up': 82}
    keys.update({f'f{i}': 57 + i for i in range(1, 13)})
    parts = chord.lower().split('+')
    mod = 0
    for name in parts[:-1]:
        if name not in mods:
            raise ValueError(f'Unknown modifier {name!r}')
        mod |= mods[name]
    name = parts[-1]
    if name in keys:
        return report(mod, keys[name])
    if len(name) == 1:
        r = text_reports(name)[0]
        return report(mod | r[0], r[2])
    raise ValueError(f'Unknown key {name!r}')


@contextlib.contextmanager
def deadline(seconds):
    def expired(signum, frame):
        raise TimeoutError('USB host did not consume a keyboard report; reconnect the cable or select keyboard mode again.')
    previous = signal.signal(signal.SIGALRM, expired)
    signal.setitimer(signal.ITIMER_REAL, seconds)
    try:
        yield
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, previous)


def control_reply(request, protocol, idle):
    kind, req, value, index, length = struct.unpack('<BBHHH', request)
    data = None
    if kind == 0x81 and req == 6:
        data = {0x22: REPORT, 0x21: HID}.get(value >> 8)
    elif kind == 0xa1:
        data = {1: RELEASE if value >> 8 == 1 else bytes(1),
                2: bytes([idle]), 3: bytes([protocol])}.get(req)
    elif kind == 0x21 and req in (9, 10, 11):
        data = b''
    return None if data is None else data[:length]


def daemon():
    RUN.mkdir(mode=0o700, exist_ok=True)
    ep0 = os.open(RUN / 'ffs/ep0', os.O_RDWR)
    protocol, idle = 1, 0
    os.write(ep0, descriptors())
    os.write(ep0, strings())
    (RUN / 'ready').touch()
    print('FunctionFS descriptors ready', flush=True)
    try:
        while True:
            select.select([ep0], [], [])
            # Read one event so SETUP replies cannot consume a later event.
            event = os.read(ep0, 12)
            if len(event) != 12:
                raise RuntimeError('Short FunctionFS event')
            typ = event[8]
            if typ == 2:
                (RUN / 'enabled').touch()
                print('Host configured USB keyboard', flush=True)
            elif typ in (1, 3):
                (RUN / 'enabled').unlink(missing_ok=True)
            elif typ == 4:
                kind, req, value, index, length = struct.unpack('<BBHHH', event[:8])
                data = control_reply(event[:8], protocol, idle)
                try:
                    if data is None:
                        # An operation in the opposite direction stalls EP0.
                        if kind & 0x80:
                            os.read(ep0, 0)
                        else:
                            os.write(ep0, b'')
                    elif kind & 0x80:
                        os.write(ep0, data)
                    else:
                        os.read(ep0, length)
                        if req == 10:
                            idle = value >> 8
                        elif req == 11:
                            protocol = value & 1
                except OSError as exc:
                    if exc.errno not in (errno.EL2HLT, errno.EIDRM, errno.ESHUTDOWN):
                        raise
    finally:
        os.close(ep0)
        (RUN / 'ready').unlink(missing_ok=True)
        (RUN / 'enabled').unlink(missing_ok=True)


def normalize(text):
    return text.replace('\r\n', '\n').replace('\r', '\n')


def validate_text(text, profile, offset=0):
    if profile not in PROFILES:
        raise ValueError('Unknown receiving-computer profile')
    for pos, char in enumerate(text):
        code = ord(char)
        if char in '\n\t' or 32 <= code <= 126:
            continue
        if code < 32 or 127 <= code <= 159 or 0xd800 <= code <= 0xdfff or 0xfdd0 <= code <= 0xfdef or code & 0xffff in (0xfffe, 0xffff):
            raise ValueError(f'Unsupported control/noncharacter U+{code:04X} at position {offset + pos + 1}')
        if profile == 'us':
            raise ValueError(f'U+{code:04X} needs a Unicode host profile (mac, linux or windows)')
        if profile == 'mac' and code > 0xffff:
            raise ValueError(f'U+{code:04X} is outside the Mac Unicode Hex Input profile (U+FFFF maximum)')


def text_chunks(data, profile):
    """Validate a bounded read buffer; input length is unrestricted."""
    offset = 0
    while chunk := data.read(TEXT_CHUNK_CHARS):
        validate_text(chunk, profile, offset)
        yield chunk
        offset += len(chunk)


def preflight_text(data, profile):
    """Check seekable input before opening USB, without retaining the text.

    Pipes cannot be rewound: validate those as they arrive, without a spool file.
    Revalidate during sending too, since a source file could have been edited.
    """
    if not data.seekable():
        return None
    position = data.tell()
    count = sum(len(chunk) for chunk in text_chunks(data, profile))
    data.seek(position)
    return count


@contextlib.contextmanager
def text_source(args):
    if args.text is not None:
        with io.StringIO(normalize(args.text)) as data:
            yield data
    elif args.file:
        # Universal newline handling carries CRLF across buffer boundaries.
        # utf-8-sig also accepts plain UTF-8 and strips an optional initial BOM.
        with args.file.open('r', encoding='utf-8-sig', newline=None) as data:
            yield data
    else:
        data = io.TextIOWrapper(sys.stdin.buffer, encoding='utf-8-sig', newline=None)
        try:
            yield data
        finally:
            data.detach()  # The caller owns stdin.


def character_reports(char, profile):
    """One character, including key-up reports. No OS-independent Unicode HID."""
    if char in '\n\t' or 32 <= ord(char) <= 126:
        return [text_reports(char)[0], RELEASE]
    digits = f'{ord(char):04x}'
    if profile == 'mac':
        result = [report(4, 0)]  # Hold Option through all four hex digits.
        for key in text_reports(digits):
            result.extend((report(4, key[2]), report(4, 0)))
        return result + [RELEASE]
    if profile == 'windows':
        # WinCompose: default Compose key (Right Alt), u, codepoint, Enter.
        # Six digits avoid the default u+a/u+e compose sequences for BMP text.
        digits = f'{ord(char):06x}'
        result = [report(64, 0), RELEASE, key_report('u'), RELEASE]
    else:
        result = [key_report('ctrl+shift+u'), RELEASE]
    for key in text_reports(digits):
        result.extend((key, RELEASE))
    return result + [key_report('enter'), RELEASE]


def write_report(fd, data, timeout=5):
    limit = time.monotonic() + timeout
    while True:
        remaining = limit - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('USB host is not accepting keystrokes; reconnect or restore network mode')
        try:
            with deadline(remaining):
                if os.write(fd, data) != len(data):
                    raise RuntimeError('Short USB keyboard report')
            return
        except BlockingIOError:
            select.select([], [fd], [], min(remaining, .05))


def process_start(pid):
    try:
        return Path(f'/proc/{pid}/stat').read_text().rsplit(') ', 1)[1].split()[19]
    except (OSError, IndexError):
        return None


def stop_sender():
    """Stop only the process holding our lock, using a pidfd against PID reuse."""
    if not (RUN / 'type.lock').exists():
        return
    with (RUN / 'type.lock').open('r+') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return  # No sender owns the endpoint.
        except BlockingIOError:
            pass
        identity = json.loads(lock.read(256))
        pid = identity.get('pid', 0)
        if not isinstance(pid, int) or pid < 2 or not identity.get('start'):
            raise RuntimeError('Cannot identify the active USB sender safely')
        if not hasattr(os, 'pidfd_open') or not hasattr(signal, 'pidfd_send_signal'):
            raise RuntimeError('Stopping another sender requires the tablet Linux runtime')
        try:
            descriptor = os.pidfd_open(pid)
        except ProcessLookupError:
            return
        try:
            if process_start(pid) != identity['start']:
                raise RuntimeError('USB sender identity changed; no signal sent')
            signal.pidfd_send_signal(descriptor, signal.SIGTERM)
            if not select.select([descriptor], [], [], 2)[0]:
                signal.pidfd_send_signal(descriptor, signal.SIGKILL)
                select.select([descriptor], [], [], .5)
        except ProcessLookupError:
            pass
        finally:
            os.close(descriptor)


@contextlib.contextmanager
def sender():
    if not (RUN / 'enabled').exists():
        raise RuntimeError('USB keyboard is not connected. Run inkline-usb send-keyboard and connect a computer.')
    with (RUN / 'type.lock').open('a+') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('Another Inkline sender is typing. Stop it first.') from None
        lock.seek(0); lock.truncate()
        json.dump({'pid': os.getpid(), 'start': process_start(os.getpid())}, lock)
        lock.flush()  # Lock identity only; /run is RAM and contains no typed text.
        fd = os.open(RUN / 'ffs/ep1', os.O_WRONLY | os.O_NONBLOCK)
        try:
            yield fd
        finally:
            try:
                write_report(fd, RELEASE, 1)
            except (OSError, TimeoutError, RuntimeError):
                pass
            os.close(fd)


def send_characters(fd, sequences, cps, hold_ms):
    for sequence in sequences:
        start = time.monotonic()
        for value in sequence:
            write_report(fd, value)
            time.sleep(hold_ms / 1000)
        time.sleep(max(0, 1 / cps - (time.monotonic() - start)))


def stream_text(args):
    # GUI protocol: one bounded JSON request -> one acknowledgement. No history.
    with sender() as fd:
        print('{"ready":true}', flush=True)
        while True:
            line = sys.stdin.buffer.readline(MAX_STREAM_BYTES + 1)
            if not line:
                return
            if len(line) > MAX_STREAM_BYTES or not line.endswith(b'\n'):
                raise ValueError('Typewriter request exceeds its RAM limit')
            request = json.loads(line)
            if not isinstance(request, dict) or set(request) not in ({'text'}, {'key'}):
                raise ValueError('Expected a text or key request')
            value = next(iter(request.values()))
            if not isinstance(value, str):
                raise ValueError('Text and key values must be strings')
            if 'text' in request:
                text = normalize(value)
                validate_text(text, args.profile)
                sequences = (character_reports(c, args.profile) for c in text)
            else:
                sequences = [[key_report(value), RELEASE]]
            send_characters(fd, sequences, args.cps, args.hold_ms)
            print('{"ok":true}', flush=True)


def type_text(argv):
    parser = argparse.ArgumentParser(description='Type UTF-8 text over USB. The receiving computer must use the selected input profile.')
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--text')
    source.add_argument('--file', type=Path)
    source.add_argument('--stdin', action='store_true')
    source.add_argument('--key', action='append', help='Named key/chord, e.g. ctrl+c or enter; may repeat')
    source.add_argument('--stream', action='store_true', help='Bounded JSON protocol for Inkline Typewriter')
    source.add_argument('--stop', action='store_true', help='Stop the active USB sender and release its keys (tablet only)')
    parser.add_argument('--profile', choices=PROFILES, default='us',
                        help='us: ASCII; mac: Unicode Hex Input; linux: Ctrl+Shift+U; windows: WinCompose with Right Alt and Unicode input enabled')
    speed = parser.add_mutually_exclusive_group()
    speed.add_argument('--cps', type=float, help='Maximum characters/second (default 20); Unicode takes more reports')
    speed.add_argument('--delay-ms', type=float, help='Minimum milliseconds between character starts')
    parser.add_argument('--start-delay', type=float, default=3, help='Seconds to focus destination (default 3)')
    parser.add_argument('--hold-ms', type=float, default=8, help='Report duration (default 8 ms)')
    parser.add_argument('--dry-run', action='store_true', help='Validate/show reports without USB (may print sensitive text as codes)')
    args = parser.parse_args(argv)
    if args.stop:
        return stop_sender()
    args.cps = args.cps if args.cps is not None else (1000 / args.delay_ms if args.delay_ms else 20)
    if args.delay_ms is not None and (not math.isfinite(args.delay_ms) or args.delay_ms <= 0):
        parser.error('--delay-ms must be positive and finite')
    if not math.isfinite(args.cps) or not 0 < args.cps <= 100:
        parser.error('--cps must be greater than 0 and at most 100')
    if not math.isfinite(args.start_delay) or not 0 <= args.start_delay <= 3600:
        parser.error('--start-delay must be 0..3600')
    if not math.isfinite(args.hold_ms) or not 1 <= args.hold_ms < 500 / args.cps:
        parser.error('--hold-ms must be at least 1 and less than half the character interval')
    if args.stream:
        if args.dry_run:
            parser.error('--stream and --dry-run are incompatible')
        return stream_text(args)
    with contextlib.ExitStack() as stack:
        if args.key:
            keys = [key_report(k) for k in args.key]
            count = len(keys)
            sequences = ([k, RELEASE] for k in keys)
        else:
            data = stack.enter_context(text_source(args))
            count = preflight_text(data, args.profile)
            sequences = (character_reports(c, args.profile)
                         for chunk in text_chunks(data, args.profile) for c in chunk)
        if args.dry_run:
            # Unknown-length streams report null; output remains incremental.
            print(json.dumps({'characters': count, 'cps': args.cps, 'profile': args.profile}))
            for sequence in sequences:
                print(json.dumps([r.hex() for r in sequence]))
            return
        with sender() as fd:
            amount = f'{count} characters' if count is not None else 'streamed text (length unknown)'
            print(f'Typing {amount} in {args.start_delay:g} seconds. Ctrl+C stops.', file=sys.stderr)
            time.sleep(args.start_delay)
            send_characters(fd, sequences, args.cps, args.hold_ms)
    print('Typing complete.', file=sys.stderr)


def main():
    # Raising on TERM allows the sender to release keys in its finally block.
    def terminate(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, terminate)
    try:
        if len(sys.argv) > 1 and sys.argv[1] == 'daemon':
            daemon()
        elif len(sys.argv) > 1 and sys.argv[1] == 'type':
            type_text(sys.argv[2:])
        else:
            raise ValueError('Expected daemon or type')
    except KeyboardInterrupt:
        return 130
    except (ValueError, RuntimeError, OSError, TimeoutError) as exc:
        print(f'keyboard-send: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
