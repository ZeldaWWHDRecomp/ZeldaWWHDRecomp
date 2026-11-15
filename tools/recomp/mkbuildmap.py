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
import difflib
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
        self.code_pointers = {}  # data word -> the code address it holds (vtables, callback tables)
        for sec, addr, typ, sym, add in p.rpx.relocs:
            if sec.name != ".text":
                if typ == 1 and p.in_text((sym.value + add) & 0xFFFFFFFF):  # R_PPC_ADDR32
                    self.code_pointers[addr] = (sym.value + add) & 0xFFFFFFFF
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


class Layout:
    """A global object some of whose fields sit elsewhere in the other build: the Japanese build's
    dComIfG_play_c (g_dComIfG_gameInfo + 0x12A0) is 4 bytes shorter from its +0x4810 on, so every
    instruction reaching such a field through a pointer has another displacement there.

    `start`/`size`: the object (canonical address, bytes). `bases`: offsets inside it that the code
    keeps pointers to (the object itself, a member object). `steps`: [(offset, delta)] from the
    object's start, a piecewise constant field shift relative to the object's own address shift.

    Spec on the command line: NAME:START:SIZE:BASE[+BASE...]:OFF=DELTA[+OFF=DELTA...], all hex but
    the deltas, e.g. g_dComIfG_gameInfo:1046F0B0:62F8:0+12A0:5AB0=-4+5BEF=-5+5BF0=-4"""

    def __init__(self, name, start, size, bases, steps):
        self.name, self.start, self.size = name, start, size
        self.bases = sorted(set(bases))
        self.steps = sorted(steps)
        if not self.steps or any(not 0 <= off < size for off, _ in self.steps) or \
                any(not 0 <= b < size for b in self.bases) or len({o for o, _ in self.steps}) != len(self.steps):
            raise ValueError("layout %s: offsets must be unique and inside the object" % name)

    @classmethod
    def parse(cls, spec):
        try:
            name, start, size, bases, steps = spec.split(":")
            return cls(name, int(start, 16), int(size, 16), [int(b, 16) for b in bases.split("+")],
                       [(int(o, 16), int(d)) for o, d in (s.split("=") for s in steps.split("+"))])
        except ValueError as e:
            raise SystemExit("bad --layout %r: %s (%s)" % (spec, e, cls.__doc__.strip().splitlines()[-2].strip()))

    @classmethod
    def from_json(cls, d):
        return cls(d["name"], int(d["start"], 16), int(d["size"], 16), [int(b, 16) for b in d["bases"]],
                   [(int(o, 16), int(x)) for o, x in d["field_steps"]])

    def to_json(self):
        return {"name": self.name, "start": "%08X" % self.start, "size": "%X" % self.size,
                "bases": ["%X" % b for b in self.bases],
                "field_steps": [["%X" % o, d] for o, d in self.steps]}

    def delta(self, off):
        """The field shift at `off` (bytes from the object's start)."""
        d = 0
        for o, x in self.steps:
            if off >= o:
                d = x
        return d

    def explains(self, d1, d2):
        """Is displacement d2 (other build) d1 (canonical) moved by this layout, from some base?"""
        for b in self.bases:
            f = b + d1
            if 0 <= f < self.size and d2 == d1 + self.delta(f) - self.delta(b):
                return True
        return False

    def sensitive(self, d):
        """Could a displacement d reach a moved field from one of the bases?"""
        lo = self.steps[0][0]
        return any(lo <= b + d < self.size for b in self.bases)


DFORM = {14} | set(range(32, 56))   # addi, and the D-form loads and stores (integer and float)


def is_layout_dform(w):
    """D-form instruction with a non-zero base register (rA == 0 is literal 0 / li, never a pointer)."""
    return (w >> 26) in DFORM and ((w >> 16) & 0x1F) != 0


def layout_word_equal(w1, w2, layouts):
    if w1 == w2:
        return True
    if (w1 & 0xFFFF0000) != (w2 & 0xFFFF0000) or not is_layout_dform(w1):
        return False
    d1, d2 = sext(w1 & 0xFFFF, 16), sext(w2 & 0xFFFF, 16)
    return any(lay.explains(d1, d2) for lay in layouts)


