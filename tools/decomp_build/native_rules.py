#!/usr/bin/env python3
"""Decomp build: which decompiled functions may later be called natively (plain C++ calls between decompiled
functions, without a guest frame; native-calls design, phase 1). This only writes the rules; nothing uses them yet.

usage: native_rules.py DECOMP_DIR OUT_DIR GEN_DIR [strict]

Reads OUT_DIR (gen.py's outputs: functions.tsv, excluded.txt, units.json, verify/image.bin when the sources are
glued), GEN_DIR/table.c (the recompiler's function, import, kept and import-call tables) and the decompilation's
unit specs (framexact / footprint lines). Writes OUT_DIR/native.txt, one line per decompiled function:

  ADDR  callee=native|guest  caller=ok|guest  REASONS  # unit name

callee=native: a decompiled caller may call it without a guest frame. Any rule that fails or cannot be decided
keeps the guest call (the reasons say which):
  C1 kept         the recompiler kept it (an instruction hook inside, or not a function entry)
  C2 runtime hook the port hooks it (it is not listed: gen.py excluded it)
  C3 framexact    a unit spec marks it frame-exact (cross-build save states compare its frame)
  C4 may park     only with `strict` (WWHD_DECOMP_PARK_RULE): a path of direct calls (bl, or a tail b into another
                  function or an import stub) or any indirect call (bctrl) reaches a blocking import. By default it is
                  noted (`(may park)`) but no reason: a native callee runs at the r1 the guest path gives it and,
                  like a guest-entered function without an explicit AutoFrame (those stay guest calls), writes no
                  guest frame, so a parked thread's guest stack is the same either way (lead ruling 2026-11-03;
                  the runtime's park guard logs such parks as a diagnostic)
  C5 footprint    `footprint FUNC SIZE` with SIZE > 0, or src:nativeframe (stale stack bytes reach game data)
  C6 src:local    its body has gabi::Local / Stack / LocalOf / FrameLocal (strict rule)
  C7 src:cpu      its body reads or writes Cpu state (cpu->, gabi::cpu, c->r/f/lr/ctr/cr)
  C8 src:call_site / src:tail_ptr
  C9 stack args (more than 8 integer or 8 float arguments), src:va, orig:caller-frame-read, orig:backchain/lr
  C10 src:jmp     setjmp/longjmp, OSSaveContext/OSLoadContext, __builtin_frame/return_address
  C11 no body     its WWHD_FUNC / WWHD_GLUE_FUNC was not found in the compiled sources
  C12 src:ret     a result that tools/decomp_build/native_returns.py did not rewrite (not a scalar or pointer, or a
                  lambda / local class in the body): the guest registers would miss it
caller=guest: its own calls must stay guest calls (A1: framexact or footprint; A2: src:cpu). Per call site rules
(A3: stack arguments at that call) are decided where the call is compiled.
"""
import bisect
import collections
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

BLOCKING = re.compile(r"^(OSReceiveMessage|OSSendMessage|OSJamMessage|OSWaitEvent|OSWaitEventWithTimeout|OSLockMutex|"
                      r"OSWaitCond|OSSleepThread|OSSleepTicks|OSJoinThread|OSWaitRendezvous|OSWaitRendezvousWithTimeout|"
                      r"OSSuspendThread|OSWaitSemaphore|OSWaitAlarm|OSFastMutex_Lock|OSFastCond_Wait|GX2WaitForVsync|"
                      r"GX2WaitForFlip|GX2DrawDone|GX2WaitTimeStamp|OSYieldThread|OSExitThread)$")
SRCF = [("src:local", re.compile(r"gabi::(Local|Stack|LocalOf|FrameLocal)\b|\bLocalOf<|\bStack<")),
        ("src:nativeframe", re.compile(r"NativeFrame<")),
        ("src:cpu", re.compile(r"\bcpu->|gabi::cpu\b|\bc->(r|f|lr|ctr|cr)\b")),
        ("src:call_site", re.compile(r"call(_ptr)?_site\s*[<(]")),
        ("src:tail_ptr", re.compile(r"tail_ptr\s*[<(]")),
        ("src:va", re.compile(r"\bva_(list|start|arg)\b|\.\.\.\s*\)")),
        ("src:jmp", re.compile(r"setjmp|longjmp|OSLoadContext|OSSaveContext|__builtin_(frame|return)_address")),
        ("src:autoframe", re.compile(r"\bAutoFrame\b"))]
