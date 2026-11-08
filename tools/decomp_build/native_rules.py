#!/usr/bin/env python3
"""Decomp build: which decompiled functions may later be called natively (plain C++ calls between decompiled
functions, without a guest frame; native-calls design, phase 1). This only writes the rules; nothing uses them yet.

usage: native_rules.py DECOMP_DIR OUT_DIR GEN_DIR [strict] [--decl TABLE] [--lift CATS] [--fast [--hcall]]

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
decl=STATUS:NIrNFf:RK (at the end of the reasons): the decompilation's declaration table (decl_lint.py --handoff;
TABLE, else DECOMP_DIR/tools/verify/decl_handoff.tsv when it exists): exact / shim functions are register-exact (every
decompiled call passes exactly the registers the definition reads, NI integer and NF float registers; RK its result:
0 none, 1 r3, 2 r3+r4, 3 f1): a native call passes C++ arguments only (gabi.h NativeInfo flags 8, real native
calls). decl=- : not in the table, or guest there: native calls keep the guest registers.
caller=guest: its own calls must stay guest calls (A1: framexact or footprint; A2: src:cpu). Per call site rules
(A3: stack arguments at that call) are decided where the call is compiled.
"""
import bisect
import glob
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
# a read of the guest stack pointer (`const u32 sp = gabi::cpu->r[1];`), not an assignment. A body whose only Cpu
# access is such reads AND that is listed in STACK_ARG_READERS keeps caller=ok (no A2): it reads its stack-passed
# arguments (entry SP + 8 and up, the caller's outgoing-argument area) or writes them back, which no callee touches in
# either form: a native callee leaves r1 and the guest stack as it found them, a guest callee runs below the body's SP
# (gabi.h do_call: fc->sp or SP - 16) and restores r1 (NativeScope / native entry restore saved_r1; gabi::Local moves r1
# the same way in either case). Listed only after reading the body: an SP-relative read BELOW the entry SP (stale
# bytes a guest callee's frame may have left, e.g. daNpc_Ko1_c::CreateHeap 02273F48) must keep A2. Callee rule C7 is
# unchanged. (2026-11-07, helper G: the 15 bodies found by the regex at decomp 00e6040fe, each read checked.)
CPU_SP_READ = re.compile(r"(?:gabi::)?cpu->r\[1\](?!\s*(?:[-+*/|&^]?=(?!=)|\+\+|--))")
STACK_ARG_READERS = {0x02011D2C, 0x024F06B4, 0x0253EC0C, 0x025580FC, 0x02561E14, 0x0258E1E8, 0x0259E08C, 0x025A847C,
                     0x025D5928, 0x025D5A20, 0x025D5B20, 0x025E4F64, 0x025E6220, 0x027EB5F4, 0x027EFBCC}
SRCF_CPU = re.compile(r"\bcpu->|gabi::cpu\b|\bc->(r|f|lr|ctr|cr)\b")
SRCF = [("src:local", re.compile(r"gabi::(Local|Stack|LocalOf|FrameLocal)\b|\bLocalOf<|\bStack<")),
        ("src:nativeframe", re.compile(r"NativeFrame<")),
        ("src:cpu", SRCF_CPU),
        ("src:call_site", re.compile(r"call(_ptr)?_site\s*[<(]")),
        ("src:tail_ptr", re.compile(r"tail_ptr(?:_ic)?\s*[<(]")),  # (_ic: gen.py --ptr-ic)
        ("src:va", re.compile(r"\bva_(list|start|arg)\b|\.\.\.\s*\)")),
        ("src:jmp", re.compile(r"setjmp|longjmp|OSLoadContext|OSSaveContext|__builtin_(frame|return)_address")),
        ("src:autoframe", re.compile(r"\bAutoFrame\b"))]
# --lift CATEGORY,...: the source reasons each step-2 category lifts (WWHD_DECOMP_NATIVE_AUTOFRAME / _LOCAL / _TAILPTR)
LIFT = {"src:autoframe": "autoframe", "src:local": "local", "src:tail_ptr": "tailptr"}  # (src:wwhd_func: no native form)
FUNC = re.compile(r"WWHD_(GLUE_)?FUNC(_R)?\(\s*0x([0-9A-Fa-f]{8})\s*,")
LCALL = re.compile(r"\bgabi::lcall<0x([0-9A-Fa-f]{8}),")
HCALL = re.compile(r"\bgabi::hcall<0x([0-9A-Fa-f]{8}),")
INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)