def layout_body_equal(b1, b2, layouts):
    return len(b1) == len(b2) and all(layout_word_equal(x, y, layouts) for x, y in zip(b1, b2))


def masked(body, layouts):
    """The body with every displacement a layout could have moved masked out: for aligning only."""
    out = []
    for w in body:
        if is_layout_dform(w) and any(lay.sensitive(sext(w & 0xFFFF, 16)) for lay in layouts):
            w &= 0xFFFF0000
        out.append(w)
    return out


def align(canon, other, layouts):
    """Pair the functions of two builds whose function lists differ: some functions only one build
    has, some in another order. Returns ({canonical index: other index}, removed, inserted).

    1. Align the function lists by body (displacements a layout can move masked out).
    2. Inside every stretch that does not align: pair bodies that are equal (layout considered) and
       unique in the stretch, then, in order, functions whose code is mostly the same (a regional
       variant; their sizes may differ); a body that does not change but moved elsewhere is paired
       in a last pass over the whole program.
    What is left has no counterpart: `removed` (only canonical) and `inserted` (only the other build).
    """
    n, m = len(canon.entries), len(other.entries)
    bc = [canon.body(i) for i in range(n)]
    bo = [other.body(j) for j in range(m)]
    mc = [masked(b, layouts) for b in bc]
    mo = [masked(b, layouts) for b in bo]

    def digest(b):
        return hashlib.blake2b(b"".join(w.to_bytes(4, "big") for w in b), digest_size=16).digest()

    sm = difflib.SequenceMatcher(None, [digest(b) for b in mc], [digest(b) for b in mo], autojunk=False)
    pairs, left_c, left_o = {}, [], []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            for k in range(i2 - i1):
                pairs[i1 + k] = j1 + k
            continue
        ci, oj = list(range(i1, i2)), list(range(j1, j2))
        # equal bodies, each the only match of the other in this stretch
        eq = {i: [j for j in oj if layout_body_equal(bc[i], bo[j], layouts)] for i in ci}
        for i in ci:
            if len(eq[i]) == 1 and sum(1 for i2_ in ci if eq[i][0] in eq[i2_]) == 1:
                pairs[i] = eq[i][0]
        ci = [i for i in ci if i not in pairs]
        oj = [j for j in oj if j not in pairs.values()]
        # the rest in order: functions whose code is mostly the same (any size; derive_aligned checks the
        # pairing through calls and data pointers where it can)
        k = 0
        for i in ci:
            for kk in range(k, len(oj)):
                j = oj[kk]
                if difflib.SequenceMatcher(None, mc[i], mo[j], autojunk=False).ratio() >= 0.6:
                    pairs[i] = j
                    k = kk + 1
                    break
        left_c += [i for i in ci if i not in pairs]
        left_o += [j for j in oj if j not in pairs.values()]
    # functions that moved: the same body somewhere else
    by_body = collections.defaultdict(list)
    for j in left_o:
        by_body[digest(mo[j])].append(j)
    for i in list(left_c):
        cand = [j for j in by_body.get(digest(mc[i]), []) if layout_body_equal(bc[i], bo[j], layouts)]
        if len(cand) == 1:
            j = cand[0]
            pairs[i] = j
            left_c.remove(i)
            left_o.remove(j)
            by_body[digest(mo[j])].remove(j)
    # functions rewritten for the region (other size, little code in common): the same call in code
    # that is the same in both builds calls them, so that call pairs them
    entry_c = {canon.entries[i]: i for i in left_c}
    entry_o = {other.entries[j]: j for j in left_o}
    votes = collections.defaultdict(collections.Counter)
    for i, j in pairs.items():
        if not layout_body_equal(bc[i], bo[j], layouts):
            continue
        for k in range(len(bc[i])):
            a, b = canon.entries[i] + 4 * k, other.entries[j] + 4 * k
            w, w2 = canon.p.word(a), other.p.word(b)
            if (w >> 26) != 18 or not (w & 1) or a in canon.p.import_calls:
                continue
            t = (sext(w & 0x03FFFFFC, 26) + (0 if w & 2 else a)) & 0xFFFFFFFF
            t2 = (sext(w2 & 0x03FFFFFC, 26) + (0 if w2 & 2 else b)) & 0xFFFFFFFF
            if t in entry_c and t2 in entry_o:
                votes[entry_c[t]][entry_o[t2]] += 1
    taken = collections.Counter(j for v in votes.values() for j in v)
    for i, v in votes.items():
        if len(v) == 1:
            j = next(iter(v))
            if taken[j] == 1:
                pairs[i] = j
                left_c.remove(i)
                left_o.remove(j)
    return pairs, sorted(left_c), sorted(left_o)


