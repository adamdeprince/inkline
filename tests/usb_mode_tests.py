"""Run the actual switch/install shells against isolated configfs and commands."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class UsbModeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='inkline-usb-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bin = self.root / 'bin'; self.bin.mkdir()
        (self.bin / 'python3').symlink_to(sys.executable)
        self.sys = self.root / 'sys'
        self.gadgets = self.sys / 'kernel/config/usb_gadget'
        self.net = self.gadgets / 'g_ether'; self.net.mkdir(parents=True)
        (self.net / 'UDC').write_text('ci_hdrc.0\n')
        self.role = self.sys / 'bus/platform/devices/ci_hdrc.0/role'
        self.role.parent.mkdir(parents=True); self.role.write_text('gadget\n')
        for interface in ('usb0', 'usb1'):
            d = self.sys / 'class/net' / interface; d.mkdir(parents=True); (d / 'uevent').touch()
        self.run = self.root / 'run/inkline-usb'; self.run.mkdir(parents=True)
        text = (ROOT / 'scripts/usb/inkline-usb').read_text()
        for prefix in ('/sys/', '/run/', '/home/root/'):
            text = text.replace(prefix, str(self.root) + prefix)
        text = text.replace('PATH="', 'PATH="' + str(self.bin) + ':', 1)
        self.script = self.root / 'inkline-usb'; self.script.write_text(text)
        self.env = dict(os.environ, USB_TEST_ROOT=str(self.root))
        self.env.pop('SSH_CONNECTION', None)
        fake = self.root / 'fake.py'
        fake.write_text('''import os,sys
from pathlib import Path
r=Path(os.environ['USB_TEST_ROOT']); command=Path(sys.argv[1]).name; args=sys.argv[2:]
with (r/'calls').open('a') as f:f.write(command+' '+' '.join(args)+'\\n')
if command=='id':print('0')
elif command=='mountpoint':sys.exit(1)
elif command=='ip':print('    inet 10.11.99.1/24 scope global usb0')
elif command=='systemctl':
 if args[:2]==['is-active','--quiet'] and args[-1]=='rm-usb-ppp.service':sys.exit(0 if (r/'ppp').exists() else 3)
 if args==['start','inkline-usb-keyboard.service']:
  if (r/'fail-keyboard').exists():sys.exit(1)
  (r/'run/inkline-usb/ready').touch()
 if args==['stop','inkline-usb-keyboard.service']:(r/'run/inkline-usb/ready').unlink(missing_ok=True)
''')
        for name in ('id', 'systemctl', 'mount', 'mountpoint', 'flock', 'ip', 'systemd-run'):
            file = self.bin / name
            file.write_text('#!/bin/sh\nexec python3 -B ' + shlex.quote(str(fake)) + ' ' + name + ' "$@"\n')
            file.chmod(0o700)

    def run_mode(self, action, succeeds=True, env=None):
        result = subprocess.run(['sh', str(self.script), *action], capture_output=True, text=True, env=env or self.env)
        self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)
        return result

    def test_switch_send_and_restore_network(self):
        self.run_mode(['send-keyboard'])
        self.assertEqual((self.net/'UDC').read_text().strip(), '')
        self.assertEqual((self.gadgets/'inkline_keyboard/UDC').read_text().strip(), 'ci_hdrc.0')
        self.run_mode(['network'])
        self.assertEqual((self.net/'UDC').read_text().strip(), 'ci_hdrc.0')
        self.assertEqual((self.gadgets/'inkline_keyboard/UDC').read_text().strip(), '')
        calls = (self.root/'calls').read_text()
        self.assertIn('systemctl start dropbear-usb0.socket dropbear-usb1.socket', calls)
        self.assertIn('systemctl is-active --quiet dropbear-usb1.socket', calls)

    def test_failure_restores_network(self):
        (self.root/'fail-keyboard').touch()
        result = self.run_mode(['send-keyboard'], succeeds=False)
        self.assertIn('attempting to restore', result.stderr)
        self.assertEqual((self.net/'UDC').read_text().strip(), 'ci_hdrc.0')
        self.assertEqual(self.role.read_text().strip(), 'gadget')

    def test_usb_ssh_cannot_disconnect_itself(self):
        env = dict(self.env, SSH_CONNECTION='10.11.99.2 52000 10.11.99.1 22')
        result = self.run_mode(['send-keyboard'], succeeds=False, env=env)
        self.assertIn('Wi-Fi SSH', result.stderr)
        self.assertFalse((self.gadgets/'inkline_keyboard').exists())
        self.assertEqual((self.net/'UDC').read_text().strip(), 'ci_hdrc.0')

    def test_satellite_and_unknown_gadgets_preserved(self):
        (self.root/'ppp').touch()
        self.run_mode(['receive-keyboard'], succeeds=False)
        self.assertEqual(self.role.read_text().strip(), 'gadget')
        (self.root/'ppp').unlink()
        foreign = self.gadgets/'unrelated'; foreign.mkdir(); (foreign/'UDC').write_text('ci_hdrc.0\n')
        self.run_mode(['network'], succeeds=False)
        self.assertEqual((foreign/'UDC').read_text(), 'ci_hdrc.0\n')

    def test_host_and_recovery_validation(self):
        self.run_mode(['receive-keyboard']); self.assertEqual(self.role.read_text().strip(), 'host')
        self.run_mode(['network']); self.assertEqual(self.role.read_text().strip(), 'gadget')
        for seconds in ('-1', '0', '9', '3601', '1;false'):
            self.run_mode(['recover-after', seconds], succeeds=False)
        self.run_mode(['recover-after', '90'])
        self.assertIn('--on-active=90s', (self.root/'calls').read_text())

    def test_installer_preserves_unrelated_paths_and_reinstalls(self):
        home = self.root/'home/root'; bundle=home/'.local/share/inkline/current/usb'; bundle.mkdir(parents=True)
        units=self.root/'etc/systemd/system'; units.mkdir(parents=True)
        unit=units/'inkline-usb-keyboard.service'
        (bundle/unit.name).write_bytes((ROOT/'scripts/usb'/unit.name).read_bytes())
        (bundle/'inkline-usb').write_text('#!/bin/sh\nexit 0\n'); (bundle/'inkline-usb').chmod(0o700)
        install=(ROOT/'scripts/usb/install.sh').read_text().replace('/home/root', str(home)).replace('/etc/systemd/system', str(units))
        script=self.root/'install.sh'; script.write_text(install)
        def run(action):return subprocess.run(['sh',str(script),action],capture_output=True)
        unit.write_text('unrelated\n')
        self.assertNotEqual(run('install').returncode,0); self.assertEqual(unit.read_text(),'unrelated\n')
        unit.unlink(); self.assertEqual(run('check').returncode,0); self.assertFalse(unit.exists())
        self.assertEqual(run('install').returncode,0)
        modified=unit.stat().st_mtime_ns
        self.assertEqual(run('install').returncode,0); self.assertEqual(unit.stat().st_mtime_ns,modified)
        self.assertEqual(os.readlink(home/'.local/bin/inkline-usb'),str(bundle/'inkline-usb'))
        public=home/'.local/bin/keyboard-send'
        self.assertEqual(os.readlink(public),str(bundle/'keyboard-send'))
        public.unlink(); public.write_text('#!/bin/sh\necho existing\n')
        self.assertEqual(run('check').returncode,0); self.assertEqual(run('install').returncode,0)
        self.assertEqual(public.read_text(),'#!/bin/sh\necho existing\n')
        self.assertEqual(run('remove').returncode,0); self.assertTrue(public.exists())

if __name__ == '__main__':unittest.main()
