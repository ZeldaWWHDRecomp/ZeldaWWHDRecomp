#!/usr/bin/env python3
"""Derive (and check) the address map between two regional builds of the game.

usage:
  mkbuildmap.py CANONICAL.rpx OTHER.rpx --name EU --title 0005000010143600 [--out FILE]
  mkbuildmap.py --check FILE [CANONICAL.rpx OTHER.rpx]

Every game address in this repository (tools/recomp/hooks*.txt, runtime/src, the docs) is an
address of the USA build: the canonical id of that function or object. Another regional build of
the same game has the same code, compiled with the region's own language branches, so its
functions sit at slightly different addresses. A build map (tools/recomp/builds/*.json) is a
piecewise constant shift that turns a canonical address into that build's address, for code
(.text) and for data (.rodata/.data/.bss).

The map is derived from the two executables and only then committed; it holds numbers, never any
of the game's code. Deriving it asserts that the two builds really are the same program:

  - the same number of functions, in the same order;
  - every function whose body is unique within both builds sits at the same index in both
    (so the i-th function of one build is the i-th of the other);
  - every direct call from a function with an identical body lands on the mapped address;
  - every data address referenced from such a function maps to exactly one address.

Functions whose body differs (the region's own language code) are listed in the map. A hook on
one of them is not valid for the other build, which is why hooks*.txt files can be marked as
belonging to a build (see tools/recomp/builds.py).
"""
import argparse
import bisect
import collections
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from analyze import Program, sext

DATA_LO, DATA_HI = 0x10000000, 0x20000000


class Binary:
    """An rpx with its functions, normalised bodies and relocation targets."""

    def __init__(self, path):
        self.path = path
        self.sha256 = file_sha256(path)
        self.p = p = Program(path)
        p.discover()
        self.entries = p.entries
        self.ends = p.entries[1:] + [p.text_hi]
        # 16-bit relocations point at the halfword, i.e. instruction address + 2
        self.reloc_type = {}
        self.reloc_target = collections.defaultdict(list)
        for sec, addr, typ, sym, add in p.rpx.relocs:
            if sec.name != ".text":
                continue
            self.reloc_type[addr & ~3] = typ
            self.reloc_target[addr & ~3].append((typ, (sym.value + add) & 0xFFFFFFFF))

    def nword(self, a):
        """The instruction at `a` with everything the linker chose masked out: branch
        displacements (the target shifts with the code) and relocated immediates (the address of
        a global shifts with the data)."""
        w = self.p.word(a)
        op = w >> 26
        if op == 18:
            return w & 0xFC000003
        if op == 16:
            return w & 0xFFFF0003
        if a in self.reloc_type:
            return w & 0xFFFF0000
        return w

    def body(self, i):
        return [self.nword(a) for a in range(self.entries[i], self.ends[i], 4)]

    def func_index(self, a):
        i = bisect.bisect_right(self.entries, a) - 1
        return i if 0 <= i < len(self.entries) and a < self.ends[i] else None


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def steps_of(pairs):
    """[(canonical, mapped)] (sorted by canonical) -> [(canonical start, delta)] runs."""
    steps, prev = [], None
    for c, m in pairs:
        if m - c != prev:
            prev = m - c
            steps.append((c, prev))
    return steps


class Mismatch(Exception):
    pass