class HeaderCalls:
    """--hcall (fast mode, gen.py --hcall): the gabi::hcall addresses a unit's sources reach, their own and those of
    the headers they include ("..." includes, transitively; searched as the compiler does: the including file's
    directory, the source's -iquote directory, then the unit include path with gen.py's OUT/hinc copy of
    wwhd_src/include in its place). Each header is read once."""

    def __init__(self, src, out):
        root = os.path.abspath(os.path.join(HERE, "..", ".."))
        self.dirs = [os.path.join(root, "runtime", "include"), os.path.join(src, "tools", "verify", "include"),
                     os.path.join(src, "tools", "verify", "src"), os.path.join(out, "hinc"),
                     os.path.join(src, "wwhd_src", "include"), os.path.join(out, "include")]
        self.memo = {}

    def _file(self, path):
        if path not in self.memo:
            try:
                text = open(path, encoding="utf-8", errors="replace").read()
            except OSError:
                text = ""
            self.memo[path] = ({int(a, 16) for a in HCALL.findall(text)}, INCLUDE.findall(text))
        return self.memo[path]

    def _find(self, inc, first):
        for d in first + self.dirs:
            p = os.path.normpath(os.path.join(d, inc))
            if os.path.isfile(p):
                return p
        return None

    def of_source(self, path, quote):
        addrs, seen, todo = set(), {path}, [path]
        while todo:
            p = todo.pop()
            a, incs = self._file(p)
            addrs |= a
            first = [os.path.dirname(p)] + ([quote] if quote else [])
            for inc in incs:
                q = self._find(inc, first)
                if q and q not in seen:
                    seen.add(q)
                    todo.append(q)
        return addrs
VERIFY_ADDR = re.compile(r"\bVERIFY(?:_NAMED)?\(\s*(0x[0-9A-F]{8})\b")
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
            if "src:cpu" in fs and not SRCF_CPU.search(CPU_SP_READ.sub("", body)):
                fs.add("src:cpu_sp")  # the only Cpu access: reads of the stack pointer (see CPU_SP_READ)
            if m.group(1) and not m.group(2) and len(allargs) > 1 and allargs[1].strip() != "void":
                fs.add("src:ret")  # a result native_returns.py did not hand to NativeScope (gabi.h: guest calls only)
            if not m.group(1):
                fs.add("src:wwhd_func")  # WWHD_FUNC (not the GameCube-style entry): its automatic frame, no native form
            # parameter types from the function head: floats by type, the rest (this, pointers, integers) by count
            params = split_args(head[head.rfind("(", 0, head.rfind(")")) + 1:head.rfind(")")]) if "(" in head else []
            nf = sum(1 for x in params if FLOAT_T.match(" ".join(x.split()[:-1]) if len(x.split()) > 1 else x))
            out.setdefault(addr, (fs, (max(0, len(margs) - nf), nf), ret_kind(allargs[1] if len(allargs) > 1 else "")))
    return out


def ret_kind(r):
    """a WWHD_(GLUE_)FUNC result type -> 0 none, 1 r3, 2 r3+r4 (Pair32), 3 f1; None when it is not a scalar result"""
    t = " ".join(r.replace("const ", " ").split())
    if t == "void":
        return 0
    if t in ("Pair32", "gabi::Pair32"):
        return 2
    if FLOAT_T.match(t):
        return 3
    if t.endswith("&") or "Ret<" in t:
        return None
    return 1


FUNC_ADDR = re.compile(r"WWHD_(?:GLUE_)?FUNC(?:_R)?\(\s*0x([0-9A-Fa-f]{8})\s*,")
RULE_ERRORS = []  # exact callees with an under-declared ext line (main: a configure error)
DECL_FIELD = re.compile(r"\bdecl=(?:exact|shim):(\d)r(\d)f:(\d)\b")
DECL_REGS = re.compile(r"^(\d+)r(\d+)f$")


