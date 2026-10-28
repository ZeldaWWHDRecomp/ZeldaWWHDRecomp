#!/usr/bin/env python3
"""Check a scripted run's WWHD_PLAYCTX_LOG trace (runtime/src/mods/play_context.h): each EXPECT must be
met by some "[playctx]" line, in the given order (a later expectation by a later or the same line).

usage: play_context_check.py LOG EXPECT [EXPECT ...]
  EXPECT is a comma-separated list of conditions on one trace line:
    flag            the flag is set (game, link, play, event, message, menu, fade, keyicon, choice, ...)
    !flag           the flag is not set
    KEY=VALUE       a field equals VALUE (A, B, ZR, X, Y, R, hand, attn, lock, minigame...: as logged)
    KEY&MASK        a hex field (st0, st1, mode) has any of MASK's bits
    KEY!&MASK       ... has none of them
    buttons~B       the context buttons (context_buttons.h) show B, as traced: A:04, X:i35, ZR:ff, ZL, tap...
    buttons!~B      ... don't show it
  e.g. play_context_check.py log "play,A=04,attn=4" "A=0E" "!play,event,message"
Exit status 0 when all are met; else the first unmet expectation and the trace lines are printed.

Typical run (headless, muted; the state is a portable .wwstate copied into a states folder as slot1; it
loads at TV frame 3400, the scenario clock starts there too, and Link stands in the state's place about
3 s later; WWHD_TEST_ORIGIN_LOAD only counts full save-state loads, not portable ones):
  tools/gametest/run_scenario.sh build/wwhd game save runs/pot 9 WWHD_STATE_DIR=$PWD/states/pot \\
      WWHD_STATE_LOAD_AT=3400:1 WWHD_PLAYCTX_LOG=1 WWHD_TEST_PRESS=6-6.15:8000
  tools/gametest/play_context_check.py runs/pot/log "play,A=04,buttons~A:04" "A=0E,ZR=09,buttons~B:0E"
"""
import re
import sys


def parse(line):
    m = re.search(r"\[playctx\] (step=.*)$", line)
    if not m:
        return None
    fields = {}
    rest = m.group(1)
    b = re.search(r" buttons=\[([^\]]*)\]", rest)
    if b:
        fields["buttons"] = b.group(1)
        rest = rest[:b.start()] + rest[b.end():]
    for tok in rest.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            fields[k] = v
    fields["_flags"] = set(fields.get("flags", "").split(",")) - {""}
    return fields


def met(fields, cond):
    for c in cond.split(","):
        c = c.strip()
        if not c:
            continue
        if c.startswith("buttons~"):
            if c[8:] not in fields.get("buttons", "").split():
                return False
        elif c.startswith("buttons!~"):
            if c[9:] in fields.get("buttons", "").split():
                return False
        elif "!&" in c:
            k, mask = c.split("!&", 1)
            if int(fields.get(k, "0"), 16) & int(mask, 16):
                return False
        elif "&" in c:
            k, mask = c.split("&", 1)
            if not int(fields.get(k, "0"), 16) & int(mask, 16):
                return False
        elif "=" in c:
            k, v = c.split("=", 1)
            if fields.get(k, "").upper() != v.upper():
                return False
        elif c.startswith("!"):
            if c[1:] in fields["_flags"]:
                return False
        elif c not in fields["_flags"]:
            return False
    return True


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    lines = [(l.rstrip("\n"), parse(l)) for l in open(sys.argv[1], errors="replace")]
    trace = [(l, f) for l, f in lines if f]
    i = 0
    for exp in sys.argv[2:]:
        while i < len(trace) and not met(trace[i][1], exp):
            i += 1
        if i == len(trace):
            print("UNMET: %s" % exp)
            for l, _ in trace[-40:]:
                print("  " + l)
            return 1
        print("met %-40s by %s" % (exp, trace[i][0].split("[playctx] ", 1)[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