def derive(canon, other):
    """The map from `canon` to `other`, or Mismatch if they are not the same program."""
    if len(canon.entries) != len(other.entries):
        raise Mismatch("different number of functions: %d and %d" % (len(canon.entries), len(other.entries)))
    n = len(canon.entries)
    bodies_c = [canon.body(i) for i in range(n)]
    bodies_o = [other.body(i) for i in range(n)]
    differs = [i for i in range(n) if bodies_c[i] != bodies_o[i]]

    # every body that is unique within both builds must sit at the same index in both
    def digest(b):
        return hashlib.blake2b(b"".join(w.to_bytes(4, "big") for w in b), digest_size=16).digest()
    dc = [digest(b) for b in bodies_c]
    do = [digest(b) for b in bodies_o]
    count_c, count_o = collections.Counter(dc), collections.Counter(do)
    index_c = {h: i for i, h in enumerate(dc)}
    index_o = {h: i for i, h in enumerate(do)}
    anchors = [h for h in count_c if count_c[h] == 1 and count_o.get(h) == 1]
    moved = [h for h in anchors if index_c[h] != index_o[h]]
    if moved:
        h = moved[0]
        raise Mismatch("%d of %d functions with a body unique to both builds are at a different index, "
                       "e.g. %08X (#%d) and %08X (#%d): the function order is not the same" % (
                           len(moved), len(anchors), canon.entries[index_c[h]], index_c[h],
                           other.entries[index_o[h]], index_o[h]))
    if len(anchors) < n // 8:
        raise Mismatch("only %d of %d functions have a body unique to both builds: too few to tie "
                       "the two together" % (len(anchors), n))

    code_steps = steps_of([(canon.entries[i], other.entries[i]) for i in range(n)])

    # check: every direct call out of an identical function lands on the mapped address
    code = piecewise(code_steps)
    checked = wrong = 0
    for i in range(n):
        if bodies_c[i] != bodies_o[i]:
            continue
        for k in range(0, canon.ends[i] - canon.entries[i], 4):
            a = canon.entries[i] + k
            w = canon.p.word(a)
            if (w >> 26) != 18 or not (w & 1) or a in canon.p.import_calls or a in canon.p.undef_calls:
                continue
            t = (sext(w & 0x03FFFFFC, 26) + (0 if w & 2 else a)) & 0xFFFFFFFF
            if not canon.p.in_text(t):
                continue
            b = other.entries[i] + k
            w2 = other.p.word(b)
            t2 = (sext(w2 & 0x03FFFFFC, 26) + (0 if w2 & 2 else b)) & 0xFFFFFFFF
            checked += 1
            if code(t) != t2:
                wrong += 1
    if wrong:
        raise Mismatch("%d of %d direct calls do not land on the mapped address" % (wrong, checked))

    # data: a relocation in an identical function names the same global in both builds
    seen = collections.defaultdict(collections.Counter)
    for a, lst in canon.reloc_target.items():
        i = canon.func_index(a)
        if i is None or bodies_c[i] != bodies_o[i]:
            continue
        lst2 = other.reloc_target.get(other.entries[i] + (a - canon.entries[i]))
        if not lst2 or len(lst2) != len(lst):
            continue
        for (t1, d1), (t2, d2) in zip(lst, lst2):
            if t1 == t2 and DATA_LO <= d1 < DATA_HI:
                seen[d1][d2] += 1
    ambiguous = {d: c for d, c in seen.items() if len(c) > 1}
    if ambiguous:
        d, c = next(iter(ambiguous.items()))
        raise Mismatch("%d data addresses map to more than one address, e.g. %08X -> %s" % (
            len(ambiguous), d, ", ".join("%08X" % x for x in c)))
    data_steps = steps_of([(d, c.most_common(1)[0][0]) for d, c in sorted(seen.items())])

    return {
        "code_bounds": [canon.p.text_lo, canon.p.text_hi],
        "data_bounds": [min(s.addr for s in canon.p.rpx.sections if s.name in (".rodata", ".data", ".bss")),
                        max(s.addr + s.size for s in canon.p.rpx.sections if s.name in (".rodata", ".data", ".bss"))],
        "code_steps": code_steps,
        "data_steps": data_steps,
        "differing_functions": [[canon.entries[i], canon.ends[i] - canon.entries[i],
                                 other.entries[i], other.ends[i] - other.entries[i]] for i in differs],
        "stats": {"functions": n, "anchors": len(anchors), "calls_checked": checked,
                  "data_addresses": len(seen)},
    }


def piecewise(steps):
    starts = [s for s, _ in steps]
    deltas = [d for _, d in steps]

    def f(a):
        i = bisect.bisect_right(starts, a) - 1
        return (a + deltas[i]) & 0xFFFFFFFF if i >= 0 else a
    return f