def derive_aligned(canon, other, layouts):
    """The map from `canon` to `other` when they are the same program with some functions added,
    removed or moved, and objects whose fields moved (`layouts`). Mismatch if they are not.

    Checked: every body unique to both builds is paired with its twin; every direct call out of a
    paired function with the same code (layout considered) lands on the mapped address; every data
    address referenced from such a function maps to exactly one address, and the final data map
    (with the layouts' field shifts) gives every one of them; every instruction that differs in a
    function counted as the same code is a displacement a layout explains.

    Then the code pointers in the data sections (vtables, callback tables): a slot maps with the data
    and the function it holds with the code. A function only one build seems to have, held by a slot
    whose counterpart holds a function only the other build seems to have, is the same function
    rewritten (paired, and the map derived again); a slot that holds another function than the map
    gives is a regional table entry, and many of those are refused like wrong calls."""
    pairs, removed, inserted = align(canon, other, layouts)
    result = derive_pairs(canon, other, layouts, pairs, removed, inserted)
    found = pair_through_pointers(canon, other, result, removed, inserted)
    if found:
        pairs.update(found)
        removed = [i for i in removed if i not in found]
        inserted = [j for j in inserted if j not in found.values()]
        result = derive_pairs(canon, other, layouts, pairs, removed, inserted)
    check_pointers(canon, other, result)
    return result


def pair_through_pointers(canon, other, result, removed, inserted):
    """{canonical index: other index} for functions without a counterpart that the same data slot holds."""
    entry_c = {canon.entries[i]: i for i in removed}
    entry_o = {other.entries[j]: j for j in inserted}
    votes = collections.defaultdict(collections.Counter)
    for slot, t in canon.code_pointers.items():
        t2 = other.code_pointers.get(result["_data"](slot))
        if t in entry_c and t2 in entry_o:
            votes[entry_c[t]][entry_o[t2]] += 1
    taken = collections.Counter(j for v in votes.values() for j in v)
    return {i: next(iter(v)) for i, v in votes.items() if len(v) == 1 and taken[next(iter(v))] == 1}


def check_pointers(canon, other, result):
    """Every code pointer in the data sections whose slot both builds have holds the mapped function."""
    maps, checked, elsewhere = result["_maps"], 0, []
    confirmed = set(result["_called"])
    for slot, t in sorted(canon.code_pointers.items()):
        t2 = other.code_pointers.get(result["_data"](slot))
        if t2 is None:
            continue
        checked += 1
        if maps(t) == t2:
            confirmed.add(t)
        else:
            elsewhere.append((slot, t, t2))
    if len(elsewhere) * 100 > checked:
        raise Mismatch("%d of %d code pointers in the data sections do not hold the mapped function, e.g. %s" % (
            len(elsewhere), checked, "; ".join("%08X holds %08X, there %08X" % e for e in elsewhere[:5])))
    s = result["stats"]
    s["pointers_checked"], s["pointers_elsewhere"] = checked, len(elsewhere)
    # differing functions that neither a call from unchanged code nor a data pointer ties to their counterpart:
    # paired by their position and their code's similarity alone
    s["differing_unconfirmed"] = ["%08X" % a for a, _, _, _ in result["differing_functions"] if a not in confirmed]


