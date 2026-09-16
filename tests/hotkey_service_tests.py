#!/usr/bin/env python3
"""Exercise real boot-service installation/removal in disposable directories."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "scripts/hotkey-service.sh"
SOURCE = ROOT / "scripts/inkline-hotkey.service"
LEGACY = "/home/root/.local/share/inkline/current/inkline-hotkey.service"


class BootServiceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="inkline-boot-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.units = self.root / "etc/systemd/system"
        self.unit = self.units / "inkline-hotkey.service"
        self.enabled = self.units / "multi-user.target.wants/inkline-hotkey.service"
        self.env = dict(os.environ, INKLINE_SYSTEMD_DIR=str(self.units))

    def run_helper(self, action, succeeds=True):
        result = subprocess.run(["sh", str(HELPER), action], env=self.env,
                                capture_output=True, text=True)
        if succeeds:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)

    def directories(self):
        self.enabled.parent.mkdir(parents=True, exist_ok=True)

    def assert_boot_visible(self):
        # /etc contains the entire unit, so discovering it requires no /home.
        self.assertFalse(self.unit.is_symlink())
        self.assertEqual(self.unit.read_bytes(), SOURCE.read_bytes())
        self.assertEqual(self.unit.stat().st_mode & 0o777, 0o644)
        self.assertEqual(os.readlink(self.enabled), "../inkline-hotkey.service")
        self.assertEqual(self.enabled.resolve(), self.unit)
        self.assertEqual(list(self.units.rglob(".inkline-hotkey.*")), [])

    def test_fresh_install_check_reinstall_remove(self):
        self.run_helper("check")
        self.assertFalse(self.units.exists())
        self.run_helper("install")
        self.assert_boot_visible()
        self.run_helper("check")
        self.run_helper("install")
        self.assert_boot_visible()
        self.run_helper("remove")
        self.run_helper("remove")
        self.assertFalse(os.path.lexists(self.unit))
        self.assertFalse(os.path.lexists(self.enabled))

    def test_migrate_both_legacy_links(self):
        self.directories()
        self.unit.symlink_to(LEGACY)
        self.enabled.symlink_to(LEGACY)
        self.run_helper("check")
        self.assertEqual(os.readlink(self.unit), LEGACY)
        self.run_helper("install")
        self.assert_boot_visible()

    def test_remove_legacy_links(self):
        self.directories()
        self.unit.symlink_to(LEGACY)
        self.enabled.symlink_to(LEGACY)
        self.run_helper("remove")
        self.assertFalse(os.path.lexists(self.unit))
        self.assertFalse(os.path.lexists(self.enabled))

    def test_accept_existing_local_enable_link(self):
        self.run_helper("install")
        self.enabled.unlink()
        self.enabled.symlink_to(self.unit)
        self.run_helper("install")
        self.assert_boot_visible()

    def test_preserve_unrelated_unit(self):
        self.directories()
        self.unit.write_text("[Service]\nExecStart=/bin/true\n")
        original = self.unit.read_bytes()
        for action in ("check", "install", "remove"):
            self.run_helper(action, succeeds=False)
            self.assertEqual(self.unit.read_bytes(), original)
            self.assertFalse(os.path.lexists(self.enabled))

    def test_preserve_unrelated_enable_file_and_link(self):
        self.directories()
        self.unit.symlink_to(LEGACY)
        for kind in ("file", "link"):
            if kind == "file":
                self.enabled.write_text("unrelated")
            else:
                self.enabled.symlink_to("/missing/unrelated.service")
            for action in ("check", "install", "remove"):
                self.run_helper(action, succeeds=False)
                self.assertEqual(os.readlink(self.unit), LEGACY)
                if kind == "file":
                    self.assertEqual(self.enabled.read_text(), "unrelated")
                else:
                    self.assertEqual(os.readlink(self.enabled), "/missing/unrelated.service")
            self.enabled.unlink()

    def test_preserve_unrelated_unit_link(self):
        self.directories()
        target = self.root / "unrelated.service"
        target.write_bytes(SOURCE.read_bytes())
        self.unit.symlink_to(target)
        for action in ("check", "install", "remove"):
            self.run_helper(action, succeeds=False)
            self.assertEqual(os.readlink(self.unit), str(target))
            self.assertEqual(target.read_bytes(), SOURCE.read_bytes())

    def test_executable_waits_for_home(self):
        text = SOURCE.read_text()
        self.assertIn("\nRequiresMountsFor=/home/root/.local/share/inkline\n", text)
        self.assertIn("\nExecStart=/home/root/.local/share/inkline/current/inkline-hotkey\n", text)


if __name__ == "__main__":
    unittest.main()
