#!/usr/bin/env python3
"""Portable save states (slotN.wwstate, runtime/src/portable_state.h): check one, show what it
holds, or turn its save data into a cking.sav.

usage:
  wwstate.py info STATE.wwstate                 header, place and progress (checks both checksums)
  wwstate.py to-sav STATE.wwstate -o OUTDIR [--into cking.sav] [--file N]
        writes OUTDIR/cking.sav whose Quest Log N (default: the state's own) is the state's save
        data; the other Quest Logs come from --into (else they are empty "New Game" files).
        The game then loads that file at the state's return place (where the game would continue
        after an in-game save there); the state itself puts Link at the exact position.

Plain Python 3, no game data. A state is a few KB of text; anything else is refused.
"""
import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wwsave as W  # noqa: E402

MAX_SIZE = 64 * 1024
BLOBS = {"savedata": W.HD_SLOT, "hd_player": 16, "hd_status": 4, "hd_event": 20, "hd_map": 220}


def read_state(path):
    raw = open(path, "rb").read(MAX_SIZE + 1)
    if len(raw) > MAX_SIZE:
        raise SystemExit("%s: larger than 64 KB, not a portable state" % path)
    text = raw.decode("utf-8")
    at = text.find("\nchecksum = ")
    if at < 0:
        raise SystemExit("%s: no checksum line" % path)
    body, ck = text[:at + 1], text[at + 1:].strip().split("=", 1)[1].strip()
    if ck != "crc32:%08X" % (zlib.crc32(body.encode("utf-8")) & 0xFFFFFFFF):
        raise SystemExit("%s: checksum mismatch (file was changed or damaged)" % path)
    kv = {}
    for line in body.splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            k, v = line.split("=", 1)
            kv[k.strip()] = v.strip()
    if kv.get("format") != "1":
        raise SystemExit("%s: format %s (this tool reads 1)" % (path, kv.get("format")))
    for k, n in BLOBS.items():
        b = bytes.fromhex(kv[k])
        if len(b) != n:
            raise SystemExit("%s: %s has %d bytes, expected %d" % (path, k, len(b), n))
        kv[k] = b
    blk = kv["savedata"]
    if struct.unpack(">II", blk[W.HD_SLOT_USED:W.HD_SLOT]) != W.hd_slot_checksum(blk):
        raise SystemExit("%s: save data checksum mismatch" % path)
    return kv


def cmd_info(a):
    st = read_state(a.state)
    for k in ("title_id", "title_version", "runtime", "created", "player_name", "stage", "room", "start_point",
              "start_room", "layer", "link_pos", "link_angle_y", "on_ship", "has_ship", "ship_pos", "ship_angle_y", "time_of_day", "date"):
        print("%-14s %s" % (k, st[k]))
    print("%-14s %d" % ("quest_log", int(st["file_slot"]) + 1))
    import hd_save_info
    info = hd_save_info.slot_info(st["savedata"][:W.GAMEDATA_SIZE], st["player_name"])
    for k, v in info.items():
        print("%-14s %s" % (k, v))


def cmd_to_sav(a):
    st = read_state(a.state)
    n = (a.file if a.file else int(st["file_slot"]) + 1) - 1
    if a.into:
        slots, extras, _ = W.read_hd(open(a.into, "rb").read())
    else:
        slots = [bytes(W.GAMEDATA_SIZE)] * 3
        extras = [W.hd_extra_defaults() for _ in range(3)]
    slots = list(slots)
    slots[n] = st["savedata"][:W.GAMEDATA_SIZE]
    extras[n] = dict(extras[n], player=st["hd_player"], status=st["hd_status"], event=st["hd_event"], map=st["hd_map"],
                     name=st["player_name"] or "Link")
    os.makedirs(a.o, exist_ok=True)
    out = os.path.join(a.o, "cking.sav")
    open(out, "wb").write(W.write_hd(slots, extras))
    print("%s: Quest Log %d from %s" % (out, n + 1, a.state))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info")
    p.add_argument("state")
    p.set_defaults(fn=cmd_info)
    p = sub.add_parser("to-sav")
    p.add_argument("state")
    p.add_argument("-o", required=True)
    p.add_argument("--into")
    p.add_argument("--file", type=int, choices=(1, 2, 3))
    p.set_defaults(fn=cmd_to_sav)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