def derive_pairs(canon, other, layouts, pairs, removed, inserted):
    """derive_aligned for a given pairing."""
    n, m = len(canon.entries), len(other.entries)
    bc = [canon.body(i) for i in range(n)]
    bo = [other.body(j) for j in range(m)]
    same = {i: j for i, j in pairs.items() if bc[i] == bo[j]}
    layout_only = {i: j for i, j in pairs.items() if i not in same and layout_body_equal(bc[i], bo[j], layouts)}
    differs = sorted(i for i in pairs if i not in same and i not in layout_only)
    unchanged = {**same, **layout_only}

    def digest(b):
        return hashlib.blake2b(b"".join(w.to_bytes(4, "big") for w in b), digest_size=16).digest()
    dc, do = [digest(b) for b in bc], [digest(b) for b in bo]
    count_c, count_o = collections.Counter(dc), collections.Counter(do)
    index_o = {h: j for j, h in enumerate(do)}
    anchors = [i for i in range(n) if count_c[dc[i]] == 1 and count_o.get(dc[i]) == 1]
    # a body unique to both builds is paired with its twin, unless the layout explains another pairing:
    # functions for consecutive fields (one per field) whose fields all moved by the field size have
    # the same bytes as the neighbour's in the other build, but their counterpart is the same field
    wrong = [i for i in anchors if pairs.get(i) != index_o[dc[i]] and
             not (i in layout_only and pairs[i] != index_o[dc[i]])]
    if wrong:
        i = wrong[0]
        raise Mismatch("%d of %d functions with a body unique to both builds are not paired with their twin, "
                       "e.g. %08X" % (len(wrong), len(anchors), canon.entries[i]))
    if len(anchors) < n // 8:
        raise Mismatch("only %d of %d functions have a body unique to both builds: too few to tie "
                       "the two together" % (len(anchors), n))
    moved, top = 0, -1   # pairs out of order: a function the other build links elsewhere
    for i in sorted(pairs):
        if pairs[i] < top:
            moved += 1
        top = max(top, pairs[i])

    code_steps = steps_of(sorted((canon.entries[i], other.entries[j]) for i, j in pairs.items()))
    code = piecewise(code_steps)
    def maps(t):
        """Where a canonical code address is in the other build, None if it has no place there."""
        k = canon.func_index(t)
        if k is None or k in removed:
            return None
        if k in differs and t != canon.entries[k]:
            return None
        return code(t)

    # A function whose bytes are the same but one call goes elsewhere (the other build calls a function
    # of its own there) is not the same code: it counts as differing. A few of those are regional
    # code; many would mean the pairing is wrong, which is refused.
    checked = wrong = duplicates = 0
    examples, calls_elsewhere, called = [], set(), set()
    for i, j in unchanged.items():
        for k in range(0, canon.ends[i] - canon.entries[i], 4):
            a = canon.entries[i] + k
            w = canon.p.word(a)
            if (w >> 26) != 18 or not (w & 1) or a in canon.p.import_calls or a in canon.p.undef_calls:
                continue
            t = (sext(w & 0x03FFFFFC, 26) + (0 if w & 2 else a)) & 0xFFFFFFFF
            if not canon.p.in_text(t):
                continue
            b = other.entries[j] + k
            w2 = other.p.word(b)
            t2 = (sext(w2 & 0x03FFFFFC, 26) + (0 if w2 & 2 else b)) & 0xFFFFFFFF
            checked += 1
            if maps(t) == t2:
                called.add(t)
            elif same_function(other, maps(t), t2):
                duplicates += 1   # the other build calls a copy of the function: the same code
            elif maps(t) != t2:
                wrong += 1
                calls_elsewhere.add(i)
                if len(examples) < 5:
                    examples.append("%08X calls %08X, there %08X calls %08X (mapped: %s)" % (
                        a, t, b, t2, "%08X" % maps(t) if maps(t) is not None else "none"))
    if wrong * 1000 > checked or len(calls_elsewhere) > 50:
        raise Mismatch("%d of %d direct calls do not land on the mapped address: %s" % (
            wrong, checked, "; ".join(examples)))
    for i in calls_elsewhere:
        unchanged.pop(i)
        same.pop(i, None)
        layout_only.pop(i, None)
    differs = sorted(set(differs) | calls_elsewhere)

    disps = collections.Counter()
    for i, j in layout_only.items():
        for w1, w2 in zip(bc[i], bo[j]):
            if w1 != w2:
                d1, d2 = sext(w1 & 0xFFFF, 16), sext(w2 & 0xFFFF, 16)
                disps[(d1, d2 - d1)] += 1
    layout_hist = {"%+X (%+d)" % (d1, delta): count
                   for (d1, delta), count in sorted(disps.items(), key=lambda kv: (-kv[1], kv[0]))}

    # data: a relocation in an unchanged function names the same global in both builds
    seen = collections.defaultdict(collections.Counter)
    for a, lst in canon.reloc_target.items():
        i = canon.func_index(a)
        if i is None or i not in unchanged:
            continue
        lst2 = other.reloc_target.get(other.entries[unchanged[i]] + (a - canon.entries[i]))
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
    observed = {d: c.most_common(1)[0][0] for d, c in seen.items()}
    data_steps = with_layouts(steps_of(sorted(observed.items())), layouts)
    data = piecewise(data_steps)
    bad = [d for d, t in observed.items() if data(d) != t]
    if bad:
        raise Mismatch("%d data addresses do not map with the layouts' field shifts, e.g. %08X -> %08X, "
                       "the map says %08X" % (len(bad), bad[0], observed[bad[0]], data(bad[0])))

    return {
        "code_bounds": [canon.p.text_lo, canon.p.text_hi],
        "data_bounds": [min(s.addr for s in canon.p.rpx.sections if s.name in (".rodata", ".data", ".bss")),
                        max(s.addr + s.size for s in canon.p.rpx.sections if s.name in (".rodata", ".data", ".bss"))],
        "code_steps": code_steps,
        "data_steps": data_steps,
        "differing_functions": [[canon.entries[i], canon.ends[i] - canon.entries[i],
                                 other.entries[pairs[i]], other.ends[pairs[i]] - other.entries[pairs[i]]] for i in differs],
        "removed_functions": [[canon.entries[i], canon.ends[i] - canon.entries[i]] for i in removed],
        "inserted_functions": [[other.entries[j], other.ends[j] - other.entries[j]] for j in inserted],
        "layouts": layouts,
        "stats": {"functions": n, "functions_other": m, "anchors": len(anchors), "calls_checked": checked,
                  "data_addresses": len(seen), "same_code": len(same), "layout_only": len(layout_only),
                  "reordered": moved, "calls_to_copies": duplicates,
                  "calls_elsewhere": ["%08X" % canon.entries[i] for i in sorted(calls_elsewhere)],
                  "layout_displacements": layout_hist},
        # for derive_aligned's pointer pass only (not written to the map)
        "_maps": maps, "_data": data, "_called": called,
    }


