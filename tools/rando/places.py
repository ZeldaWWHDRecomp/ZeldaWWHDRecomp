#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Where each TWWHD Randomizer location is, for the spoiler log's map, read from the player's own game.

Runs on the player's machine (setup, or the first time a seed is made) and writes its table into the player's data
folder; the repository and the packages hold none of it. Adapted from GreenNaugahyde/ZeldaWWHDRecompAndroid (MPL-2.0)
tools/rando/spoiler_places.py at db65d60, over this repository's Yaz0 / SARC readers (tools/shaderprep.py).

The randomizer's location data (logic/data/location_data.yaml) names, for most locations, the stage or room file and
the byte offsets it changes there; the actor entry (chest, salvage point, item, ...) around the first offset gives the
spot (its x / y / z). The square: a sea room's number, the 7 x 7 grid for the sea's own stage file, else the stage's
square (STAGE_SQUARES); locations without a file (rewards, mail, shops) take their island's square from the name
("Windfall Island - ...") as the located ones teach it. Dungeons keep their stage (the map draws its floors).

usage: places.py --game GAME_DIR --locations LOCATIONS.json --out OUT.tsv
  (LOCATIONS.json: the output of `wwhd-rando --data DATA locations`)
  OUT: name<TAB>square (0: none)<TAB>stage ("-": the sea or none)<TAB>x<TAB>y<TAB>z ("nan": no spot)