def to_json(m, name, title, sha256, canon_sha256, canon_name):
    return {
        "name": name,
        "title_id": title,
        "rpx_sha256": sha256,
        "derived_from": {"name": canon_name, "rpx_sha256": canon_sha256},
        "comment": ("Canonical (%s) address -> %s address, as a piecewise constant shift. Derived and "
                    "checked by tools/recomp/mkbuildmap.py; numbers only, no game code." % (canon_name, name)),
        "checked": m["stats"],
        "code_bounds": ["%08X" % a for a in m["code_bounds"]],
        "data_bounds": ["%08X" % a for a in m["data_bounds"]],
        "code_steps": [["%08X" % a, d] for a, d in m["code_steps"]],
        "data_steps": [["%08X" % a, d] for a, d in m["data_steps"]],
        "differing_functions": [["%08X" % a, s1, "%08X" % b, s2] for a, s1, b, s2 in m["differing_functions"]],
    }


def report(m):
    s = m["stats"]
    print("functions %d, bodies unique to both builds %d, direct calls checked %d, data addresses %d" % (
        s["functions"], s["anchors"], s["calls_checked"], s["data_addresses"]))
    print("code: %d steps, data: %d steps, functions with a different body: %d" % (
        len(m["code_steps"]), len(m["data_steps"]), len(m["differing_functions"])))
    for a, d in m["code_steps"]:
        print("   code from %08X: %+d" % (a, d))
    for a, d in m["data_steps"]:
        print("   data from %08X: %+d" % (a, d))
    for a, s1, b, s2 in m["differing_functions"]:
        print("   body differs: %08X (%d bytes) -> %08X (%d bytes)" % (a, s1, b, s2))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rpx", nargs="*", help="the canonical (USA) cking.rpx and the other build's")
    ap.add_argument("--name", help="short name of the other build, e.g. EU")
    ap.add_argument("--title", help="its title id, e.g. 0005000010143600")
    ap.add_argument("--canonical-name", default="USA")
    ap.add_argument("--out", help="where to write the map (default: stdout)")
    ap.add_argument("--check", help="re-derive the map in this file and compare")
    args = ap.parse_args()

    if args.check:
        have = json.load(open(args.check))
        if len(args.rpx) != 2:
            print("%s: %s, title %s, rpx %s..." % (args.check, have["name"], have["title_id"],
                                                   have["rpx_sha256"][:16]))
            print("pass both rpx files to re-derive and compare")
            return 0
        canon, other = Binary(args.rpx[0]), Binary(args.rpx[1])
        if other.sha256 != have["rpx_sha256"]:
            sys.exit("%s is not the build this map is for (SHA-256 %s..., expected %s...)" % (
                args.rpx[1], other.sha256[:16], have["rpx_sha256"][:16]))
        if canon.sha256 != have["derived_from"]["rpx_sha256"]:
            sys.exit("canonical executable SHA-256 differs from the map's source")
        fresh = to_json(derive(canon, other), have["name"], have["title_id"], other.sha256,
                        canon.sha256, have["derived_from"]["name"])
        same = all(fresh[k] == have.get(k) for k in ("code_steps", "data_steps", "differing_functions", "code_bounds", "data_bounds"))
        report(derive(canon, other))
        print("map in %s: %s" % (args.check, "unchanged" if same else "DIFFERS from the two binaries"))
        return 0 if same else 1

    if len(args.rpx) != 2 or not args.name or not args.title:
        ap.error("need CANONICAL.rpx OTHER.rpx --name NAME --title TITLEID")
    canon, other = Binary(args.rpx[0]), Binary(args.rpx[1])
    m = derive(canon, other)
    report(m)
    out = to_json(m, args.name, args.title, other.sha256, canon.sha256, args.canonical_name)
    text = json.dumps(out, indent=1) + "\n"
    if args.out:
        with open(args.out, "w") as f:
            f.write(text)
        print("wrote %s" % args.out)
    else:
        print(text)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Mismatch as e:
        sys.exit("these two executables are not the same program: %s" % e)