def same_function(b, a1, a2):
    """Are the functions at a1 and a2 of binary `b` copies: the same instructions, referring to the
    same addresses?"""
    if a1 is None or a2 is None:
        return False
    i, j = b.func_index(a1), b.func_index(a2)
    if i is None or j is None or b.entries[i] != a1 or b.entries[j] != a2:
        return False
    n = b.ends[i] - a1
    if b.ends[j] - a2 != n:
        return False
    for k in range(0, n, 4):
        w1, w2 = b.p.word(a1 + k), b.p.word(a2 + k)
        op = w1 >> 26
        if op in (16, 18) and not (w1 & 2):   # relative branch: compare the target
            if w1 != w2 and not (w1 & 0xFC000003) == (w2 & 0xFC000003):
                return False
            m = 0x03FFFFFC if op == 18 else 0xFFFC
            if (sext(w1 & m, 26 if op == 18 else 16) + a1 + k) != (sext(w2 & m, 26 if op == 18 else 16) + a2 + k):
                if not (a1 <= sext(w1 & m, 26 if op == 18 else 16) + a1 + k < a1 + n):
                    return False
        elif w1 != w2:
            return False
        if b.reloc_target.get(a1 + k) != b.reloc_target.get(a2 + k):
            return False
    return True


def with_layouts(steps, layouts):
    """Data steps with the layouts' field shifts inside their objects: a canonical field address maps
    to the field's address in the other build (GD(object + offset) in the runtime)."""
    if not layouts:
        return steps
    f = piecewise(steps)
    points = dict(steps)
    for lay in layouts:
        base = f(lay.start) - lay.start
        end = lay.start + lay.size
        after = f(end) - end
        inside = [s for s in points if lay.start < s < end]
        if inside:
            raise Mismatch("layout %s: the data map already changes inside the object (at %08X)" % (lay.name, inside[0]))
        points.setdefault(lay.start, base)
        for off, d in lay.steps:
            points[lay.start + off] = base + d
        points.setdefault(end, after)
    out, prev = [], None
    for a, d in sorted(points.items()):
        if d != prev:
            out.append((a, d))
            prev = d
    return out


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
        # only in maps derived with derive_aligned (functions added/removed, moved fields)
        **({"removed_functions": [["%08X" % a, s] for a, s in m["removed_functions"]],
            "inserted_functions": [["%08X" % a, s] for a, s in m["inserted_functions"]],
            "layouts": [lay.to_json() for lay in m["layouts"]]} if "layouts" in m else {}),
    }


