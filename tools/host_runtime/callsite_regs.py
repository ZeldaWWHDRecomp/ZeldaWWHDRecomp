#!/usr/bin/env python3
"""Which registers do the callers of a port-owned root read after the call?

A host replacement (hook) leaves every register as it was on entry except its declared outputs. That is
only exact if no caller reads a register the original left behind as a side effect: this checks every
call site in the plain-form recompiled code (WWHD_RECOMP_PLAIN=1) with a liveness analysis over GPRs,
FPRs, CR bits, CTR and XER[CA].

usage: callsite_regs.py GEN_PLAIN_DIR inventory.tsv --rpx=cking.rpx [ADDR ...] > callsite_regs.tsv
Output per root: callers analysed, tail calls, and the registers live after the call (outputs included,
so the caller's use of r3/r4/f1 shows too). Indirect callers (vtables, function pointers) cannot be
analysed here and are counted separately.
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "verify"))
import funcdb  # noqa: E402

CR_RE = re.compile(r"c->cr\[(\d+)\](\s*=(?!=))?")
CRSET_RE = re.compile(r"cr_set_\w+\(c, (\d+),")
SPR_RE = re.compile(r"c->(ctr|xer_ca)\b(\s*=(?!=))?")
RET_USE = {("r", 3), ("r", 4), ("f", 1), ("f", 2)}
VOL_CR = {("cr", b) for f in (0, 1, 5, 6, 7) for b in range(4 * f, 4 * f + 4)}
SCRATCH = funcdb.VOLATILE | VOL_CR | {("ctr", 0), ("xer_ca", 0)}


CRCLR_RE = re.compile(r"c->cr\[(\d+)\] = c->cr\[\1\] \^ c->cr\[\1\]")


def extra(stmt):
    u, d = set(), set()
    for piece in stmt.split(";"):
        m = CRCLR_RE.search(piece)
        if m:  # crxor b,b,b (crclr): writes 0, reads nothing
            d.add(("cr", int(m.group(1))))
            continue
        pd = set()
        for m in CR_RE.finditer(piece):
            r = ("cr", int(m.group(1)))
            if m.group(2):
                pd.add(r)
            elif r not in d:
                u.add(r)
        for m in CRSET_RE.finditer(piece):
            f = int(m.group(1))
            pd |= {("cr", b) for b in range(4 * f, 4 * f + 4)}
        if "cr0_rc(c," in piece:  # record forms (or., add., ...) set CR0
            pd |= {("cr", b) for b in range(4)}
        for m in SPR_RE.finditer(piece):
            r = (m.group(1), 0)
            if m.group(2):
                pd.add(r)
            elif r not in d:
                u.add(r)
        d |= pd
    return u, d


IMPORT_DEFS = {("r", 3), ("r", 4), ("f", 1)}


def root_defs(df, gi, addr, seen):
    """Registers (GPR, FPR, CR bits, CTR, XER[CA]) the original root writes, with its callees. Imports
    (the port's HLE functions) write only their result registers; an indirect call may write any
    volatile register."""
    if addr in seen or addr not in gi.loc:
        return set()
    seen.add(addr)
    g = df.cfg(addr)
    d = set()
    for k in g.reach:
        s = g.insns[k][1]
        d |= g.defs[k] | extra(s)[1]
        for t in g.calls[k]:
            d |= root_defs(df, gi, t, seen)
        if "ppc_dispatch(c)" in s:
            d |= SCRATCH
        elif g.indirect[k]:
            d |= IMPORT_DEFS
    return d


def live_after(df, caller, root, unknown_reads_args=True, ret_use=None):
    g = df.cfg(caller)
    n = len(g.insns)
    use, defs, sites, tails = [], [], [], []
    for k, (a, s) in enumerate(g.insns):
        u, d = set(g.use[k]), set(g.defs[k])
        eu, ed = extra(s)
        u |= eu - d
        d |= ed
        if k in g.reach:
            for t in g.calls[k]:
                if t == root:
                    (tails if s.startswith("MUSTTAIL") or "MUSTTAIL" in s else sites).append(k)
                else:
                    u |= df.livein(t) - d
                    d |= df.maydef(t)
            if g.indirect[k]:
                if unknown_reads_args:
                    u |= funcdb.ARGS - d
                d |= funcdb.VOLATILE
            if s.startswith(("return;", "MUSTTAIL return")) and not g.calls[k]:
                u |= RET_USE if ret_use is None else ret_use
        use.append(u)
        defs.append(d)
    live = [set() for _ in range(n)]
    changed = True
    while changed:
        changed = False
        for k in range(n - 1, -1, -1):
            out = set()
            for j in g.succ[k]:
                out |= live[j]
            if k in sites:
                # the call to the root: what is live after it is what the caller reads of the root's result
                new = use[k] | out
            else:
                new = use[k] | (out - defs[k])
            if new != live[k]:
                live[k] = new
                changed = True
    after = set()
    for k in sites:
        for j in g.succ[k]:
            after |= live[j]
    if tails:
        after |= RET_USE if ret_use is None else ret_use
    return after, len(sites), len(tails)


def fmt(regs):
    def key(r):
        return ("r", "f", "cr", "ctr", "xer_ca").index(r[0]), r[1]
    return ",".join("%s%d" % r if r[0] in ("r", "f", "cr") else r[0] for r in sorted(regs, key=key))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--rpx=")]
    rpx_path = next((a[6:] for a in sys.argv[1:] if a.startswith("--rpx=")), None)
    gen, inv = args[0], args[1]
    rows = [l.rstrip("\n").split("\t") for l in open(inv)]
    head, rows = rows[0], rows[1:]
    col = {h: i for i, h in enumerate(head)}
    want = {int(a, 16) for a in args[2:]}
    roots = [int(r[0], 16) for r in rows if r[col["reach"]] != "internal" and (not want or int(r[0], 16) in want)]
    gi = funcdb.GenIndex(gen)
    df = funcdb.Dataflow(gi)
    df_strict = funcdb.Dataflow(gi, unknown_reads_args=False)
    callers = {r: set() for r in roots}
    callers_of = {}
    for a in gi.addrs:
        txt = gi.text(a)
        for m in funcdb.CALL_RE.finditer(txt):
            t = int(m.group(1), 16)
            if t != a:
                callers_of.setdefault(t, set()).add(a)
            if t in callers and t != a:
                callers[t].add(a)
    def special_defs(addr, seen):
        # CR bits, CTR and XER[CA] the original (with its callees) may write
        if addr in seen or addr not in gi.loc:
            return set()
        seen.add(addr)
        g = df.cfg(addr)
        d = set()
        for k in g.reach:
            d |= extra(g.insns[k][1])[1]
            for t in g.calls[k]:
                d |= special_defs(t, seen)
            if g.indirect[k]:
                d |= VOL_CR | {("ctr", 0), ("xer_ca", 0)}
        return d

    # what the callers of a function read of its result registers after it returns: the union over its
    # call sites (strict liveness, recursively for the callers' own returns); a function whose address
    # is taken (vtables, pointers) or that is reached by a tail call or has no known caller keeps the
    # conservative r3, r4, f1, f2
    import struct as _struct
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    import rpx as rpxmod
    taken = set()
    if rpx_path:
        for sec, off, typ, sym, add in rpxmod.Rpx(rpx_path).relocs:
            if typ in (rpxmod.R_PPC_ADDR32, rpxmod.R_PPC_ADDR16_LO):
                taken.add((sym.value + add) & 0xFFFFFFFF)
    memo, busy = {}, set()

    def ret_reads(f, depth):
        if f in memo:
            return memo[f]
        if f in taken or depth > 6 or f in busy or not callers_of.get(f):
            return set(RET_USE)
        busy.add(f)
        r = set()
        for c in callers_of[f]:
            v, s_, t_ = live_after(df_strict, c, f, False, ret_reads(c, depth + 1))
            r |= v & RET_USE
        busy.discard(f)
        if depth == 0 or not (r - RET_USE):
            memo[f] = r
        return r

    print("addr\tcallers\tsites\ttail_calls\tlive_after\tlive_scratch\tclobbered_and_read\tclobbered_and_read_strict")
    for r in roots:
        allv, hard, ns, nt = set(), set(), 0, 0
        for c in sorted(callers[r]):
            v, s, t = live_after(df, c, r)
            allv |= v
            # strict: imports and indirect calls read no argument registers (a register is then live
            # only if the caller's own code or a known callee reads it)
            hard |= live_after(df_strict, c, r, False, ret_reads(c, 1))[0]
            ns += s
            nt += t
        # the registers a caller reads that the original root may have written: the replacement must
        # produce these exactly (outputs such as r3/r4/f1 show up here too)
        clob = (allv & df.maydef(r)) | (allv & special_defs(r, set()))
        clob_h = hard & root_defs(df, gi, r, set())
        print("%08X\t%d\t%d\t%d\t%s\t%s\t%s\t%s" % (r, len(callers[r]), ns, nt, fmt(allv), fmt(allv & SCRATCH),
                                                    fmt(clob & SCRATCH), fmt(clob_h & SCRATCH)))


if __name__ == "__main__":
    main()
