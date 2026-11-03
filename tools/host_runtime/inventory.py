#!/usr/bin/env python3
"""Inventory of the port-owned guest runtime (task-lib2.md §0.2): the GHS C/C++ runtime and the nn
static wrappers, which are not decomp units and get host replacements.

usage: inventory.py --gen GEN_DIR --rpx cking.rpx --board task-lib2.md --decomp DECOMP_GIT_DIR
                    [--decomp-ref decomp-readable] [--trace TRACE.tsv ...] > inventory.tsv

The board and the decomp are only read for their address lists (which functions are someone else's);
nothing else from them reaches the output. Names come from names.py.

The set is derived, not listed by hand: every recompiled function (GEN_DIR/table.c) that is neither
decompiled (a "/* ADDR */" marker directly above a definition in the decomp branch) nor a row of the
stage-2 board, restricted to the two port-owned blocks below. Anything else left over is printed to
stderr so a change in the board or the decomp shows up.

Facts per function: size, static callers (direct and tail calls in the recompiled C) inside and outside
the block, 32-bit references from the RPX data sections (vtables, function-pointer tables), imports it
calls, whether the port already hooks it (tools/recomp/hooks*.txt) and an optional runtime call count
(--trace: the TSV a WWHD_HOSTRT_TRACE run writes, runtime/src/trace.cpp).
"""
import argparse, bisect, collections, glob, os, re, struct, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from names import name_of  # noqa: E402

BLOCKS = [  # (name, first, end): end exclusive
    ("nn", 0x0286C590, 0x0286DB50),   # nn static wrappers (before nw4f ut 0286DB50)
    ("ghs", 0x028EA038, 0x028F626C),  # GHS C/C++ runtime (after the SDK gfd/mtx code, before the nw4f ut tail)
]