MAP_KEYS = ("checked", "code_steps", "data_steps", "differing_functions", "code_bounds", "data_bounds",
            "removed_functions", "inserted_functions", "layouts")


def derive_any(canon, other, layouts=()):
    """The old one-to-one derivation when the two builds have the same functions and no object
    layout moved (the European build: its map stays byte for byte what it was), else derive_aligned."""
    if not layouts and len(canon.entries) == len(other.entries):
        return derive(canon, other)
    return derive_aligned(canon, other, list(layouts))


def report(m):
    s = m["stats"]
    print("functions %d, bodies unique to both builds %d, direct calls checked %d, data addresses %d" % (
        s["functions"], s["anchors"], s["calls_checked"], s["data_addresses"]))
    if "layouts" in m:
        print("other build: %d functions; same code %d, same code but for moved fields %d, out of order %d; "
              "only canonical %d, only the other build %d" % (
                  s["functions_other"], s["same_code"], s["layout_only"], s["reordered"],
                  len(m["removed_functions"]), len(m["inserted_functions"])))
        print("code pointers in the data sections checked %d, holding a regional function %d; differing functions "
              "paired by position and similarity alone: %s" % (
                  s["pointers_checked"], s["pointers_elsewhere"], " ".join(s["differing_unconfirmed"]) or "none"))
        for lay in m["layouts"]:
            print("   layout %s at %08X (+%X bytes), bases %s: %s" % (
                lay.name, lay.start, lay.size, " ".join("+%X" % b for b in lay.bases),
                ", ".join("from +%X %+d" % (o, d) for o, d in lay.steps)))
        hist = s.get("layout_displacements")
        if hist:
            total = sum(hist.values())
            print("   explained displacements: %d word%s (%d distinct)" % (
                total, "" if total == 1 else "s", len(hist)))
            line = ""
            for k, count in hist.items():
                item = "%s: %d" % (k, count)
                if line and len(line) + 2 + len(item) > 96:
                    print("      " + line)
                    line = item
                else:
                    line = item if not line else line + ", " + item
            if line:
                print("      " + line)
        for a, size in m["removed_functions"]:
            print("   only canonical: %08X (%d bytes)" % (a, size))
        for a, size in m["inserted_functions"]:
            print("   only the other build: %08X (%d bytes)" % (a, size))
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
    ap.add_argument("--layout", action="append", default=[], type=Layout.parse,
                    help="an object whose fields moved in the other build (repeatable): "
                         "NAME:START:SIZE:BASE[+BASE..]:OFF=DELTA[+OFF=DELTA..], hex but the deltas; "
                         "see Layout in this file")
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
        layouts = [Layout.from_json(d) for d in have.get("layouts", [])]
        m = derive_any(canon, other, layouts)
        fresh = to_json(m, have["name"], have["title_id"], other.sha256, canon.sha256, have["derived_from"]["name"])
        same = all(fresh.get(k) == have.get(k) for k in MAP_KEYS)
        report(m)
        print("map in %s: %s" % (args.check, "unchanged" if same else "DIFFERS from the two binaries"))
        return 0 if same else 1

    if len(args.rpx) != 2 or not args.name or not args.title:
        ap.error("need CANONICAL.rpx OTHER.rpx --name NAME --title TITLEID")
    canon, other = Binary(args.rpx[0]), Binary(args.rpx[1])
    m = derive_any(canon, other, args.layout)
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