def read_decl(path):
    """decl_lint.py --handoff table -> {addr: (status, int registers, float registers)}"""
    out = {}
    if not path or not os.path.isfile(path):
        return out
    for line in open(path, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        f = line.rstrip("\n").split("\t")
        m = DECL_REGS.match(f[2]) if len(f) > 2 else None
        if m:
            st = f[1]  # a callee with e? sites an indirect call may read is `guest` in the table (decl_lint e?i)
            out[int(f[0], 16)] = (st, int(m.group(1)), int(m.group(2)))
    return out


EXT_GLUE = re.compile(r"^ext\s+([0-9A-Fa-f]{8})\s+(.*?)\s*(?:#.*)?$")
EXT_PROTO = re.compile(r"^(.*?)\s*((?:\w+::)*~?\w+)\s*\((.*)\)\s*(?:const)?\s*(?:in\s+\S+)?$")
FLOAT_SCALARS = ("f32", "f64", "float", "double", "Vec_f32")


def ext_regs(proto):
    """(int registers, float registers) an `ext` glue line's declaration passes (`this` for a qualified name that is
    not static), None when it cannot be counted (variadic, unparsed)"""
    m = EXT_PROTO.match(proto)
    if not m or "..." in m.group(3):
        return None
    gi = 1 if "::" in m.group(2) and not m.group(1).startswith("static") else 0
    fi = 0
    for p in split_args(m.group(3)):
        p = re.sub(r"/\*.*?\*/", "", p).split("=")[0].strip()
        if p in ("", "void"):
            continue
        t = re.sub(r"\b(const|volatile|struct|class|enum)\b", " ", p)
        if "*" in t or "&" in t or "[" in t:
            gi += 1
            continue
        w = t.split()
        if any(x in FLOAT_SCALARS for x in w[-2:]):
            fi += 1
        else:
            gi += 1
    return gi, fi


PROTO_LINE = re.compile(r"^\s*(?:extern\s+|static\s+|inline\s+|virtual\s+)*[\w:<>,*&\s]+?[\s*&]~?\w+\s*\([^;{}]*\)\s*(?:const\s*)?;\s*$")
CALL_NAME = re.compile(r"(?<![\w&])(~?\w+)\s*\(")


def called_names(text):
    """names written as a call (`NAME(`) in code: comments, glue blocks, ext stub definitions and prototypes left out"""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    out = set()
    for ln in text.splitlines():
        if PROTO_LINE.match(ln):
            continue
        out.update(CALL_NAME.findall(ln))
    return out


def under_declared(paths, decl, rks=None):
    """{source path: [(callee, passes, takes)]}: `ext` glue lines that pass fewer registers than a register-exact
    (exact / shim) callee's definition takes, except stubs the source only takes the address of (gabi::guest_fn, a HostFn
    binding: no call; a stub called through a header wrapper counts as called). The declaration table (decl_lint.py) should never let these
    through; a caller that relies on a register an earlier call left must keep the guest registers (its calls store
    their results), so such sources' functions keep their calls as guest calls (A3 decl) and an exact callee loses its
    native entry (rules(): an exact callee here is a configure error)."""
    out = {}
    for p in paths:
        try:
            t = open(p, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        m = re.search(r"/\* WWHD glue(.*?)\*/", t, re.S)
        if not m:
            continue
        calls = None
        for line in m.group(1).splitlines():
            e = EXT_GLUE.match(line.strip())
            if not e or "callback" in e.group(2).split():
                continue
            a = int(e.group(1), 16)
            d = decl.get(a)
            if not d or d[0] not in ("exact", "shim"):
                continue
            r = ext_regs(e.group(2))
            pm0 = EXT_PROTO.match(e.group(2))
            if pm0 and rks is not None and rks.get(a) == 0 and ret_kind(pm0.group(1).replace("static ", "")) not in (0, None):
                # the declaration reads a result the callee's definition never sets: the caller relies on r3/f1 as an
                # earlier call left it (d_s_logo: finishDrawCommands' "result" is ResMgr_getArchiveFile's r3)
                r = (-1, -1)
            if r and (r[0] < d[1] or r[1] < d[2]):
                pm = EXT_PROTO.match(e.group(2))
                name = pm.group(2).split("::")[-1] if pm else None
                if calls is None:
                    calls = called_names("\n".join(x for x in t.splitlines() if "/* other unit (ext) */" not in x))
                if name and name not in calls and re.search(r"&\s*%s\b" % re.escape(name), t):
                    continue  # used by its address only (taken in the source and never called there)
                out.setdefault(p, []).append((a, r, (d[1], d[2]), d[0]))
    return out


def decl_token(d, rk):
    """the decl= field of a native.txt line"""
    if not d or d[0] not in ("exact", "shim") or rk is None or d[1] > 8 or d[2] > 8:
        return "decl=-"
    return "decl=%s:%dr%df:%d" % (d[0], d[1], d[2], rk)


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


def rules(src, out, gen_dir, strict=False, decl_path=None, lift=()):
    if decl_path is None:
        decl_path = os.path.join(src, "tools", "verify", "decl_handoff.tsv")
    decl = read_decl(decl_path)
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
    paths = [s["path"] for u in uj["units"] for s in u["sources"]]
    srcf = source_features(paths)
    under = under_declared(paths, decl, {a: v[2] for a, v in srcf.items()})
    # an exact callee an ext line under-declares loses its native entry (configure error); a result declared for a callee
    # that returns nothing only needs the caller's guest calls (A3 decl: every earlier call stores its result)
    under_callees = {a for v in under.values() for a, r, _, st in v if st == "exact" and r != (-1, -1)}
    RULE_ERRORS[:] = sorted(under_callees)
    under_funcs = set()
    for p in under:
        try:
            under_funcs.update(int(x, 16) for x in FUNC_ADDR.findall(open(p, encoding="utf-8", errors="replace").read()))
        except OSError:
            pass
    for p, v in sorted(under.items()):
        for a, r, tk, st in v:
            what = ("declares a result its %s definition never sets" % st if r == (-1, -1) else
                    "passes %dr%df, its %s definition takes %dr%df" % (r[0], r[1], st, tk[0], tk[1]))
            print("decomp: native-call rules: %s: ext %08X %s (the file's "
                  "functions keep guest calls%s)" % (os.path.relpath(p, out), a, what,
                                                     ", the callee its registers" if st == "exact" and r != (-1, -1) else ""))
    # gen.py --hooked-bodies (fast mode): functions the runtime hooks, listed so their bodies run decompiled behind
    # the hook; never entered natively (callers reach hook_X)
    hooked = set()
    hp = os.path.join(out, "hooked.txt")
    if os.path.isfile(hp):
        hooked = {int(x, 16) for x in open(hp, encoding="utf-8").read().split()}
    lines = []
    for a in sorted(dec):
        r = []
        if a in kept:
            r.append("kept")
        if a in hooked:
            r.append("runtime hook")
        if a in fx:
            r.append("framexact")
        note = []
        if park is None:
            (r if strict else note).append("may park (no code image)")
        elif a in park:
            (r if strict else note).append("may park")
        if fp.get(a, 0):
            r.append("footprint")
        fs, (ni, nf), rk = srcf.get(a, (None, (0, 0), None))
        if fs is None:
            r.append("no body")
        else:
            r += sorted(fs - {"src:cpu_sp"})
        if ni > 8 or nf > 8:
            r.append("stack args")
        r += sorted(x for x in feat.get(a, ()) if x != "orig:bctrl")
        # level 1 step 2 (--lift): these source reasons no longer keep the guest call; the native entry then places r1
        # as do_call does (gabi.h GABI_NATIVE_PLACE, NativeInfo bit 18: "(place)")
        lifted = [x for x in r if LIFT.get(x) in lift]
        if lifted:
            r = [x for x in r if x not in lifted]
            note.append("place")
        caller = []
        if a in fx:
            caller.append("A1 framexact")
        if a in fp:
            caller.append("A1 footprint")
        if fs and "src:cpu" in fs and not ("src:cpu_sp" in fs and a in STACK_ARG_READERS):
            caller.append("A2 cpu")
        if fs and (("src:autoframe" in fs and "autoframe" not in lift) or "src:wwhd_func" in fs):
            caller.append("A1 frame")
        if a in under_funcs:
            caller.append("A3 decl")
        d = decl.get(a)
        if a in under_callees:
            d = None
        lines.append("%08X  callee=%s caller=%s  %s %s  # %s %s" % (
            a, "guest" if r else "native", "guest" if caller else "ok",
            ",".join(r + caller + ["(%s)" % x for x in note]) or "-", decl_token(d, rk), dec[a][0], dec[a][1]))
    return lines


def fast_shape(flags):
    """the native entry shape of a register-exact function (gabi.h NEntryT Thunk::run): result type, parameter list,
    argument names (NativeInfo flags: bits 8-11 integer registers, `this` included, 12-15 float, 16-17 result)"""
    ni, nf, rk = flags >> 8 & 15, flags >> 12 & 15, flags >> 16 & 3
    cr = ("void", "uint32_t", "uint64_t", "double")[rk]
    params = ["uint32_t a%d" % i for i in range(ni)] + ["double f%d" % i for i in range(nf)]
    names = ["a%d" % i for i in range(ni)] + ["f%d" % i for i in range(nf)]
    return cr, ", ".join(params), ", ".join(names)


def fast_text(own_verify, own_exact, ext_exact):
    """fast mode (cmake WWHD_DECOMP_FAST, gabi.h GABI_FAST_NE): calls between units bound by address at link time.
    Each register-exact function of the unit is exported as extern "C" wwhd_ne_XXXXXXXX (its native entry shape)
    through VERIFY's GABI_NE_<addr>(F) (empty for every other VERIFY address of the unit), and each register-exact
    callee in another unit is declared with gabi_native::Ne<ADDR> {ok, fn} for gabi.h's lcall"""
    t = ["#define GABI_FAST_NE_TABLE 1",
         "#ifndef GABI_NE_VIS",
         "#if defined(__APPLE__)",
         '#define GABI_NE_VIS __attribute__((visibility("default")))  /* kept global by ld -r; executables bind directly */',
         "#else",
         '#define GABI_NE_VIS __attribute__((visibility("protected")))  /* kept by --localize-hidden; no PLT */',
         "#endif",
         "#endif",
         "#define GABI_NE_TABLE 1",
         "namespace gabi_native {",
         "template <uint32_t A> struct Ne { static constexpr bool ok = false; };"]
    for a, flags in sorted(ext_exact.items()):
        cr, params, _ = fast_shape(flags)
        t.append('extern "C" GABI_NE_VIS %s wwhd_ne_%08X(%s);' % (cr, a, params or "void"))
        t.append("template <> struct Ne<0x%08Xu> { static constexpr bool ok = true; static constexpr auto fn = "
                 "&wwhd_ne_%08X; };" % (a, a))
    t.append("}  /* namespace gabi_native */")
    for tok in sorted(own_verify):
        a = int(tok, 16)
        if a in own_exact:
            cr, params, names = fast_shape(own_exact[a])
            t.append('#define GABI_NE_%s(F) extern "C" GABI_NE_VIS %s wwhd_ne_%08X(%s) { return gabi::ne_body<%su, F, '
                     '%s>(%s); }' % (tok, cr, a, params or "void", tok, cr, names))
        else:
            t.append("#define GABI_NE_%s(F)" % tok)
    return "\n".join(t) + "\n"


def unit_headers(out, lines, fast=False, hcalls=None):
    """OUT/native/K.h for game unit K (units.json order, as glue/K.cpp): its functions' and the decompiled functions
    it calls in other units (lean calls) {address, slot index, flags (1: callee=native, 2: caller=ok, 4: listed)},
    sorted, as gabi.h's GABI_NATIVE_TABLE (pre-included by cmake/Decomp.cmake)"""
    import gen
    uj = json.load(open(os.path.join(out, "units.json"), encoding="utf-8"))
    index = {u["name"]: k for k, u in enumerate(uj["units"])}
    rows = collections.defaultdict(list)
    slot_of, flags_of = {}, {}
    for slot, line in enumerate(lines):
        f = line.split("#")[0].split()
        unit = line.split("#")[1].split()[0]
        flags = 4 | (1 if f[1] == "callee=native" else 0) | (2 if f[2] == "caller=ok" else 0)
        if "(place)" in line.split("#")[0]:  # a lifted step-2 reason: the native entry places r1 (gabi.h bit 18)
            flags |= 1 << 18
        m = DECL_FIELD.search(line.split("#")[0])
        if m:  # register-exact: flags 8, its registers and result (gabi.h NativeInfo)
            flags |= 8 | int(m.group(1)) << 8 | int(m.group(2)) << 12 | int(m.group(3)) << 16
        rows[index[unit]].append((int(f[0], 16), slot, flags))
        slot_of[int(f[0], 16)] = slot
        flags_of[int(f[0], 16)] = flags
    # the decompiled functions of other units that a unit calls (lean calls, gabi.h lcall): their slots and flags
    # (4; 1 and 8 with the registers: lcall's real native call; 2 is the callee's own and not used there)
    for u in uj["units"]:
        k = index[u["name"]]
        own = {r[0] for r in rows[k]}
        ext = set()
        for src in u["sources"]:
            try:
                ext.update(int(a, 16) for a in LCALL.findall(open(src["path"], encoding="utf-8", errors="replace").read()))
            except OSError:
                pass
            if hcalls:  # --hcall: the unit's calls by constant address (gabi.h hcall: native_info and Ne)
                ext |= hcalls.of_source(src["path"], src.get("quote"))
        rows[k] += [(a, slot_of[a], flags_of[a] & ~2) for a in ext - own if a in slot_of]
    # fast mode: every unit's VERIFY addresses (a function verified in two units is exported by functions.tsv's unit
    # only, the unit its row above belongs to)
    verify = collections.defaultdict(set)
    if fast:
        for u in uj["units"]:
            for src in u["sources"]:
                try:
                    verify[index[u["name"]]].update(
                        VERIFY_ADDR.findall(open(src["path"], encoding="utf-8", errors="replace").read()))
                except OSError:
                    pass
    # the exported functions: register-exact, with a VERIFY in their own unit (a callee without one keeps lcall's path)
    unit_of = {a: index[lines[s].split("#")[1].split()[0]] for a, s in slot_of.items()}
    exported = {a for a, f in flags_of.items() if f & 9 == 9 and "0x%08X" % a in verify[unit_of[a]]}
    os.makedirs(os.path.join(out, "native"), exist_ok=True)
    nexp = next_ = 0
    for name, k in index.items():
        r = sorted(rows.get(k, [])) or [(0, 0, 0)]
        extra = ""
        if fast:
            exact = {a: f for a, _, f in rows.get(k, []) if a in exported}
            own = {a: f for a, f in exact.items() if unit_of[a] == k}
            ext = {a: f for a, f in exact.items() if unit_of[a] != k}
            nexp += len(own)
            next_ += len(ext)
            extra = fast_text(verify[k], own, ext)
        gen.write_if_changed(os.path.join(out, "native", "%d.h" % k), (
            "// generated by tools/decomp_build/native_rules.py: native calls of game unit %s (decomp/native.txt)\n"
            "#pragma once\n#include <cstdint>\n#define GABI_NATIVE_TABLE 1\n"
            "namespace gabi_native {\ninline constexpr uint32_t kTab[][3] = {\n%s};\n}\n%s") % (
            name, "".join("    {0x%08Xu, %du, %du},\n" % x for x in r), extra))
    if fast:
        print("decomp: fast mode: %d register-exact functions exported by address (wwhd_ne_*), %d declarations of "
              "callees in other units" % (nexp, next_))


def main():
    args = sys.argv[1:]
    decl_path = None
    lift = ()
    fast = "--fast" in args
    if fast:
        args.remove("--fast")
    hcall = "--hcall" in args
    if hcall:
        args.remove("--hcall")
        if not fast:
            sys.exit("native_rules.py: --hcall needs --fast")
    if "--lift" in args:
        i = args.index("--lift")
        lift = tuple(x for x in (args[i + 1] if i + 1 < len(args) else "").split(",") if x)
        del args[i:i + 2]
    if "--decl" in args:
        i = args.index("--decl")
        decl_path = args[i + 1] if i + 1 < len(args) else ""
        del args[i:i + 2]
    if len(args) not in (3, 4) or (len(args) == 4 and args[3] != "strict"):
        sys.exit(__doc__.strip().splitlines()[4])
    src, out, gen_dir = (os.path.abspath(x) for x in args[:3])
    strict = len(args) == 4
    lines = rules(src, out, gen_dir, strict, decl_path or None, lift)
    if RULE_ERRORS:  # an exact callee with an under-declared call (rules() printed each): the decl table is wrong
        sys.exit("native_rules.py: %d ext declaration(s) pass fewer registers than their exact callee takes; fix the "
                 "decompilation's declarations (decl_lint.py) or its table" % len(RULE_ERRORS))
    import gen
    gen.write_if_changed(os.path.join(out, "native.txt"),
                         "# generated by tools/decomp_build/native_rules.py (native-calls rules)\n"
                         + "\n".join(lines) + "\n")
    unit_headers(out, lines, fast, HeaderCalls(src, out) if hcall else None)
    n = sum(1 for x in lines if "callee=native" in x)
    ne = sum(1 for x in lines if "callee=native" in x and DECL_FIELD.search(x.split("#")[0]))
    print("decomp: native-call rules%s: %d of %d functions may be called natively, %d of them register-exact (%s)" % (
        " (strict park rule)" if strict else "", n, len(lines), ne, os.path.join(out, "native.txt")))


if __name__ == "__main__":
    main()
