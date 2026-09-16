import importlib.util
import io
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

SOURCE = Path(__file__).resolve().parents[1] / 'scripts/usb/keyboard.py'
spec = importlib.util.spec_from_file_location('keyboard', SOURCE)
kb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kb)


class KeyboardTests(unittest.TestCase):
    def test_usb_descriptor_lengths_and_hid_class(self):
        blob = kb.descriptors()
        self.assertEqual(struct.unpack('<IIIII', blob[:20]), (3, len(blob), 3, 3, 3))
        offset = 20
        for speed in range(2):
            interface = blob[offset:offset + 9]
            self.assertEqual(interface, bytes([9, 4, 0, 0, 1, 3, 1, 1, 1]))
            offset += 9
            self.assertEqual(blob[offset:offset + 2], b'\x09\x21')
            self.assertEqual(struct.unpack('<H', blob[offset + 7:offset + 9])[0], len(kb.REPORT))
            offset += 9
            self.assertEqual(blob[offset:offset + 6], b'\x07\x05\x81\x03\x08\x00')
            offset += 7
        self.assertEqual(offset, len(blob))

    def test_golden_usage_codes(self):
        samples = {'a': (0, 4), 'A': (2, 4), 'z': (0, 29), '1': (0, 30),
                   '0': (0, 39), '!': (2, 30), '@': (2, 31), '#': (2, 32),
                   '-': (0, 45), '_': (2, 45), '=': (0, 46), '+': (2, 46),
                   '[': (0, 47), '{': (2, 47), ']': (0, 48), '}': (2, 48),
                   '\\': (0, 49), '|': (2, 49), ';': (0, 51), ':': (2, 51),
                   "'": (0, 52), '"': (2, 52), '`': (0, 53), '~': (2, 53),
                   ',': (0, 54), '<': (2, 54), '.': (0, 55), '>': (2, 55),
                   '/': (0, 56), '?': (2, 56), '\n': (0, 40), '\t': (0, 43),
                   ' ': (0, 44)}
        for char, (modifier, usage) in samples.items():
            with self.subTest(char=char):
                self.assertEqual(kb.text_reports(char), [bytes([modifier, 0, usage, 0, 0, 0, 0, 0])])

    def test_printable_ascii_and_line_endings(self):
        self.assertEqual(len(kb.text_reports(''.join(map(chr, range(32, 127))))), 95)
        self.assertEqual(kb.text_reports('a\r\nb\rc'), kb.text_reports('a\nb\nc'))
        self.assertEqual(kb.text_reports('aa')[0], kb.text_reports('aa')[1])

    def test_rejects_entire_unsupported_input(self):
        for text in ('safe text\x00', 'safe texté', 'safe text\b', '😀'):
            with self.assertRaises(ValueError):
                kb.text_reports(text)

    def test_chords(self):
        self.assertEqual(kb.key_report('ctrl+shift+a'), bytes.fromhex('0300040000000000'))
        self.assertEqual(kb.key_report('cmd+c'), bytes.fromhex('0800060000000000'))
        self.assertEqual(kb.key_report('enter'), bytes.fromhex('0000280000000000'))
        with self.assertRaises(ValueError):
            kb.key_report('fake+a')

    def test_mac_keeps_option_held_for_hex_digits(self):
        kb.validate_text('café 日本語', 'mac')
        self.assertEqual([(r[0], r[2]) for r in kb.character_reports('é', 'mac')],
                         [(4, 0), (4, 39), (4, 0), (4, 39), (4, 0),
                          (4, 8), (4, 0), (4, 38), (4, 0), (0, 0)])
        with self.assertRaises(ValueError):
            kb.validate_text('valid prefix😀', 'mac')

    def test_linux_and_windows_supplementary_unicode(self):
        for profile in ('linux', 'windows'):
            kb.validate_text('日本語 中文 é 😀', profile)
            reports = kb.character_reports('😀', profile)
            self.assertEqual(reports[-2:], [bytes.fromhex('0000280000000000'), bytes(8)])
        linux = kb.character_reports('😀', 'linux')
        self.assertEqual(linux[:2], [bytes.fromhex('0300180000000000'), bytes(8)])
        self.assertEqual([r[2] for r in linux[2:-2:2]], [30, 9, 35, 39, 39])  # 1f600
        windows = kb.character_reports('😀', 'windows')
        self.assertEqual(windows[:4], [bytes.fromhex('4000000000000000'), bytes(8),
                                      bytes.fromhex('0000180000000000'), bytes(8)])
        self.assertEqual([r[2] for r in windows[4:-2:2]], [39, 30, 9, 35, 39, 39])  # 01f600

    def test_validation_precedes_any_usb_output(self):
        for profile, text in [('us', 'prefixé'), ('mac', 'prefix😀'),
                              ('windows', 'prefix\x1b'), ('linux', 'prefix\x00')]:
            with mock.patch.object(kb, 'sender') as sender:
                with self.assertRaises(ValueError):
                    kb.type_text(['--profile', profile, '--text', text, '--start-delay', '0'])
                sender.assert_not_called()
        for profile in kb.PROFILES:
            for bad in ('\x7f', '\x85', '\uffff', '\ud800'):
                with self.assertRaises(ValueError):
                    kb.validate_text('prefix' + bad, profile)

    def test_exclusive_sender_lock(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root / 'ffs').mkdir(); (root / 'ffs/ep1').touch(); (root / 'enabled').touch()
            with mock.patch.object(kb, 'RUN', root), kb.sender():
                with self.assertRaisesRegex(RuntimeError, 'Another Inkline sender'):
                    with kb.sender():
                        self.fail('Second sender acquired the lock')

    def test_large_files_use_bounded_reads_without_size_cutoff(self):
        # A virtual file larger than the former 8 MiB cutoff avoids allocating
        # that much test data. Both passes must keep every read bounded.
        class RepeatedText:
            length = 9 * 1024 * 1024 + 17
            position = 0
            reads = []
            def seekable(self): return True
            def tell(self): return self.position
            def seek(self, position): self.position = position
            def read(self, size=-1):
                self.reads.append(size)
                if not 0 < size <= kb.TEXT_CHUNK_CHARS:
                    raise AssertionError('Unbounded read')
                size = min(size, self.length - self.position)
                self.position += size
                return 'a' * size
        data = RepeatedText()
        self.assertEqual(kb.preflight_text(data, 'us'), data.length)
        self.assertEqual(data.tell(), 0)
        self.assertEqual(sum(map(len, kb.text_chunks(data, 'us'))), data.length)
        self.assertGreater(len(data.reads), 100)

    def test_file_preflight_includes_tail_and_normalizes_utf8(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'text.txt'
            path.write_bytes(b'a' * (kb.TEXT_CHUNK_CHARS + 1) + b'\x00')
            with mock.patch.object(kb, 'sender') as sender:
                with self.assertRaisesRegex(ValueError, 'position 16386'):
                    kb.type_text(['--file', str(path), '--start-delay', '0'])
                sender.assert_not_called()
            path.write_bytes(b'\xef\xbb\xbf' + 'a\r\n日本語\rb\n'.encode())
            output = io.StringIO()
            with mock.patch.object(kb, 'TEXT_CHUNK_CHARS', 1), mock.patch.object(kb.sys, 'stdout', output):
                kb.type_text(['--file', str(path), '--profile', 'linux', '--dry-run'])
            self.assertIn('"characters": 8', output.getvalue())

    def test_nonseekable_pipe_sends_valid_chunks_then_releases_on_error(self):
        class Pipe(io.BytesIO):
            def seekable(self): return False
            def seek(self, *args): raise AssertionError('Cannot rewind a pipe')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root / 'ffs').mkdir(); (root / 'ffs/ep1').touch(); (root / 'enabled').touch()
            pipe = Pipe(b'aa\x00')
            with mock.patch.object(kb, 'RUN', root), mock.patch.object(kb, 'TEXT_CHUNK_CHARS', 2), \
                 mock.patch.object(kb.sys, 'stdin', type('Stdin', (), {'buffer': pipe})()), \
                 mock.patch.object(kb.time, 'sleep'):
                with self.assertRaises(ValueError):
                    kb.type_text(['--stdin', '--start-delay', '0'])
            self.assertEqual((root / 'ffs/ep1').read_bytes(),
                             bytes.fromhex('0000040000000000') + bytes(8) +
                             bytes.fromhex('0000040000000000') + bytes(16))
            self.assertFalse(pipe.closed)

    def test_nonblocking_usb_retries_and_releases_on_error(self):
        with mock.patch.object(kb.os, 'write', side_effect=[BlockingIOError(), 8]) as write, \
             mock.patch.object(kb.select, 'select'):
            kb.write_report(123, kb.RELEASE)
            self.assertEqual(write.call_count, 2)
        with mock.patch.object(kb.os, 'write', return_value=4):
            with self.assertRaisesRegex(RuntimeError, 'Short USB'):
                kb.write_report(123, kb.RELEASE)

    @unittest.skipUnless(sys.platform == 'linux' and hasattr(kb.os, 'pidfd_open'), 'Linux pidfd check')
    def test_stop_command_targets_live_sender_and_releases_keys(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root / 'ffs').mkdir(); (root / 'ffs/ep1').touch(); (root / 'enabled').touch()
            code = """import importlib.util,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('kb',sys.argv[1]);kb=importlib.util.module_from_spec(spec);spec.loader.exec_module(kb)
kb.RUN=Path(sys.argv[2]);sys.argv=['keyboard.py','type','--text','a'*1000,'--start-delay','0']
sys.exit(kb.main())
"""
            child = subprocess.Popen([sys.executable, '-B', '-c', code, str(SOURCE), str(root)], stderr=subprocess.DEVNULL)
            try:
                limit = time.monotonic() + 5
                while (root / 'ffs/ep1').stat().st_size == 0 and time.monotonic() < limit:
                    time.sleep(.02)
                self.assertGreater((root / 'ffs/ep1').stat().st_size, 0)
                with mock.patch.object(kb, 'RUN', root):
                    kb.stop_sender()
                self.assertEqual(child.wait(timeout=3), 130)
                self.assertEqual((root / 'ffs/ep1').read_bytes()[-8:], bytes(8))
            finally:
                if child.poll() is None: child.kill(); child.wait()

    def test_control_requests(self):
        request = lambda kind, req, val, n: struct.pack('<BBHHH', kind, req, val, 0, n)
        self.assertEqual(kb.control_reply(request(0x81, 6, 0x2200, 255), 1, 0), kb.REPORT)
        self.assertEqual(kb.control_reply(request(0x81, 6, 0x2200, 3), 1, 0), kb.REPORT[:3])
        self.assertEqual(kb.control_reply(request(0xa1, 3, 0, 1), 1, 0), b'\x01')
        self.assertIsNone(kb.control_reply(request(0x81, 6, 0x9900, 1), 1, 0))

    def test_bad_speeds_and_dry_run_do_not_need_usb(self):
        for options in (['--cps', '0'], ['--cps', '-1'], ['--cps', 'nan'],
                        ['--cps', '101'], ['--delay-ms', '0'], ['--delay-ms', '-5'],
                        ['--start-delay', 'nan'], ['--hold-ms', '100']):
            result = subprocess.run([sys.executable, '-B', str(SOURCE), 'type', '--text', 'a',
                                     '--dry-run', *options], capture_output=True)
            self.assertNotEqual(result.returncode, 0, options)
        result = subprocess.run([sys.executable, '-B', str(SOURCE), 'type', '--text', 'aa',
                                 '--dry-run', '--delay-ms', '100'], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'"cps": 10.0', result.stdout)

    def test_repeated_keys_have_releases_and_requested_pacing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'ffs').mkdir()
            (root / 'ffs/ep1').touch()
            (root / 'enabled').touch()
            start = time.monotonic()
            with mock.patch.object(kb, 'RUN', root):
                kb.type_text(['--text', 'aa', '--cps', '20', '--start-delay', '0'])
            self.assertGreaterEqual(time.monotonic() - start, .09)
            self.assertEqual((root / 'ffs/ep1').read_bytes(),
                             bytes.fromhex('0000040000000000') + bytes(8) +
                             bytes.fromhex('0000040000000000') + bytes(16))

    def test_cancellation_releases_held_key(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'ffs').mkdir()
            (root / 'ffs/ep1').touch()
            (root / 'enabled').touch()
            write = kb.os.write
            calls = 0
            def interrupt_after_press(fd, data):
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise KeyboardInterrupt
                return write(fd, data)
            with mock.patch.object(kb, 'RUN', root), mock.patch.object(kb.os, 'write', interrupt_after_press):
                with self.assertRaises(KeyboardInterrupt):
                    kb.type_text(['--text', 'abc', '--start-delay', '0'])
            self.assertEqual((root / 'ffs/ep1').read_bytes(), bytes.fromhex('0000040000000000') + bytes(8))


if __name__ == '__main__':
    unittest.main()
