#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""places.py on synthetic stage files (no game files): actor spots, sea squares, stage squares, names' islands."""
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import places  # noqa: E402


def yaz0(data):  # literal-only Yaz0 (every group's flag byte 0xFF)
    out = bytearray(b"Yaz0" + struct.pack(">I", len(data)) + b"\0" * 8)
    for i in range(0, len(data), 8):
        out += b"\xff" + data[i:i + 8]
    return bytes(out)


def sarc(files):
    names = b""
    offsets = []
    for n, _ in files:
        offsets.append(len(names) // 4)
        names += n.encode() + b"\0"
        names += b"\0" * (-len(names) % 4)
    hl, nodes = 0x14, b""
    data, pos = b"", []
    for _, d in files:
        data += b"\0" * (-len(data) % 0x20)
        pos.append((len(data), len(data) + len(d)))
        data += d
    for i, (s, e) in enumerate(pos):
        nodes += struct.pack(">IIII", i, 0x01000000 | offsets[i], s, e)
    sfat = b"SFAT" + struct.pack(">HHI", 12, len(files), 0x65) + nodes
    sfnt = b"SFNT" + struct.pack(">HH", 8, 0) + names
    head_len = hl + len(sfat) + len(sfnt)
    doff = head_len + (-head_len % 0x20)
    body = sfat + sfnt + b"\0" * (doff - head_len)
    hdr = b"SARC" + struct.pack(">HHIIHH", hl, 0xFEFF, doff + len(data), doff, 0x100, 0)
    return hdr + body + data


def bfres(name, payload):
    """a FRES whose external-file group (header slot 11) holds `name`"""
    f = bytearray(b"FRES" + b"\0" * 0x60)
    g = 0x20 + 4 * 11
    group = len(f)
    struct.pack_into(">i", f, g, group - g)
    f += struct.pack(">ii", 0, 1) + b"\0" * 16 + b"\0" * 16  # header, root entry, entry 1
    e = group + 8 + 16
    name_at = len(f)
    f += name.encode() + b"\0"
    f += b"\0" * (-len(f) % 4)
    desc = len(f)
    f += struct.pack(">iI", 0, len(payload))
    data_at = len(f)
    f += payload
    struct.pack_into(">i", f, e + 8, name_at - (e + 8))
    struct.pack_into(">i", f, e + 12, desc - (e + 12))
    struct.pack_into(">i", f, desc, data_at - desc)
    return bytes(f)


def dz(actors):
    """a dzr/dzs with one chunk of 0x20-byte entries; actors: [(x, y, z)]"""
    head = struct.pack(">I", 1) + b"TRES" + struct.pack(">II", len(actors), 16)
    body = b"".join(b"takara\0\0" + b"\0" * 4 + struct.pack(">fff", *xyz) + b"\0" * 8 for xyz in actors)
    return head + body


class Places(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.game = self.tmp.name
        stage = os.path.join(self.game, "content", "Common", "Stage")
        os.makedirs(stage)

        def put(name, inner, payload):
            with open(os.path.join(stage, name), "wb") as fh:
                fh.write(yaz0(sarc([(inner, bfres(inner == "Stage.bfres" and "stage.dzs" or "room.dzr", payload))])))
        put("sea_Room11.szs", "Room11.bfres", dz([(1, 2, 3), (-150000.4, 10, 250000.6)]))
        put("sea_Stage.szs", "Stage.bfres", dz([(-250000, 0, -250000)]))
        put("kindan_Room3.szs", "Room3.bfres", dz([(5, 6, 7)]))
        put("Hyrule_Room0.szs", "Room0.bfres", dz([(8, 9, 10)]))

    def tearDown(self):
        self.tmp.cleanup()

    def test_build(self):
        P = "content/Common/Stage/%s.szs@YAZ0@SARC@%s.bfres@BFRES@%s"
        locs = [
            {"Names": {"English": "Windfall Island - Chest"}, "Paths": {P % ("sea_Room11", "Room11", "room.dzr"): [0x10 + 0x20 + 4]}},
            {"Names": {"English": "Sea - Treasure"}, "Paths": {P % ("sea_Stage", "Stage", "stage.dzs"): [0x10]}},
            {"Names": {"English": "Forbidden Woods - Chest"}, "Paths": {P % ("kindan_Room3", "Room3", "room.dzr"): [0x10]}},
            {"Names": {"English": "Windfall Island - Letter"}},  # no file: Windfall's square from the chest above
            {"Names": {"English": "Hyrule - Chest"}, "Paths": {P % ("Hyrule_Room0", "Room0", "room.dzr"): [0x10]}},
            {"Names": {"English": "Missing - Chest"}, "Paths": {P % ("nope_Room1", "Room1", "room.dzr"): [0x10]}},
            {"Names": {"English": "Outside - Chest"}, "Path": P % ("sea_Room11", "Room11", "room.dzr"), "Offsets": [0x400]},
        ]
        rows = places.build(self.game, locs)
        out = os.path.join(self.game, "out", "places.tsv")
        places.write(rows, out)
        with open(out, encoding="utf-8") as fh:
            got = [l.rstrip("\n").split("\t") for l in fh if not l.startswith("#")]
        self.assertEqual(got, [
            ["Windfall Island - Chest", "11", "-", "-150000", "10", "250001"],
            ["Sea - Treasure", "9", "-", "-250000", "0", "-250000"],
            ["Forbidden Woods - Chest", "41", "kindan", "5", "6", "7"],
            ["Windfall Island - Letter", "11", "-", "nan", "nan", "nan"],
            ["Hyrule - Chest", "0", "Hyrule", "8", "9", "10"],
            ["Missing - Chest", "0", "-", "nan", "nan", "nan"],
            ["Outside - Chest", "0", "-", "nan", "nan", "nan"],
        ])

    def test_sea_square(self):
        self.assertEqual(places.sea_square(-350000, -350000), 1)
        self.assertEqual(places.sea_square(349999, 349999), 49)
        self.assertEqual(places.sea_square(350000, 0), 0)


if __name__ == "__main__":
    unittest.main()
