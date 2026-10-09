#!/usr/bin/env python3
"""Validate the actor evidence reader without game data."""
from pathlib import Path
import struct
import tempfile
import unittest

from medli_scenario import actor_records


def record(step, full=1):
    data = bytes(256)
    return b"ADMP" + struct.pack("<QIfII", step, full, 1.0, 0x15000000, len(data)) + data


class EvidenceTest(unittest.TestCase):
    def test_count_only_complete_full_passes_after_room_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "actors.bin"
            data = record(100) + record(200, 0) + record(201) + record(202)
            for tail in (b"", record(203)[:15], record(203)[:100]):
                p.write_bytes(data + tail)
                self.assertEqual(actor_records(p, 200), 2)
            p.write_bytes(b"")
            self.assertEqual(actor_records(p, 0), 0)

    def test_reject_corruption_inside_the_stream(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "actors.bin"
            p.write_bytes(record(201) + b"BAD!" + record(202)[4:])
            with self.assertRaises(ValueError):
                actor_records(p, 200)
            p.write_bytes(b"ADMP" + struct.pack("<QIfII", 201, 1, 1.0, 0x15000000, 0x10001))
            with self.assertRaises(ValueError):
                actor_records(p, 200)


if __name__ == "__main__":
    unittest.main()
