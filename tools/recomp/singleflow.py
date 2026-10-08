"""Single-precision register halves across the whole game (round25 left out, ppc2c.M).

Within a function, recomp.py follows which FPR halves hold single-precision values (ppc2c.fp_transfer).
Across functions, this module adds:
- entry states: what holds at every way into a function. A function whose address is taken (relocations:
  vtables, function pointers), the program entry, hooked functions (the runtime calls them) and functions
  with no known caller start with nothing known; any other one with what holds at all its direct calls,
  tail branches and the fall-through into it from the function before.
- return summaries: the halves a function always returns as single precision, whatever it was given
  (its exits analysed from its entry state, which every caller satisfies). After an ordinary call the
  caller keeps f14-f31 (the calling convention) and takes the volatile halves from the summary; after a
  register save/restore helper (which does not follow the convention) it keeps every half the helper
  never writes; after an indirect call or an import, f14-f31; after a hooked function, nothing.
Both are solved together as a greatest fixpoint (must-analysis: start from "everything known" and
remove until stable). WWHD_RECOMP_SINGLE_IP=0 keeps the per-function analysis alone.
"""
import os

import ppc2c

INTERPROC = os.environ.get("WWHD_RECOMP_SINGLE_IP", "1") != "0"
TOP = (1 << 64) - 1
SAVED = ppc2c.SAVED_FPRS
VOLATILE = TOP & ~SAVED


def _sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def _fp_written(w):
    """FPR halves an instruction may write (both halves of the destination: an over-approximation)"""
    op = w >> 26
    d = (w >> 21) & 31
    m = 3 << (2 * d)
    if op in (48, 49, 50, 51, 56, 57, 59):
        return m
    if op == 31 and ((w >> 1) & 0x3FF) in (535, 567, 599, 631):
        return m
    if op == 63:
        xo = (w >> 1) & 0x3FF
        if xo in (0, 32, 64, 711, 134, 38, 70):
            return 0
        return m
    if op == 4:
        xo5 = (w >> 1) & 31
        xo = (w >> 1) & 0x3FF
        if xo5 == 7 or xo in (0, 32, 64, 96, 1014):
            return 0
        return m
    return 0