def block_of(a):
    for n, lo, hi in BLOCKS:
        if lo <= a < hi:
            return n
    return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", required=True)
    ap.add_argument("--rpx", required=True)
    ap.add_argument("--board", required=True)
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--decomp-ref", default="decomp-readable")
    ap.add_argument("--hooks-dir", default=os.path.join(os.path.dirname(__file__), "..", "recomp"))
    ap.add_argument("--trace", nargs="*", default=[])
    ap.add_argument("--expect", type=int, help="fail unless exactly this many port-owned functions and no strays")
    a = ap.parse_args()

    funcs = sorted(int(x, 16) for x in re.findall(r"\{0x([0-9A-F]{8})u, f_", open(os.path.join(a.gen, "table.c")).read()))
    nxt = {f: (funcs[i + 1] if i + 1 < len(funcs) else f) for i, f in enumerate(funcs)}

    # decompiled: a marker line followed by a definition (not a call-site binding comment "/* ADDR [v ...] */")
    src = subprocess.run(["git", "-C", a.decomp, "grep", "-h", "-E", "-A1", r"^/\* 0[23][0-9A-F]{6} \*/$", a.decomp_ref, "--", "wwhd_src"],
                         capture_output=True, text=True, check=True).stdout.splitlines()
    done = set()
    for i, l in enumerate(src):
        m = re.match(r"^(?:[^:]+:)?/\* (0[23][0-9A-F]{6}) \*/$", l)
        if m:
            done.add(int(m.group(1), 16))
    # the second marker style (as tools/verify/mkunit.py scans it): VERIFY(0xADDR, fn) after a candidate,
    # WWHD_FUNC(0xADDR, ...) as the first statement of one
    src = subprocess.run(["git", "-C", a.decomp, "grep", "-h", "-E", r"^\s*(VERIFY|WWHD_FUNC)\(\s*0x[0-9A-Fa-f]{8}\s*,",
                          a.decomp_ref, "--", "wwhd_src"], capture_output=True, text=True, check=True).stdout.splitlines()
    for l in src:
        m = re.match(r"^(?:[^:]+:)?\s*(?:VERIFY|WWHD_FUNC)\(\s*0x([0-9A-Fa-f]{8})\s*,", l)
        if m:
            done.add(int(m.group(1), 16))
    board = set(int(x, 16) for x in re.findall(r"^\| (0[23][0-9A-F]{6}) \|", open(a.board).read(), re.M))
    resid = [f for f in funcs if f not in done and f not in board]
    owned = [f for f in resid if block_of(f)]
    for f in resid:
        if not block_of(f):
            print("note: %08X is neither decompiled nor on the board, outside the port-owned blocks" % f, file=sys.stderr)
    strays = [f for f in resid if not block_of(f)]
    print("%d port-owned functions (%s), %d strays" % (len(owned), ", ".join(
        "%s %d" % (n, sum(1 for f in owned if block_of(f) == n)) for n, _, _ in BLOCKS), len(strays)), file=sys.stderr)
    if a.expect is not None and (len(owned) != a.expect or strays):
        sys.exit("inventory: expected %d port-owned functions and no strays" % a.expect)
    for f in funcs:
        if block_of(f) and f not in owned:
            print("note: %08X inside a port-owned block is %s" % (f, "decompiled" if f in done else "on the board"), file=sys.stderr)

    # static call graph from the recompiled C
    callers = collections.defaultdict(set)
    imports = collections.defaultdict(set)
    calls = collections.defaultdict(set)
    for path in sorted(glob.glob(os.path.join(a.gen, "code_*.c"))):
        cur, prev = None, ""
        for line in open(path):
            m = re.match(r"void f_([0-9A-F]{8})\(Cpu", line)
            if m:
                cur, prev = int(m.group(1), 16), ""
                continue
            if cur is None:
                continue
            s = line.strip()
            dead = s.startswith("MUSTTAIL return f_") and prev == "return;"
            if not dead:
                for t in re.findall(r"\bf_([0-9A-F]{8})\(c\)", s):
                    t = int(t, 16)
                    if t != cur:
                        callers[t].add(cur)
                        calls[cur].add(t)
                for imp in re.findall(r"\bimp_([A-Za-z0-9_]+)\(c\)", s):
                    imports[cur].add(imp)
            prev = s.split("/*")[0].strip()
    # address references from the relocations: ADDR32 in data (vtables, function-pointer tables) and
    # ADDR16_LO in code (an address built with lis/addi, e.g. a callback); REL24 calls are in the C already
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    import rpx as rpxmod
    owned_set = set(owned)
    dref, cref = collections.Counter(), collections.defaultdict(set)
    import bisect
    for sec, off, typ, sym, addend in rpxmod.Rpx(a.rpx).relocs:
        v = (sym.value + addend) & 0xFFFFFFFF
        if v not in owned_set:
            continue
        if typ == rpxmod.R_PPC_ADDR32:
            dref[v] += 1
        elif typ == rpxmod.R_PPC_ADDR16_LO:
            i = bisect.bisect_right(funcs, off) - 1
            cref[v].add(funcs[i] if i >= 0 else off)
    hooked = set()
    for hf in glob.glob(os.path.join(a.hooks_dir, "hooks*.txt")):
        for l in open(hf):
            m = re.match(r"^\s*([0-9A-Fa-f]{8})\b", l)
            if m:
                hooked.add(int(m.group(1), 16))
    trace, trace_lr = collections.Counter(), collections.defaultdict(set)
    for t in a.trace:
        for l in open(t):
            p = l.rstrip("\n").split("\t")
            if len(p) >= 2 and re.match(r"[0-9A-Fa-f]{8}$", p[0]):
                trace[int(p[0], 16)] += int(p[1])
                if len(p) > 2:
                    trace_lr[int(p[0], 16)].update(x for x in p[2].split(",") if x)

    # ELF entry point: the C runtime's start-up code (runs main, then exit)
    entry_point = struct.unpack_from(">I", open(a.rpx, "rb").read(), 0x18)[0]
    blk = {f: block_of(f) for f in owned}
    def outside(c, f):
        return block_of(c) != blk[f]
    # how each function is reached: "game" (called or address-taken from outside its block), "data"
    # (only through data: vtables, tables), "entry" (the RPX entry point), else through other owned
    # functions only ("internal"), or not at all ("unreached")
    kind = {}
    for f in owned:
        if f == entry_point:
            kind[f] = "entry"
        elif any(outside(c, f) for c in callers[f] | cref[f]):
            kind[f] = "game"
        elif dref[f]:
            kind[f] = "data"
    edges = {f: set(calls[f]) | {t for t in owned if f in cref[t]} for f in owned}
    roots = [f for f in owned if f in kind]
    reached_from = collections.defaultdict(set)
    for r in roots:
        st, seen = [r], {r}
        while st:
            x = st.pop()
            for y in edges.get(x, ()):
                if y in blk and y not in seen:
                    seen.add(y)
                    st.append(y)
        for y in seen:
            reached_from[y].add(r)
    for f in owned:
        if f not in kind:
            kind[f] = "internal" if reached_from[f] else "unreached"

    print("addr\tsize\tblock\treach\tcallers_out\tcallers_in\tdata_refs\tcode_addr_refs\tcallees\timports\thooked\ttrace_count\ttrace_lr\tgroup\tname")
    for f in owned:
        out = sorted(c for c in callers[f] if outside(c, f))
        inn = sorted(c for c in callers[f] if not outside(c, f))
        print("%08X\t%d\t%s\t%s\t%d\t%d\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s" % (
            f, nxt[f] - f, blk[f], kind[f], len(out), len(inn), dref[f], ",".join("%08X" % x for x in sorted(cref[f])),
            ",".join("%08X" % x for x in sorted(calls[f])), ",".join(sorted(imports[f])),
            "yes" if f in hooked else "", trace.get(f, "") if a.trace else "", ",".join(sorted(trace_lr[f])) if a.trace else "",
            name_of(f)[1], name_of(f)[0]))

if __name__ == "__main__":
    main()
