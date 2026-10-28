#!/usr/bin/env python3
"""Check a scripted item ring run (runtime/src/mods/item_ring.h): its WWHD_ITEM_RING_LOG lines and the save
info dumped at the end (WWHD_SAVEINFO_DUMP: dSv_info_c, the select items X / Y / R at +0x29, the inventory
at +0x5C).
usage: item_ring_check.py LOG SI.BIN EXPECT [EXPECT ...]
  EXPECT, met in order by "[ring]" lines (a later one by a later or the same line):
    open:Y          the ring opened on Y (X, Y or R)
    on:NAME         the stick / finger rested on the entry NAME (as logged, e.g. "Hookshot")
    closed:equip    it closed with an equip (closed:press, closed:nothing)
    put:Y           an equip was done on Y; the save's select item for Y at the end must be the slot it
                    logged, and the other buttons as logged with it
    pace:held       the game's pause timer held the world (pace:runs: it ran again)
    !open           (at the end of the list) no ring opened after the last matched line
Exit status 0 when all are met; else the first unmet expectation and the ring lines are printed.
Typical run: see docs/controller-input.md ("Item ring", Testing).
"""
import re
import sys


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    log, si, expects = sys.argv[1], sys.argv[2], sys.argv[3:]
    lines = [l.rstrip("\n") for l in open(log, errors="replace") if "[ring]" in l]
    data = open(si, "rb").read()
    select = data[0x29:0x2C]
    i = 0
    last_put = {}
    for e in expects:
        if e == "!open":
            if any(" open (" in l for l in lines[i:]):
                print("FAIL: a ring opened after the last expectation")
                print("\n".join(lines))
                return 1
            continue
        kind, _, arg = e.partition(":")
        found = False
        while i < len(lines):
            l = lines[i]
            ok = False
            if kind == "open":
                ok = re.search(r"\[ring\] %s open \(" % re.escape(arg), l) is not None
            elif kind == "on":
                ok = re.search(r"\[ring\] on: %s \(" % re.escape(arg), l) is not None
            elif kind == "closed":
                ok = re.search(r"\[ring\] closed: %s" % re.escape(arg), l) is not None
            elif kind == "pace":
                ok = re.search(r"\[ring\] pace: %s" % re.escape(arg), l) is not None
            elif kind == "put":
                m = re.search(r"\[ring\] %s: slot ([0-9A-F]{2}) put on; buttons now ([0-9A-F]{2}) ([0-9A-F]{2}) ([0-9A-F]{2})" % re.escape(arg), l)
                if m:
                    ok = True
                    last_put = {"button": "XYR".index(arg), "slot": int(m.group(1), 16), "now": [int(m.group(k), 16) for k in (2, 3, 4)]}
            else:
                print("unknown expectation", e)
                return 2
            if ok:
                found = True
                break
            i += 1
        if not found:
            print("FAIL: unmet", e)
            print("\n".join(lines))
            return 1
    if last_put:
        b = last_put["button"]
        if select[b] != last_put["slot"]:
            print("FAIL: save select %s is %02X, the ring put %02X" % ("XYR"[b], select[b], last_put["slot"]))
            return 1
        if list(select) != last_put["now"]:
            print("FAIL: save select items %s, logged after the equip %s" % (select.hex(" "), last_put["now"]))
            return 1
    print("ok: %d expectations met; save select items X Y R = %s" % (len(expects), select.hex(" ").upper()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
