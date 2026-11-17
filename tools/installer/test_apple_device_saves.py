#!/usr/bin/env python3
"""apple_device.py: what the setup log may show, the Mac game check, the team identifier, and the save copies'
files and folders. Needs no Xcode or device."""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import apple_device  # noqa: E402


class Log(unittest.TestCase):
    def test_team_and_identity_are_masked(self):
        apple_device.private("ABCDE12345", "0123456789ABCDEF0123456789ABCDEF01234567")
        lines = []
        with mock.patch.object(apple_device.subprocess, "run",
                               return_value=SimpleNamespace(returncode=1, stdout=b"Apple Development (ABCDE12345) failed")):
            with self.assertRaises(apple_device.Error) as e:
                apple_device.run(["codesign", "--sign", "0123456789ABCDEF0123456789ABCDEF01234567", "app"], None,
                                 lines.append, "signing")
        text = "\n".join(lines) + str(e.exception)
        self.assertNotIn("ABCDE12345", text)
        self.assertNotIn("0123456789ABCDEF", text)
        self.assertIn("…345", text)
        self.assertIn("…567", text)


class MacGame(unittest.TestCase):
    def test_paths_compare_exactly(self):
        exe = "/Users/p/Wind Waker HD (1)/data/bin/WindWakerHD"
        ps = SimpleNamespace(stdout="/usr/bin/login\n%s --setup\n" % exe)
        with mock.patch.object(apple_device.subprocess, "run", return_value=ps):
            self.assertTrue(apple_device.process_running({exe}))
            self.assertFalse(apple_device.process_running({"/Users/p/Wind Waker HD/data/bin/WindWakerHD"}))
            self.assertFalse(apple_device.process_running({exe[:-1]}))


class Settings(unittest.TestCase):
    def test_team_identifier_checked(self):
        with tempfile.TemporaryDirectory() as tmp:
            ctx = SimpleNamespace(data_dir=tmp)
            self.assertEqual(apple_device.settings(ctx, team="ABCDE12345")["team"], "ABCDE12345")
            for bad in ("abcde12345", "ABCDE1234", "ABCDE12345 --x", "ABCDE-2345"):
                with self.assertRaises(apple_device.Error):
                    apple_device.settings(ctx, team=bad)


class Saves(unittest.TestCase):
    def test_only_the_games_save_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            user = Path(tmp) / "user"
            user.mkdir()
            for name in ("cking.sav", "cking_pic0_1.sav", "cking 2.sav", "cking (1).sav", "notes.txt", ".cking.sav.icloud"):
                (user / name).write_bytes(b"x")
            self.assertEqual([p.name for p in apple_device._sav_files(tmp)], ["cking.sav", "cking_pic0_1.sav"])

    def test_new_folder_never_collides(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp) / "backups" / "org.example.tv-20261116-100000"
            a, b, c = (apple_device._new_folder(base) for _ in range(3))
            self.assertEqual([a.name, b.name, c.name], [base.name, base.name + "-2", base.name + "-3"])

    def test_drive_copy_needs_the_container_on_this_mac(self):
        with tempfile.TemporaryDirectory() as home, tempfile.TemporaryDirectory() as tmp:
            backup = Path(tmp)
            (backup / "user").mkdir()
            device_folder = "/private/var/mobile/Library/Mobile Documents/iCloud~com~example~wwhd/Documents/Wind Waker HD"
            (backup / "user" / "save-folder.json").write_text(json.dumps({"folder": device_folder}), encoding="utf-8")
            with mock.patch.object(apple_device.Path, "home", return_value=Path(home)):
                with self.assertRaises(apple_device.Error) as e:
                    apple_device._device_drive_folder(backup)
                self.assertIn("iCloud Drive", str(e.exception))
                drive = Path(home) / "Library" / "Mobile Documents" / "iCloud~com~example~wwhd" / "Documents" / "Wind Waker HD"
                (drive / "user").mkdir(parents=True)
                self.assertEqual(apple_device._device_drive_folder(backup), drive)
                (drive / "user" / ".cking.sav.icloud").write_bytes(b"")
                with self.assertRaises(apple_device.Error):
                    apple_device._device_drive_folder(backup)
            (backup / "user" / "save-folder.json").unlink()
            self.assertIsNone(apple_device._device_drive_folder(backup))


if __name__ == "__main__":
    unittest.main()
