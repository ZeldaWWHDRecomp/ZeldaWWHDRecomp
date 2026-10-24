#!/usr/bin/env python3
"""compare.py RUN_A RUN_B [END]: the game state of two scripted runs (run_scenario.sh output folders),
aligned at the scenario origin. Used to compare the decomp build with the normal build
(docs/decomp-build.md); any two builds or settings can be compared the same way.

Compared per logic step (relative to the origin, 0 .. end*30):
  link    Link trace lines (procedure, position, speeds, angles, animation frame ...), real-time column dropped
  camera  camera trace lines
  rng     cM_rnd seeds before each Link execute
  sound   sound starts (step, wrapper, sound id); caller LR reported separately
  procs   per process execute: id, profile, 64-bit hash of the process memory, position
  save    save-info block at the end (si.bin)
"""
import collections
import os
import re
import sys


def origin(run):
    # ALIGN=link: runs are aligned on Link's first execute (profile 00A8 in the digest)
    if os.environ.get("ALIGN") == "link":
        if not os.path.exists(os.path.join(run, "digest.txt")):
            print("%s: no digest.txt (the run did not take place)" % run)
            return None
        for l in open(os.path.join(run, "digest.txt"), errors="replace"):
            p = l.split()
            if len(p) > 2 and p[2] == "00A8":
                return int(p[0])
        return None
    log = os.path.join(run, "log")
    if not os.path.exists(log):  # (no such run: e.g. FPS without 30 compares against new30)
        print("%s: no log (the run did not take place)" % run)
        return None
    for l in open(log, errors="replace"):
        m = re.search(r"\[test\] origin.*logic step (\d+)", l)
        if m:
            return int(m.group(1))
    return None


def lines(run, name):
    # (a run stopped right after its end may have written its last line only in part: left out)
    p = os.path.join(run, name)
    if not os.path.exists(p):
        return []
    text = open(p, errors="replace").read()
    out = text.splitlines()
    if out and not text.endswith("\n"):
        out.pop()
    return out


def by_step(run, name, stepcol, drop=(), s0=0, end=None):
    out = collections.defaultdict(list)
    for l in lines(run, name):
        p = l.split()
        if len(p) <= stepcol:
            continue
        try:
            st = int(p[stepcol]) - s0
        except ValueError:
            continue
        if st < 0 or (end is not None and st > end):
            continue
        out[st].append(" ".join(x for i, x in enumerate(p) if i != stepcol and i not in drop))
    return out


def first_diff(a, b, label, show=2):
    # a run killed right after the end may not have flushed its last lines: common range only, without
    # the last step of the shorter run (a step's lines may have been written in part)
    last = min(max(a, default=0), max(b, default=0)) - 1
    ks = sorted(k for k in set(a) | set(b) if k <= last)
    nd = [k for k in ks if a.get(k) != b.get(k)]
    res = "%-7s %5d steps, %s" % (label, len(ks), "identical" if not nd else "%d differ, first at step %d (t=%.2f s)" % (len(nd), nd[0], nd[0] / 30))
    detail = []
    for k in nd[:show]:
        aa, bb = a.get(k, []), b.get(k, [])
        sa, sb = collections.Counter(aa), collections.Counter(bb)
        only_a = list((sa - sb).elements())[:4]
        only_b = list((sb - sa).elements())[:4]
        detail.append("   step %d: A only %s\n            B only %s" % (k, only_a, only_b))
    return res, nd, detail


def main():
    A, B = sys.argv[1], sys.argv[2]
    end = int(float(sys.argv[3]) * 30) if len(sys.argv) > 3 else None
    sa, sb = origin(A), origin(B)
    print("origin: %s step %s, %s step %s" % (A, sa, B, sb))
    if sa is None or sb is None:
        print("no origin (scenario did not start)")
        return 2
    bad = 0
    # trace.txt: pass step realtime full dt proc ...  (drop the pass counter and real time)
    for name, label, stepcol, drop in (("trace.txt", "link", 1, (0, 2)), ("cam.txt", "camera", 1, (0, 2)), ("rng.txt", "rng", 0, ())):
        r, nd, det = first_diff(by_step(A, name, stepcol, drop, sa, end), by_step(B, name, stepcol, drop, sb, end), label)
        print(r)
        for d in det:
            print(d)
        bad += bool(nd)
    # se.txt: step full fn id lr
    sea = by_step(A, "se.txt", 0, (4,), sa, end)
    seb = by_step(B, "se.txt", 0, (4,), sb, end)
    r, nd, det = first_diff(sea, seb, "sound")
    print(r + "  (%d / %d starts)" % (sum(map(len, sea.values())), sum(map(len, seb.values()))))
    for d in det:
        print(d)
    bad += bool(nd)
    lra = by_step(A, "se.txt", 0, (), sa, end)
    lrb = by_step(B, "se.txt", 0, (), sb, end)
    nlr = sum(1 for k in set(lra) | set(lrb) if lra.get(k) != lrb.get(k))
    if nlr and not nd:
        print("        (caller LR of a sound start differs at %d steps: a swapped caller)" % nlr)
    # digest.txt: step id name size hash [x y z]
    da = by_step(A, "digest.txt", 0, (), sa, end)
    db = by_step(B, "digest.txt", 0, (), sb, end)
    r, nd, det = first_diff(da, db, "procs", show=3)
    print(r + "  (%d / %d process executes)" % (sum(map(len, da.values())), sum(map(len, db.values()))))
    for d in det:
        print(d)
    if nd:
        # which processes (by profile name) differ at all
        names = collections.Counter()
        for k in nd:
            ca, cb = collections.Counter(da.get(k, [])), collections.Counter(db.get(k, []))
            for x in (ca - cb) + (cb - ca):
                names[x.split()[1]] += 1
        print("   differing process lines by profile:", ", ".join("%s:%d" % kv for kv in names.most_common(12)))
    bad += bool(nd)
    # images at scenario logic steps (WWHD_DUMP_STEPS): pixels that differ (TV and GamePad)
    import glob
    imgs = sorted(os.path.basename(p) for p in glob.glob(os.path.join(A, "step_*.png")))
    if imgs:
        from PIL import Image, ImageChops
        res = []
        for n in imgs:
            pb_ = os.path.join(B, n)
            if not os.path.exists(pb_):
                res.append("%s missing" % n)
                continue
            d = ImageChops.difference(Image.open(os.path.join(A, n)).convert("RGB"), Image.open(pb_).convert("RGB"))
            k = sum(1 for px in d.get_flattened_data() if px != (0, 0, 0))
            if k > 100:
                res.append("%s %d px %s" % (n, k, d.getbbox()))
        print("images  %d compared, %s" % (len(imgs), "identical (<=100 px)" if not res else "%d differ: %s" % (len(res), "; ".join(res[:6]))))
        bad += bool(res)
    pa, pb = os.path.join(A, "si.bin"), os.path.join(B, "si.bin")
    if os.path.exists(pa) and os.path.exists(pb):
        x, y = open(pa, "rb").read(), open(pb, "rb").read()
        diffs = [i for i in range(min(len(x), len(y))) if x[i] != y[i]]
        print("save    %s" % ("identical (%d bytes)" % len(x) if x == y else "%d bytes differ: %s" % (len(diffs), ["0x%X" % i for i in diffs[:16]])))
        bad += x != y
    else:
        print("save    missing si.bin")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
