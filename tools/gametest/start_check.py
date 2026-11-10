#!/usr/bin/env python3
"""start_check.py: the start of a scripted run that loads a portable state (WWHD_TEST_FIXED_SEED), from its log.

The fixed-seed test aid logs where the state's scene change began: the old stage's cM_rnd seeds just before they were
reset ("were ...", a fingerprint of everything before the load) and mTimer when the old stage's loading frames began.
Two runs are only comparable when these agree: a run whose boot or title screen took another course reaches the load
from a different state, and its later differences are not the build's doing (2026-11-06: cliff_check's fake
divergences, such as A/B runs of one scenario on two paths).

  start_check.py RUN                          prints the start
  start_check.py RUN --timer N [--were "a b c"]  exit 3 (BADSTART) unless the start is that one
  start_check.py RUN_A RUN_B                  exit 3 unless both runs started the same way
  (any form: exit 4 when a run had frame interpolation on)

Exit 2: no start line (no portable load, or the run did not get there).
Exit 4 (INTERP): a run had frame interpolation on. Comparison runs play at 30 fps: with interpolation on, how many logic
steps a stage change takes follows the audio threads' wall-clock frames (the new play scene waits for the audio
system's first dynamic wave), so the stage entry can move by a step and the runs are not comparable.
Used by cliff_check.sh and compare.py.
"""
import os
import re
import sys

_LOADING = re.compile(r"portable state: loading the new stage: cM_rnd seeds [\d ]+\(were ([\d ]+)\).*?mTimer (\d+)\)")


def start(run):
    """(mTimer, old-stage seeds) of the run's first portable-state load, or None"""
    log = os.path.join(run, "log")
    if not os.path.exists(log):
        return None
    m = _LOADING.search(open(log, errors="replace").read())
    return (int(m.group(2)), m.group(1).strip()) if m else None


_INTERP_ON = re.compile(r"\[interp\] frame interpolation on.*|\[test\] t=[\d.]+ s: 60 fps mode [12]")


def interp_on(run):
    """the log line that shows frame interpolation switched on in the run, or None (30 fps throughout)"""
    log = os.path.join(run, "log")
    if not os.path.exists(log):
        return None
    m = _INTERP_ON.search(open(log, errors="replace").read())
    return m.group(0) if m else None


def interp_message(run, line):
    return ("INTERP: %s had frame interpolation on (%s): comparison runs play at 30 fps (WWHD_INTERP=0, no "
            "WWHD_INTERP_AT_STEP / WWHD_INTERP_FPS / scenario 60 fps mode); not a valid comparison" % (run, line.strip()))


def describe(s):
    return "mTimer %d, old-stage seeds %s" % s if s else "no portable-state start"


def main(argv):
    args = argv[1:]
    timer = were = None
    if "--timer" in args:
        i = args.index("--timer"); timer = int(args[i + 1]); del args[i:i + 2]
    if "--were" in args:
        i = args.index("--were"); were = args[i + 1].strip(); del args[i:i + 2]
    if len(args) not in (1, 2):
        print(__doc__)
        return 2
    for r in args:
        line = interp_on(r)
        if line:
            print("start: " + interp_message(r, line))
            return 4
    a = start(args[0])
    if len(args) == 2:
        b = start(args[1])
        if a is None or b is None:
            print("start: %s: %s; %s: %s" % (args[0], describe(a), args[1], describe(b)))
            return 2
        ok = a == b
        print("start: %s (%s / %s)" % ("same" if ok else "BADSTART: the runs started differently", describe(a), describe(b)))
        return 0 if ok else 3
    print("start: %s: %s" % (args[0], describe(a)))
    if a is None:
        return 2
    if (timer is not None and a[0] != timer) or (were is not None and a[1] != were):
        print("start: BADSTART (expected mTimer %s, old-stage seeds %s)" % (timer, were))
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