class Func:
    """one function's code, branches, calls and exits"""
    __slots__ = ("start", "end", "words", "succ", "calls", "exits", "falls", "sites", "writes", "callers")

    def __init__(self, rc, start, end):
        p = rc.p
        self.start, self.end = start, end
        n = (end - start) // 4
        self.words = [p.word(start + 4 * i) for i in range(n)]
        self.succ, self.calls, self.exits = [], {}, {}
        self.sites = {(s - start) // 4 for s in rc.sites if start <= s < end}
        self.writes = 0
        self.callers = set()
        self.falls = False
        for i, w in enumerate(self.words):
            a = start + 4 * i
            op, lk = w >> 26, w & 1
            nxt = [i + 1]  # (i + 1 == n: off the end, into the next function)
            self.writes |= _fp_written(w)
            if op in (16, 18):
                t = (_sext(w & 0x03FFFFFC, 26) if op == 18 else _sext(w & 0xFFFC, 16)) + (0 if w & 2 else a)
                t &= 0xFFFFFFFF
                always = op == 18 or ((w >> 21) & 0x14) == 0x14
                if lk:
                    if a in p.import_calls or a in p.undef_calls:
                        self.calls[i] = ("import", None)
                    elif t in rc.hooks:
                        self.calls[i] = ("hook", t)
                    elif t in rc.entries:
                        self.calls[i] = ("direct", t)
                    else:
                        self.calls[i] = ("unknown", None)
                    self.succ.append(nxt)
                elif start <= t < end:
                    self.succ.append(([] if always else nxt) + [(t - start) // 4])
                else:
                    if a in p.import_calls:
                        self.exits[i] = ("import", None)
                    elif t in rc.entries:
                        self.exits[i] = ("tail", t)
                    else:
                        self.exits[i] = ("unknown", None)
                    self.succ.append([] if always else nxt)
            elif op == 19 and ((w >> 1) & 0x3FF) in (16, 528):
                xo = (w >> 1) & 0x3FF
                always = ((w >> 21) & 0x14) == 0x14
                if lk:
                    self.calls[i] = ("indirect", None)
                    self.succ.append(nxt)
                else:
                    out = [] if always else list(nxt)
                    jt = p.jump_tables.get(a) if xo == 528 else None
                    if jt:
                        out += [(jt[0] + 4 * k - start) // 4 for k in range(jt[1]) if start <= jt[0] + 4 * k < end]
                    if xo == 16:
                        self.exits[i] = ("return", None)
                    else:
                        self.exits[i] = ("ijump", None)  # anything left by the table's cases jumps away
                    self.succ.append(out)
            else:
                self.succ.append(nxt)
        # control can run off the end into the next function
        self.falls = n > 0 and n in self.succ[n - 1]
        for i in range(n):
            if n in self.succ[i]:
                self.succ[i] = [j for j in self.succ[i] if j < n]


def runtime_addresses():
    """code addresses written in the runtime's sources (guest_call by address, e.g. mods/climb.cpp):
    functions the runtime may call directly start with nothing known"""
    import re
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "runtime")
    found = set()
    for d, _, files in os.walk(root):
        if "third_party" in d:
            continue
        for name in files:
            if name.endswith((".cpp", ".mm", ".h", ".c")):
                with open(os.path.join(d, name), errors="replace") as f:
                    found.update(int(m, 16) for m in re.findall(r"0x(0[23][0-9A-Fa-f]{6})\b", f.read()))
    return found


class Solver:
    def __init__(self, rc):
        self.rc = rc
        self.funcs = {}
        starts = rc.sorted_entries
        for k, s in enumerate(starts):
            self.funcs[s] = Func(rc, s, rc.func_end(s))
        for s, f in self.funcs.items():
            for kind, t in list(f.calls.values()) + list(f.exits.values()):
                if kind in ("direct", "tail") and t in self.funcs:
                    self.funcs[t].callers.add(s)
            if f.falls and f.end in self.funcs:
                self.funcs[f.end].callers.add(s)
        self.helpers = set(rc.saved_clobbers)
        # halves a helper (and whatever it falls or jumps into) may write
        self.helper_writes = {}
        open_ = set(rc.p.addr_taken) | {rc.p.entry} | set(rc.hooks) | runtime_addresses()
        self.entry = {s: (0 if s in open_ or not f.callers else TOP) for s, f in self.funcs.items()}
        self.ret = {s: TOP for s in self.funcs}

    def helper_w(self, s, seen=None):
        if s in self.helper_writes:
            return self.helper_writes[s]
        seen = seen or set()
        if s in seen or s not in self.funcs:
            return TOP
        seen.add(s)
        f = self.funcs[s]
        w = f.writes
        for kind, t in list(f.calls.values()) + list(f.exits.values()):
            if kind in ("direct", "tail"):
                w |= self.helper_w(t, seen)
            elif kind in ("import", "indirect", "unknown", "hook", "ijump"):
                w = TOP if kind != "ijump" else w
        if f.falls:
            w |= self.helper_w(f.end, seen)
        self.helper_writes[s] = w
        return w

    def after_call(self, state, kind, t):
        """the halves known after a call of the given kind"""
        if kind == "direct":
            if t in self.helpers:
                return state & ~self.helper_w(t)
            return (state & SAVED) | (self.ret.get(t, 0) & VOLATILE)
        if kind in ("import", "indirect"):
            return state & SAVED
        return 0  # hook, unknown

    def run(self, f, entry):
        """forward dataflow over f from `entry`: the state before each instruction, the states at its
        direct calls, tail branches and fall-through (target -> state), and its return state"""
        n = len(f.words)
        state = [TOP] * n
        reached = [False] * n
        if n == 0:
            return [], {}, TOP
        state[0] = 0 if 0 in f.sites else entry
        reached[0] = True
        work = [0]
        while work:
            i = work.pop()
            w = f.words[i]
            s = state[i]
            call = f.calls.get(i)
            out = self.after_call(s, *call) if call else ppc2c.fp_transfer(s, w, True)
            for j in f.succ[i]:
                new = 0 if j in f.sites else (state[j] & out if reached[j] else out)
                if not reached[j] or new != state[j]:
                    reached[j] = True
                    state[j] = new
                    work.append(j)
        sites, ret = {}, TOP

        def site(t, st):
            sites[t] = sites.get(t, TOP) & st

        for i in range(n):
            if not reached[i]:
                state[i] = 0
                continue
            s = state[i]
            call = f.calls.get(i)
            if call and call[0] == "direct":
                site(call[1], s)
            ex = f.exits.get(i)
            if ex:
                kind, t = ex
                if kind == "return":
                    ret &= s
                elif kind == "tail":
                    site(t, s)
                    ret &= self.after_call(s, "hook" if t in self.rc.hooks else "direct", t)
                elif kind == "ijump":
                    ret &= s & SAVED
                else:
                    ret &= 0
        if f.falls and reached[n - 1]:
            w = f.words[n - 1]
            call = f.calls.get(n - 1)
            out = self.after_call(state[n - 1], *call) if call else ppc2c.fp_transfer(state[n - 1], w, True)
            site(f.end, out)
            ret &= self.after_call(out, "direct", f.end)
        return state, sites, ret

    def solve(self):
        work = list(self.funcs)
        queued = set(work)
        rounds = 0
        while work:
            s = work.pop()
            queued.discard(s)
            rounds += 1
            f = self.funcs[s]
            _, sites, ret = self.run(f, self.entry[s])
            if ret != self.ret[s]:
                self.ret[s] = ret
                for c in f.callers:
                    if c not in queued:
                        queued.add(c)
                        work.append(c)
            # a callee's entry: what holds at all its call sites (recomputed from every caller's sites)
            for t, st in sites.items():
                if t not in self.funcs or self.entry[t] == 0:
                    continue
                new = self.entry[t] & st
                if new != self.entry[t]:
                    self.entry[t] = new
                    if t not in queued:
                        queued.add(t)
                        work.append(t)
        self.rounds = rounds
        return self

    def states(self, start):
        """the state before each instruction of the function, for the translation"""
        f = self.funcs[start]
        return self.run(f, self.entry[start])[0]
