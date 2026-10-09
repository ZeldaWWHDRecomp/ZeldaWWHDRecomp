#!/usr/bin/env python3
"""Synthetic save-repair regressions; no player saves or game files."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

import wwsave as w


def sample():
    slots = [bytearray(w.GAMEDATA_SIZE) for _ in range(3)]
    for n, g in enumerate(slots):
        w.put(g, w.HD_FIELDS, "info.save_count", struct.pack(">H", n + 1))
        w.put(g, w.HD_FIELDS, "collect.collect", bytes.fromhex("0f03000100000000"))
        w.put(g, w.HD_FIELDS, "status_a.select_equip", bytes.fromhex("3e3cffff"))
        w.put(g, w.HD_FIELDS, "event.flags", bytes([0x11]) + bytes(255))
    return slots, [w.hd_extra_defaults() for _ in range(3)]


class RepairTest(unittest.TestCase):
    def test_only_selected_sword_and_checksum_change(self):
        slots, extras = sample()
        raw = bytearray(w.write_hd(slots, extras))
        # Preserve nonzero reserved bytes, rather than rewriting the entire save.
        for n in range(3):
            start = n * w.HD_SLOT
            raw[start + w.GAMEDATA_SIZE + 7] = 0x5A
            struct.pack_into(">II", raw, start + w.HD_SLOT_USED,
                             *w.hd_slot_checksum(raw[start:start + w.HD_SLOT]))
        raw = bytes(raw)
        for file in (1, 2, 3):
            result = w.repair_medli(raw, file)
            start = (file - 1) * w.HD_SLOT
            allowed = {start + w.HD_FIELDS[x][0] for x in
                       ("collect.collect", "status_a.select_equip")}
            allowed.update(range(start + w.HD_SLOT_USED, start + w.HD_SLOT))
            self.assertTrue({i for i, (a, b) in enumerate(zip(raw, result)) if a != b} <= allowed)
            repaired, after_extras, _ = w.read_hd(result)
            self.assertEqual(w.get(repaired[file - 1], w.HD_FIELDS, "collect.collect")[0], 1)
            self.assertEqual(w.get(repaired[file - 1], w.HD_FIELDS, "status_a.select_equip"), b"\x38\x3c\xff\xff")
            self.assertEqual(after_extras, w.read_hd(raw)[1])
            with self.assertRaises(w.HdError):
                w.repair_medli(result, file)  # no second alteration

    def test_refuse_later_progress_and_bad_checksums(self):
        for field, value in [("collect.symbol", b"\x02"), ("collect.triforce", b"\x01"),
                             ("info.clear_count", b"\x01"), ("info.save_count", b"\0\0"),
                             ("memory03.dungeon_item", b"\x08"),
                             ("collect.collect", bytes.fromhex("0101000100000000"))]:
            slots, extras = sample()
            w.put(slots[0], w.HD_FIELDS, field, value)
            with self.assertRaises(w.HdError):
                w.repair_medli(w.write_hd(slots, extras))
        slots, extras = sample()
        ev = bytearray(w.get(slots[0], w.HD_FIELDS, "event.flags")); ev[0x2E] |= 4
        w.put(slots[0], w.HD_FIELDS, "event.flags", ev)
        with self.assertRaises(w.HdError):
            w.repair_medli(w.write_hd(slots, extras))
        raw = bytearray(w.write_hd(*sample())); raw[0] ^= 1
        with self.assertRaises(w.HdError):
            w.repair_medli(raw)
        for file in (0, 4):
            with self.assertRaises(w.HdError):
                w.repair_medli(w.write_hd(*sample()), file)

    def test_cli_backup_source_preservation_and_no_overwrites(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            source = root / "source.sav"; raw = w.write_hd(*sample()); source.write_bytes(raw)
            out = root / "repair"
            cmd = [sys.executable, str(pathlib.Path(w.__file__)), "repair-medli", str(source), "-o", str(out), "--file", "2"]
            first = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(source.read_bytes(), raw)
            self.assertEqual((out / "cking.sav.before-medli.bak").read_bytes(), raw)
            expected = w.repair_medli(raw, 2)
            self.assertEqual((out / "cking.sav").read_bytes(), expected)
            self.assertNotEqual(subprocess.run(cmd, capture_output=True).returncode, 0)
            self.assertEqual((out / "cking.sav").read_bytes(), expected)
            self.assertEqual((out / "cking.sav.before-medli.bak").read_bytes(), raw)
            source.write_bytes(b"invalid")
            cmd[cmd.index(str(out))] = str(root / "invalid")
            self.assertNotEqual(subprocess.run(cmd, capture_output=True).returncode, 0)
            self.assertFalse((root / "invalid").exists())


if __name__ == "__main__":
    unittest.main()