FUNC = re.compile(r"WWHD_(GLUE_)?FUNC(_R)?\(\s*0x([0-9A-Fa-f]{8})\s*,")
LCALL = re.compile(r"\bgabi::lcall<0x([0-9A-Fa-f]{8}),")
FLOAT_T = re.compile(r"^(?:const\s+)?(?:f32|f64|float|double)$")


def read_image(path):
    """verify/image.bin: (addr u32 LE, size u32 LE, bytes) records -> sorted [(addr, bytes)]"""
    d = open(path, "rb").read()
    secs, p = [], 0
    while p + 8 <= len(d):
        a, n = struct.unpack_from("<II", d, p)
        p += 8
        secs.append((a, d[p:p + n]))
        p += n
    return sorted(secs)


def code_words(secs, lo, hi):
    for a, d in secs:
        if a <= lo and hi <= a + len(d):
            return struct.unpack_from(">%dI" % ((hi - lo) // 4), d, lo - a)
    return ()


def read_table(path):
    """GEN_DIR/table.c -> (function entries sorted, {import slot: name}, {call site: import slot}, kept set)"""
    tab = open(path, encoding="utf-8", errors="replace").read()
    funcs = sorted(int(x, 16) for x in re.findall(r"\{0x([0-9A-F]{8})u, f_[0-9A-F]{8}\}", tab))
    imports = {int(a, 16): n for a, n in re.findall(r'\{0x([0-9A-F]{8})u, 0x[0-9A-F]{8}u, "[^"]*", "([^"]*)"', tab)}
    m = re.search(r"g_recomp_decomp_import_calls\[\]\[2\] = \{(.*?)\};", tab, re.S)
    icalls = {int(a, 16): int(b, 16) for a, b in re.findall(r"\{0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u\}", m.group(1))} if m else {}
    m = re.search(r"g_recomp_decomp_kept\[\] = \{(.*?)\};", tab, re.S)
    kept = {int(x, 16) for x in re.findall(r"0x([0-9A-F]{8})u", m.group(1))} if m else set()
    return funcs, imports, icalls, kept


def scan_code(secs, funcs, icalls):
    """the original code of every function -> (bl targets, tail-b targets into other functions, features)"""
    fset = set(funcs)
    calls, tails, feat = {}, {}, collections.defaultdict(set)
    for i, a in enumerate(funcs):
        e = funcs[i + 1] if i + 1 < len(funcs) else a + 4
        if e - a > 0x40000:
            e = a + 4
        ws = code_words(secs, a, e)
        cl, tl, frame, mflr = [], [], 0, 0
        if any(w >> 26 == 19 and ((w >> 1) & 0x3FF) == 528 and w & 1 for w in code_words(secs, a, e)):
            feat[a].add("orig:bctrl")
        for k, w in enumerate(ws):
            ea = a + 4 * k
            op = w >> 26
            if op in (16, 18) and not w & 1 and ea in icalls:
                # a tail branch (b / bc) into an import stub (sead CriticalSection::lock: `b OSLockMutex`): the import
                # runs as if called from here
                cl.append(icalls[ea])
            elif op == 18:
                li = w & 0x03FFFFFC
                if li & 0x02000000:
                    li -= 0x04000000
                t = (li if w & 2 else ea + li) & 0xFFFFFFFF
                if w & 1:
                    cl.append(icalls.get(ea, t))
                elif t in fset and not a <= t < e:
                    tl.append(t)
            elif op == 37 and ((w >> 21) & 31) == 1 and ((w >> 16) & 31) == 1 and not frame:  # stwu r1, -N(r1)
                frame = -((w & 0xFFFF) - 0x10000 if w & 0x8000 else (w & 0xFFFF))
            elif w & 0xFC1FFFFF == 0x7C0802A6:  # mflr rD
                mflr += 1
            elif op in (32, 34, 40, 42, 48, 50) and ((w >> 16) & 31) == 1:  # loads d(r1)
                d = w & 0xFFFF
                d = d - 0x10000 if d & 0x8000 else d
                rd = (w >> 21) & 31
                if op == 32 and d == 0 and rd != 1:
                    feat[a].add("orig:backchain/lr")
                elif d >= frame + 8 and not (op == 32 and d == frame + 4):
                    feat[a].add("orig:caller-frame-read")
        if mflr > 1:
            feat[a].add("orig:backchain/lr")
        calls[a], tails[a] = cl, tl
    return calls, tails, feat


def may_park(funcs, imports, calls, tails, indirect=()):
    """functions from which a blocking import is reachable; `indirect`: functions with a bctrl (their targets are
    unknown: counted as reaching one)"""
    park = {a for a in funcs if any(t in imports and BLOCKING.match(imports[t]) for t in calls[a])} | set(indirect)
    callers = collections.defaultdict(set)
    for a in funcs:
        for t in calls[a] + tails[a]:
            callers[t].add(a)
    work = list(park)
    while work:
        x = work.pop()
        for c in callers[x]:
            if c not in park:
                park.add(c)
                work.append(c)
    return park


def split_args(s):
    """the top-level comma-separated items of a macro argument list"""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "(<[{":
            depth += 1
        elif ch in ")>]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def source_features(paths):
    """{addr: (features, (int args, float args))} from the bodies of WWHD_FUNC / WWHD_GLUE_FUNC functions"""
    out = {}
    for p in paths:
        try:
            t = open(p, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for m in FUNC.finditer(t):
            addr = int(m.group(3), 16)
            # the macro's arguments, then the body: from the last '{' before it to its matching '}'
            depth, k = 1, t.index("(", m.start()) + 1
            while k < len(t) and depth:
                depth += {"(": 1, ")": -1}.get(t[k], 0)
                k += 1
            allargs = split_args(t[t.index("(", m.start()) + 1:k - 1])
            margs = allargs[2:]  # after addr, R
            o = t.rfind("{", 0, m.start())
            depth, j = 0, o
            while j < len(t):
                depth += {"{": 1, "}": -1}.get(t[j], 0)
                if depth == 0:
                    break
                j += 1
            head = t[t.rfind("\n", 0, max(0, t.rfind(")", 0, o))) + 1:o]
            body = t[k:j]
            fs = set(nm for nm, rx in SRCF if rx.search(body) or (nm == "src:va" and rx.search(head)))
            if m.group(1) and not m.group(2) and len(allargs) > 1 and allargs[1].strip() != "void":
                fs.add("src:ret")  # a result native_returns.py did not hand to NativeScope (gabi.h: guest calls only)
            if not m.group(1):
                fs.add("src:wwhd_func")  # WWHD_FUNC (not the GameCube-style entry): its automatic frame, no native form
            # parameter types from the function head: floats by type, the rest (this, pointers, integers) by count
            params = split_args(head[head.rfind("(", 0, head.rfind(")")) + 1:head.rfind(")")]) if "(" in head else []
            nf = sum(1 for x in params if FLOAT_T.match(" ".join(x.split()[:-1]) if len(x.split()) > 1 else x))
            out.setdefault(addr, (fs, (max(0, len(margs) - nf), nf)))
    return out


def spec_values(src, directive):
    """{FUNC: int(VALUE)} of every `directive FUNC VALUE` line of the gated unit specs (gen.py unit_specs)"""
    import gen
    rx = re.compile(r"^\s*%s\s+([0-9A-Fa-f]{8})\s+(\S+)" % directive)
    out = {}
    for _, lines in gen.unit_specs(src):
        for line in lines:
            m = rx.match(line.split("#")[0])
            if m:
                out[int(m.group(1), 16)] = int(m.group(2), 16 if not m.group(2).startswith("0x") else 0)
    return out


def rules(src, out, gen_dir, strict=False):
    dec = {}
    for line in open(os.path.join(out, "functions.tsv"), encoding="utf-8"):
        a, u, n = line.rstrip("\n").split("\t")
        dec[int(a, 16)] = (u, n)
    funcs, imports, icalls, kept = read_table(os.path.join(gen_dir, "table.c"))
    img = os.path.join(out, "verify", "image.bin")
    secs = read_image(img) if os.path.isfile(img) else []
    calls, tails, feat = scan_code(secs, funcs, icalls)
    park = may_park(funcs, imports, calls, tails, [a for a in funcs if "orig:bctrl" in feat.get(a, ())]) if secs else None
    fx = set(spec_values(src, "framexact"))
    fp = spec_values(src, "footprint")
    uj = json.load(open(os.path.join(out, "units.json"), encoding="utf-8"))
    srcf = source_features([s["path"] for u in uj["units"] for s in u["sources"]])
    lines = []
    for a in sorted(dec):
        r = []
        if a in kept:
            r.append("kept")
        if a in fx:
            r.append("framexact")
        note = []
        if park is None:
            (r if strict else note).append("may park (no code image)")
        elif a in park:
            (r if strict else note).append("may park")
        if fp.get(a, 0):
            r.append("footprint")
        fs, (ni, nf) = srcf.get(a, (None, (0, 0)))
        if fs is None:
            r.append("no body")
        else:
            r += sorted(fs)
        if ni > 8 or nf > 8:
            r.append("stack args")
        r += sorted(x for x in feat.get(a, ()) if x != "orig:bctrl")
        caller = []
        if a in fx:
            caller.append("A1 framexact")
        if a in fp:
            caller.append("A1 footprint")
        if fs and "src:cpu" in fs:
            caller.append("A2 cpu")
        if fs and ("src:autoframe" in fs or "src:wwhd_func" in fs):
            caller.append("A1 frame")
        lines.append("%08X  callee=%s caller=%s  %s  # %s %s" % (
            a, "guest" if r else "native", "guest" if caller else "ok",
            ",".join(r + caller + ["(%s)" % x for x in note]) or "-", dec[a][0], dec[a][1]))
    return lines


def unit_headers(out, lines):
    """OUT/native/K.h for game unit K (units.json order, as glue/K.cpp): its functions' and the decompiled functions
    it calls in other units (lean calls) {address, slot index, flags (1: callee=native, 2: caller=ok, 4: listed)},
    sorted, as gabi.h's GABI_NATIVE_TABLE (pre-included by cmake/Decomp.cmake)"""
    import gen
    uj = json.load(open(os.path.join(out, "units.json"), encoding="utf-8"))
    index = {u["name"]: k for k, u in enumerate(uj["units"])}
    rows = collections.defaultdict(list)
    slot_of = {}
    for slot, line in enumerate(lines):
        f = line.split("#")[0].split()
        unit = line.split("#")[1].split()[0]
        flags = 4 | (1 if f[1] == "callee=native" else 0) | (2 if f[2] == "caller=ok" else 0)
        rows[index[unit]].append((int(f[0], 16), slot, flags))
        slot_of[int(f[0], 16)] = slot
    # the decompiled functions of other units that a unit calls (lean calls, gabi.h lcall): their slots, flags 4
    for u in uj["units"]:
        k = index[u["name"]]
        own = {r[0] for r in rows[k]}
        ext = set()
        for src in u["sources"]:
            try:
                ext.update(int(a, 16) for a in LCALL.findall(open(src["path"], encoding="utf-8", errors="replace").read()))
            except OSError:
                pass
        rows[k] += [(a, slot_of[a], 4) for a in ext - own if a in slot_of]
    os.makedirs(os.path.join(out, "native"), exist_ok=True)
    for name, k in index.items():
        r = sorted(rows.get(k, [])) or [(0, 0, 0)]
        gen.write_if_changed(os.path.join(out, "native", "%d.h" % k), (
            "// generated by tools/decomp_build/native_rules.py: native calls of game unit %s (decomp/native.txt)\n"
            "#pragma once\n#include <cstdint>\n#define GABI_NATIVE_TABLE 1\n"
            "namespace gabi_native {\ninline constexpr uint32_t kTab[][3] = {\n%s};\n}\n") % (
            name, "".join("    {0x%08Xu, %du, %du},\n" % x for x in r)))


def main():
    if len(sys.argv) not in (4, 5) or (len(sys.argv) == 5 and sys.argv[4] != "strict"):
        sys.exit(__doc__.strip().splitlines()[4])
    src, out, gen_dir = (os.path.abspath(x) for x in sys.argv[1:4])
    strict = len(sys.argv) == 5
    lines = rules(src, out, gen_dir, strict)
    import gen
    gen.write_if_changed(os.path.join(out, "native.txt"),
                         "# generated by tools/decomp_build/native_rules.py (native-calls rules)\n"
                         + "\n".join(lines) + "\n")
    unit_headers(out, lines)
    n = sum(1 for x in lines if "callee=native" in x)
    print("decomp: native-call rules%s: %d of %d functions may be called natively (%s)" % (
        " (strict park rule)" if strict else "", n, len(lines), os.path.join(out, "native.txt")))


if __name__ == "__main__":
    main()