"""
import argparse
import collections
import math
import os
import re
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import shaderprep  # noqa: E402  (Yaz0 and SARC readers)

FILE = re.compile(r"(?:Stage/|@SARC@)([A-Za-z0-9_]+?)_(Stage|Room\d+)\.szs@YAZ0@SARC@[^@]+\.bfres@BFRES@(room\.dzr|stage\.dzs)$")
# The sea square (1..49) of the stages off the sea's own files: stage names and square numbers only, as
# GreenNaugahyde's ProgressPlaces.java (MPL-2.0, db65d60) gives them where its places agree on one square.
STAGE_SQUARES = {
    "Asoko": 11, "Cave01": 34, "Cave02": 2, "Cave04": 16, "Cave05": 43, "Cave07": 12, "Cave09": 44, "Cave10": 44,
    "Edaichi": 45, "Ekaze": 4, "GanonJ": 26, "M2tower": 1, "M_Dai": 45, "M_DaiB": 45, "M_DaiMB": 45, "M_Dra09": 13,
    "M_DragB": 13, "M_NewD2": 13, "MiniHyo": 40, "MiniKaz": 20, "Ocmera": 11, "Pfigure": 11, "Pjavdou": 44,
    "Pnezumi": 11, "ShipD": 30, "Siren": 26, "SirenB": 26, "SirenMB": 26, "TF_01": 31, "TF_02": 7, "TF_03": 35,
    "TF_04": 33, "TyuTyu": 12, "kaze": 4, "kazeB": 4, "kazeMB": 4, "kenroom": 26, "kinBOSS": 41, "kinMB": 41,
    "kindan": 41, "majroom": 1,
}
ACTOR_TAGS = ("ACT", "SCO", "TRE", "TGO", "TGS", "DOO")


def be32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def sea_square(x, z):
    col, row = math.floor((x + 350000) / 100000), math.floor((z + 350000) / 100000)
    return row * 7 + col + 1 if 0 <= col < 7 and 0 <= row < 7 else 0


def bfres_file(f, wanted):
    """an external file of a BFRES (the HD rooms embed room.dzr / stage.dzs there), or None"""
    s32 = lambda o: struct.unpack_from(">i", f, o)[0]
    g = 0x20 + 4 * 11
    if len(f) < g + 4 or f[:4] != b"FRES" or s32(g) == 0:
        return None
    go = g + s32(g)
    for i in range(1, s32(go + 4) + 1):
        e = go + 8 + 16 * i
        no = e + 8 + s32(e + 8)
        name = f[no:f.index(b"\0", no)].decode(errors="replace")
        if name == wanted:
            do = e + 12 + s32(e + 12)
            return f[do + s32(do):do + s32(do) + be32(f, do + 4)]
    return None


class Game:
    """the files a location path names ("content/...szs@YAZ0@SARC@x.bfres@BFRES@room.dzr"), read once each"""

    def __init__(self, root):
        self.root = root
        self.cache = {}

    def open(self, path):
        if path in self.cache:
            return self.cache[path]
        parts = path.split("@")
        data = None
        if len(parts) == 1:
            try:
                with open(os.path.join(self.root, *parts[0].split("/")), "rb") as fh:
                    data = fh.read()
            except OSError:
                data = None
        else:
            kind = parts[-2] if len(parts) >= 3 and parts[-2] in ("SARC", "BFRES") else parts[-1]
            if kind == "YAZ0":
                outer = self.open("@".join(parts[:-1]))
                data = shaderprep.yaz0(outer) if outer else None
            elif kind == "SARC":
                outer = self.open("@".join(parts[:-2]))
                if outer and outer[:4] == b"SARC":
                    data = next((d for n, d in shaderprep.sarc(outer) if n == parts[-1]), None)
            elif kind == "BFRES":
                outer = self.open("@".join(parts[:-2]))
                data = bfres_file(outer, parts[-1]) if outer else None
        # only the actor lists stay (the archives above them are large and read once per stage)
        if path.endswith((".dzr", ".dzs")) or len(self.cache) < 4:
            self.cache[path] = data
        return data


def entry_at(d, off):
    """(x, y, z) of the actor entry holding byte `off` of a dzr / dzs"""
    if not d or len(d) < 4:
        return None
    n = be32(d, 0)
    if n > 256 or 4 + 12 * n > len(d):
        return None
    for i in range(n):
        e = 4 + 12 * i
        tag = d[e:e + 4].decode("latin1")
        count, start = be32(d, e + 4), be32(d, e + 8)
        size = 0x24 if tag.startswith(("SCO", "TGSC")) else 0x20
        if tag[:3] not in ACTOR_TAGS:
            continue
        if start <= off < start + count * size:
            k = start + (off - start) // size * size
            if k + 24 > len(d):
                return None
            return struct.unpack_from(">fff", d, k + 12)
    return None


def load_locations(path):
    """the locations as location_data.yaml has them, from the output of `wwhd-rando --data DATA locations` (JSON: the
    release's Python has no YAML reader)"""
    import json
    with open(path, encoding="utf-8") as fh:
        return [{"Names": {"English": l["name"]}, "Paths": l["paths"]} for l in json.load(fh)["locations"]]


def build(game_dir, locations):
    game = Game(game_dir)
    rows, prefix = [], collections.defaultdict(collections.Counter)
    for loc in locations:
        name = loc["Names"]["English"]
        if "Paths" in loc:
            paths = loc["Paths"] or {}
        elif "Path" in loc:
            paths = {loc["Path"]: loc.get("Offsets") or []}
        else:
            paths = {}
        spot = stage = room = None
        for path, offs in paths.items():
            m = FILE.search(path)
            if not m or not offs:
                continue
            xyz = entry_at(game.open(path), offs[0])
            if xyz is None:
                continue
            spot, stage, room = xyz, m.group(1), m.group(2)
            break
        square = 0
        if stage == "sea":
            square = int(room[4:]) if room.startswith("Room") and room != "Room0" else sea_square(spot[0], spot[2])
        elif stage is not None:
            square = STAGE_SQUARES.get(stage, 0)
        rows.append([name, square, stage, spot])
        if square and " - " in name:
            prefix[name.split(" - ")[0]][square] += 1
    learnt = {k: c.most_common(1)[0][0] for k, c in prefix.items()}
    for row in rows:
        if not row[1] and " - " in row[0]:
            row[1] = learnt.get(row[0].split(" - ")[0], 0)
    return rows


def write(rows, out):
    lines = ["# TWWHD Randomizer locations: name, square, stage (- the sea), x, y, z (tools/rando/places.py)\n"]
    for name, square, stage, spot in rows:
        st = "-" if stage in (None, "sea") else stage
        xyz = "\t".join("%.0f" % v for v in spot) if spot and stage is not None else "nan\tnan\tnan"
        lines.append("%s\t%d\t%s\t%s\n" % (name, square, st, xyz))
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(os.path.abspath(out)), prefix=".places-")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        fh.writelines(lines)
    os.replace(tmp, out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--game", required=True, help="the game folder (code/, content/, meta/)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--locations", required=True, help="the output of wwhd-rando --data DATA locations")
    a = ap.parse_args(argv)
    rows = build(a.game, load_locations(a.locations))
    write(rows, a.out)
    unplaced = [r[0] for r in rows if not r[1]]
    print("%d locations, %d with a spot, %d without a square" % (len(rows), sum(1 for r in rows if r[3]), len(unplaced)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
