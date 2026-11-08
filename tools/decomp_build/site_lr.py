#!/usr/bin/env python3
"""Decomp build: site-exact LR. A decompiled function that calls the same target T from several places cannot have its
guest calls told apart at run time (call_lr leaves LR as it is then). This tool gives each such call the return address
of its own `bl` in the original, as a compile-time constant, where that can be decided:

  the k-th call to T in the decompiled body (in source order) is the k-th `bl T` in the original function (in address
  order), and only where both have the same number of calls to T; otherwise every call to T in that function keeps
  call_lr's answer as before. Calls to a target the original calls once get the same constant (call_lr's answer).

Which source call reaches which target is decided by the compiler: each unit source is compiled once more as LLVM IR
with line tables (`cc`, a compiler launcher on a shadow object library, cmake/Decomp.cmake), and the inline chain of
each guest call (gabi::call / lcall / gmem_call / decomp_lcall with a constant target, or a call of another decompiled
function) names the call expression of the decompiled body it comes from. `unit` then writes the unit's sources with
those call expressions wrapped as `(GABI_SITE(T, RET), CALL)` (gabi.h NativeScope::site: a mark that T's guest or
native entry takes, site_take); frame-exact functions keep their own call_site LRs and get no marks.

usage: site_lr.py cc COMPILER ARGS...            (compiler launcher: writes the scan JSON as the object file)
       site_lr.py prepare OUT_DIR GEN_DIR SRC_DIR  (configure: per-unit bl lists, function-like macro names, sites.cmake)
       site_lr.py unit OUT_DIR K SCAN...           (build: unit K's sited sources and OUT_DIR/sites/K.txt)
"""
import bisect
import collections
import glob
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# ---------------------------------------------------------------------------------------------------------- scan (cc)
GF = re.compile(r"WWHD_GLUE_FUNC(?:_R)?\(\s*0x([0-9A-Fa-f]{8})\s*,")
DOCALL = re.compile(r"@_ZN4gabi9do_call_g\w*\(ptr [^,]*, ptr [^,]*, ptr [^,]*, i32 (?:noundef )?(%?-?[\w.]+), i32 (?:noundef )?(%?-?[\w.]+)")
LCALL = re.compile(r"@decomp_lcall\(ptr [^,]*, i32 (?:noundef )?(%?-?[\w.]+), i32 (?:noundef )?(%?-?[\w.]+)")
GMEM = re.compile(r"@gmem_call\(ptr [^,]*, i32 (?:noundef )?(%?-?[\w.]+), i32 (?:noundef )?(%?-?[\w.]+)")
NLR = re.compile(r"@decomp_native_lr\(")
DIRECT = re.compile(r"\b(?:call|invoke) [^@]*@(\"[^\"]+\"|[\w.$]+)\(")
DBG = re.compile(r"!dbg !(\d+)")


def scan_ir(ir, src_lines):
    """LLVM IR (text) of a unit source -> {function: {'a': its WWHD_GLUE_FUNC address or 0, 'e': [[kind, value, chain]]}}.
    kind 'g': a guest bl to the constant value; 'v': a guest bl to a target known only at run time; 'd': a call of the
    function `value`. chain: the call's inline chain, outermost first, [line, column, WWHD_GLUE_FUNC address of the
    function the location is in, or 0]; the outermost line is the physical line of the source (1-based; negative: not
    in it)."""
    ir = re.sub(r"\n\s+to label", " to label", ir)
    logical, cf, cl = {}, None, 1
    for i, t in enumerate(src_lines):
        m = re.match(r'#line (\d+) "([^"]*)"', t)
        if m:
            cf, cl = m.group(2), int(m.group(1))
            continue
        logical[(cf, cl)] = i
        cl += 1
    files = {m.group(1): m.group(2) for m in re.finditer(r'^!(\d+) = !DIFile\(filename: "([^"]*)"', ir, re.M)}
    loc, scope, spfile, spaddr = {}, {}, {}, {}
    for m in re.finditer(r"^!(\d+) = (?:distinct )?!DILocation\(line: (\d+), column: (\d+), scope: !(\d+)(?:, inlinedAt: !(\d+))?", ir, re.M):
        loc[m.group(1)] = (int(m.group(2)), int(m.group(3)), m.group(4), m.group(5))
    for m in re.finditer(r"^!(\d+) = (?:distinct )?!DILexicalBlock(?:File)?\(scope: !(\d+)", ir, re.M):
        scope[m.group(1)] = m.group(2)
    for m in re.finditer(r"^!(\d+) = (?:distinct )?!DISubprogram\([^\n]*?file: !(\d+), line: (\d+)", ir, re.M):
        f = files.get(m.group(2))
        spfile[m.group(1)] = f
        i = logical.get((f, int(m.group(3))))
        a = 0
        if i is not None:
            for t in src_lines[i:i + 3]:
                g = GF.search(t)
                if g:
                    a = int(g.group(1), 16)
                    break
                if "}" in t:
                    break
        spaddr[m.group(1)] = a

    def spof(s):
        for _ in range(1000):
            if s in spaddr:
                return s
            s = scope.get(s)
            if s is None:
                return None

    cache = {}

    def chain(n):
        if n in cache:
            return cache[n]
        out, k = [], n
        while k:
            l = loc.get(k)
            if not l:
                break
            s = spof(l[2])
            out.append([l[0], l[1], spaddr.get(s, 0), s])
            k = l[3]
        out.reverse()
        if out:
            i = logical.get((spfile.get(out[0][3]), out[0][0]))
            out[0][0] = i + 1 if i is not None else -out[0][0]
        cache[n] = r = [x[:3] for x in out]
        return r

    funcs, cur = {}, None
    for line in ir.split("\n"):
        if line.startswith("define "):
            m = re.search(r'@("[^"]+"|[\w.$]+)\(', line)
            cur = m.group(1).strip('"')
            d = DBG.search(line)
            funcs[cur] = {"a": spaddr.get(d.group(1), 0) if d else 0, "e": []}
            continue
        if line.startswith("}"):
            cur = None
            continue
        if cur is None or ("call " not in line and "invoke " not in line) or NLR.search(line):
            continue
        d = DBG.search(line)
        ch = chain(d.group(1)) if d else []
        ev = None
        for rx, ai, ki, ok in ((DOCALL, 0, 1, ("0", "2")), (LCALL, 1, None, None), (GMEM, 0, 1, ("0",))):
            m = rx.search(line)
            if m:
                if ki is not None and not (m.group(ki + 1) in ok or m.group(ki + 1).startswith("%")):
                    ev = "skip"  # bctrl (kind 1)
                    break
                a = m.group(ai + 1)
                ev = ("g", int(a) & 0xFFFFFFFF) if not a.startswith("%") else ("v", 0)
                break
        if ev == "skip":
            continue
        if ev is None:
            m = DIRECT.search(line)
            if not m or m.group(1).startswith("llvm."):
                continue
            ev = ("d", m.group(1).strip('"'))
        funcs[cur]["e"].append([ev[0], ev[1], ch])
    return funcs


def cc(argv):
    """compiler launcher: the compile command with its object file replaced by the scan of the source's IR"""
    args = list(argv)
    out = src = None
    rest = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == "-o":
            out = args[i + 1]
            i += 2
            continue
        if a in ("-g0", "-g", "-c") or a.startswith("-flto"):
            i += 1
            continue
        rest.append(a)
        i += 1
    for a in rest:
        if not a.startswith("-") and os.path.splitext(a)[1] in (".cpp", ".cc", ".cxx"):
            src = a
    if out is None or src is None:
        sys.exit("site_lr.py cc: no -o or no source in: %s" % " ".join(argv))
    r = subprocess.run(rest + ["-S", "-emit-llvm", "-gline-tables-only", "-o", "-"], stdout=subprocess.PIPE)
    if r.returncode:
        sys.exit(r.returncode)
    with open(src, encoding="utf-8", errors="replace") as fh:
        lines = fh.read().split("\n")
    funcs = scan_ir(r.stdout.decode("utf-8", "replace"), lines)
    tmp = out + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump({"src": os.path.abspath(src), "funcs": funcs}, fh, separators=(",", ":"))
    os.replace(tmp, out)


# ------------------------------------------------------------------------------------------------------- prepare
def bl_calls(secs, funcs, icalls, a):
    """the original's bl / bla of the function at a: [(target, return address)] in address order (an import call by
    its import slot, as call_lr.h keys it)"""
    import native_rules as nr
    i = bisect.bisect_left(funcs, a)
    if i == len(funcs) or funcs[i] != a:
        return None
    e = funcs[i + 1] if i + 1 < len(funcs) else a + 4
    if e - a > 0x40000:
        return None
    out = []
    for k, w in enumerate(nr.code_words(secs, a, e)):
        ea = a + 4 * k
        if (w >> 26) == 18 and (w & 1):
            li = w & 0x03FFFFFC
            if li & 0x02000000:
                li -= 0x04000000
            out.append((icalls.get(ea, (li if w & 2 else ea + li) & 0xFFFFFFFF), ea + 4))
    return out


FUNC_MACRO = re.compile(r"^\s*#\s*define\s+(\w+)\(", re.M)
NOSCOPE = re.compile(r"^\s*#\s*undef\s+WWHD_GLUE_FUNC\b", re.M)
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)


def sited_path(out, k, path):
    """unit k's sited copy of a source (per unit: a support source without VERIFY marks is compiled into several)"""
    g = os.path.join(out, "glued") + os.sep
    rel = path[len(g):] if path.startswith(g) else os.path.join("_src", path.lstrip(os.sep))
    return os.path.join(out, "sited", str(k), rel)


def prepare(out, gen_dir, src):
    import gen
    import native_rules as nr
    funcs, _, icalls, _ = nr.read_table(os.path.join(gen_dir, "table.c"))
    img = os.path.join(out, "verify", "image.bin")
    secs = nr.read_image(img) if os.path.isfile(img) else []
    uj = json.load(open(os.path.join(out, "units.json"), encoding="utf-8"))
    index = {u["name"]: k for k, u in enumerate(uj["units"])}
    fx = set()
    if os.path.isfile(os.path.join(out, "frame_exact.txt")):
        fx = {int(l.split()[0], 16) for l in open(os.path.join(out, "frame_exact.txt")) if l.strip()}
    per = collections.defaultdict(dict)
    for line in open(os.path.join(out, "functions.tsv"), encoding="utf-8"):
        a, u, _ = line.rstrip("\n").split("\t")
        a = int(a, 16)
        if a in fx or u not in index:
            continue
        b = bl_calls(secs, funcs, icalls, a) if secs else None
        if b:
            per[index[u]]["%08X" % a] = b
    os.makedirs(os.path.join(out, "sites"), exist_ok=True)
    for k in range(len(uj["units"])):
        gen.write_if_changed(os.path.join(out, "sites", "bl_%d.json" % k), json.dumps(per.get(k, {}), separators=(",", ":")))
    macros, noscope = set(), set()
    for d in ("wwhd_src", os.path.join("tools", "verify", "include"), os.path.join("tools", "verify", "src")):
        for p in glob.glob(os.path.join(src, d, "**", "*.h"), recursive=True):
            t = open(p, encoding="utf-8", errors="replace").read()
            macros.update(FUNC_MACRO.findall(t))
            if NOSCOPE.search(t):
                noscope.add(os.path.basename(p))
    gen.write_if_changed(os.path.join(out, "sites", "macros.txt"), "\n".join(sorted(macros)) + "\n")
    # headers that define their own WWHD_GLUE_FUNC (no NativeScope, so no GABI_SITE): their includers get no marks
    gen.write_if_changed(os.path.join(out, "sites", "noscope.txt"), "\n".join(sorted(noscope)) + "\n")
    # the sited copies as the units' sources (cmake/Decomp.cmake): the same -iquote as the copy they come from
    cm = ["# generated by tools/decomp_build/site_lr.py"]
    for k, u in enumerate(uj["units"]):
        paths = [sited_path(out, k, s["path"]) for s in u["sources"]]
        cm.append("set(WWHD_DECOMP_UNIT_%d_SITED\n  %s)" % (k, "\n  ".join('"%s"' % p.replace("\\", "/") for p in paths)))
        for s, p in zip(u["sources"], paths):
            q = s["quote"] or os.path.dirname(s["path"])
            cm.append('set_property(SOURCE "%s" APPEND PROPERTY COMPILE_OPTIONS -iquote "%s")' % (p.replace("\\", "/"), q.replace("\\", "/")))
    gen.write_if_changed(os.path.join(out, "sites", "sites.cmake"), "\n".join(cm) + "\n")
    print("decomp: site-exact LR: call lists of %d functions in %d units" % (sum(len(v) for v in per.values()), len(per)))


# ------------------------------------------------------------------------------------------------------ rewrite
KEYWORDS = {"alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch",
            "char", "class", "co_await", "co_return", "co_yield", "compl", "concept", "const", "const_cast", "consteval",
            "constexpr", "constinit", "continue", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else",
            "enum", "explicit", "export", "extern", "float", "for", "friend", "goto", "if", "inline", "int", "long",
            "mutable", "namespace", "new", "noexcept", "not", "not_eq", "operator", "or", "or_eq", "private",
            "protected", "public", "register", "reinterpret_cast", "requires", "return", "short", "signed", "sizeof",
            "static", "static_assert", "static_cast", "struct", "switch", "template", "throw", "try", "typedef",
            "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "while", "xor", "xor_eq"}
# a call expression may follow these words directly (any other identifier before it: a declaration)
EXPR_AFTER = {"return", "else", "do", "case", "throw", "co_return", "co_yield", "co_await", "and", "or", "not"}
BLANK = re.compile(r"//[^\n]*|/\*.*?\*/|\"(?:[^\"\\\n]|\\.)*\"|'(?:[^'\\\n]|\\.)*'|^\s*#[^\n]*", re.S | re.M)


def blank_text(t):
    """comments, string and character literals and preprocessor lines as spaces (same offsets)"""
    return BLANK.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), t)


def _id(ch):
    return ch.isalnum() or ch == "_"


def _back_ws(t, i):
    """the last non-blank index before i, or -1"""
    i -= 1
    while i >= 0 and t[i] in " \t\r\n":
        i -= 1
    return i


def _fwd_ws(t, i):
    while i < len(t) and t[i] in " \t\r\n":
        i += 1
    return i


def _match_fwd(t, i, o, c):
    d = 0
    for j in range(i, len(t)):
        if t[j] == o:
            d += 1
        elif t[j] == c:
            d -= 1
            if d == 0:
                return j
        elif t[j] in ";{}" and o != "{":
            return None
    return None


def _match_back(t, i, o, c):
    d = 0
    for j in range(i, -1, -1):
        if t[j] == c:
            d += 1
        elif t[j] == o:
            d -= 1
            if d == 0:
                return j
        elif t[j] in ";{}":
            return None
    return None


def _angle_fwd(t, i):
    """t[i] == '<': the matching '>' of a template argument list, or None"""
    d = 0
    for j in range(i, len(t)):
        ch = t[j]
        if ch == "<":
            d += 1
        elif ch == ">":
            d -= 1
            if d == 0:
                return j
        elif ch == "(":
            j2 = _match_fwd(t, j, "(", ")")
            if j2 is None:
                return None
        elif ch in ";{}" or (ch == "&" and t[j + 1:j + 2] == "&") or (ch == "|" and t[j + 1:j + 2] == "|"):
            return None
    return None


def _angle_back(t, i):
    """t[i] == '>': the matching '<', or None"""
    d = 0
    for j in range(i, -1, -1):
        ch = t[j]
        if ch == ">":
            d += 1
        elif ch == "<":
            d -= 1
            if d == 0:
                return j
        elif ch in ";{}()":
            return None
    return None


def _word_start(t, i):
    while i > 0 and _id(t[i - 1]):
        i -= 1
    return i


def _operand_start(t, b):
    """b: the last index of a postfix-expression operand (before `->` / `.`): its first index, or None"""
    if _id(t[b]):
        s = _word_start(t, b)
        if t[s:b + 1] in KEYWORDS and t[s:b + 1] != "this":
            return None
        return s
    if t[b] in ")]":
        o = _match_back(t, b, "(" if t[b] == ")" else "[", t[b])
        if o is None:
            return None
        b2 = _back_ws(t, o)
        if b2 >= 0 and (_id(t[b2]) or t[b2] in ")]>"):
            if _id(t[b2]):
                w = t[_word_start(t, b2):b2 + 1]
                if w in KEYWORDS and w != "this":
                    return o if t[b] == ")" else None  # `return (x)->m()`: the group is the operand
            if t[b2] == ">":
                a = _angle_back(t, b2)
                if a is None:
                    return None
                b3 = _back_ws(t, a)
                if b3 < 0 or not _id(t[b3]):
                    return None
                return _qualified_start(t, _word_start(t, b3))
            sub = _operand_start(t, b2)
            return None if sub is None else _qualified_start(t, sub)
        return o if t[b] == ")" else None
    return None


def _number_before(t, b):
    """t[b] == '.': it ends a numeric literal (`1.`), not an operand"""
    j = b - 1
    return _id(t[j]) and t[_word_start(t, j)].isdigit()


def _qualified_start(t, s):
    """s: the start of a name; back over `X::` / `X<...>::` / `::` qualifiers and `->` / `.` member access"""
    while True:
        b = _back_ws(t, s)
        if b >= 1 and t[b - 1:b + 1] == "::":
            b2 = _back_ws(t, b - 1)
            if b2 >= 0 and _id(t[b2]):
                s = _word_start(t, b2)
                continue
            if b2 >= 0 and t[b2] == ">":
                a = _angle_back(t, b2)
                b3 = _back_ws(t, a) if a is not None else -1
                if b3 < 0 or not _id(t[b3]):
                    return None
                s = _word_start(t, b3)
                continue
            s = b - 1
            continue
        if b >= 1 and (t[b - 1:b + 1] == "->" or (t[b] == "." and t[b - 1] != "." and not _number_before(t, b))):
            b2 = _back_ws(t, b - 1 if t[b] == ">" else b)
            if b2 < 0:
                return None
            if _id(t[b2]) and t[_word_start(t, b2):b2 + 1] == "template":
                return None
            o = _operand_start(t, b2)
            if o is None:
                return None
            s = o
            continue
        return s


def call_extent(t, p, macros=()):
    """t: blanked source text; p: the offset of a call's callee (or member) name. -> (start, end) of the whole call
    expression [start, end), or None when it is not a plain call expression that can be wrapped (an operator, a
    constructor of a declared object, a macro, a destructor, ...)"""
    if p >= len(t) or not (t[p].isalpha() or t[p] == "_") or (p > 0 and _id(t[p - 1])):
        return None
    q = p
    while q < len(t) and _id(t[q]):
        q += 1
    name = t[p:q]
    if name in KEYWORDS or name in macros:
        return None
    r = _fwd_ws(t, q)
    while True:  # a qualified name (the location names its first part): on to the last one
        if r < len(t) and t[r] == "<":
            a = _angle_fwd(t, r)
            if a is None:
                return None
            r = _fwd_ws(t, a + 1)
        if t[r:r + 2] != "::":
            break
        r = _fwd_ws(t, r + 2)
        if r >= len(t) or not (t[r].isalpha() or t[r] == "_"):
            return None
        q = r
        while q < len(t) and _id(t[q]):
            q += 1
        if t[r:q] in KEYWORDS or t[r:q] in macros:
            return None
        r = _fwd_ws(t, q)
    if r >= len(t) or t[r] != "(":
        return None
    e = _match_fwd(t, r, "(", ")")
    if e is None:
        return None
    s = _qualified_start(t, p)
    if s is None:
        return None
    b = _back_ws(t, s)
    if b >= 0:
        if _id(t[b]):
            if t[_word_start(t, b):b + 1] not in EXPR_AFTER:
                return None  # `Type name(args)`: a declaration
        elif t[b] in "*&":
            b2 = _back_ws(t, b)
            if b2 >= 0 and (_id(t[b2]) or t[b2] == ">") and t[_word_start(t, b2):b2 + 1] not in EXPR_AFTER:
                return None  # `Type* name(args)` (or a product: left as it is)
        elif t[b] == ">" and not (b > 0 and t[b - 1] in "->="):
            return None  # `T<U> name(args)` (or a comparison: left as it is)
        elif t[b] == "~":
            return None
        elif t[b] == ")":
            o = _match_back(t, b, "(", ")")
            b2 = _back_ws(t, o) if o is not None else -1
            if o is None or (b2 >= 2 and t[_word_start(t, b2):b2 + 1] == "new"):
                return None  # `new (place) T(args)`
    return s, e + 1


def source_calls(fns, glue, F):
    """F's guest calls (all scans of its unit: fns {function: events}, glue {function: address}) -> {target: [key]} in
    source order. key: the call's chain of (line, column) from F's body inward."""
    def events(fn, depth, seen):
        out = []
        for kind, val, ch in fns.get(fn, []):
            key, tgt = [], None
            for i, (l, c, ga) in enumerate(ch):
                if i > 0 and ga and ga != F:
                    tgt = ga
                    break
                key.append((l, c))
            if tgt is not None:
                out.append((tuple(key), tgt))
            elif kind == "g":
                if not (val == F and len(ch) <= 2):  # not the body's own guest entry (WWHD_GLUE_FUNC's nested call)
                    out.append((tuple(key), val))
            elif kind == "v":
                out.append((tuple(key), None))
            elif kind == "d":
                if val in glue:
                    out.append((tuple(key), glue[val]))
                elif val in fns and depth < 8 and val not in seen:
                    sub = events(val, depth + 1, seen + (val,))
                    roots = {e[2][0][2] for e in fns[val] if e[2]}
                    out += sub if roots == {F} else [(tuple(key) + k, t) for k, t in sub]
        return out

    fn = next((f for f, a in glue.items() if a == F and f in fns), None)
    keys = collections.defaultdict(set)
    if fn is not None:
        for k, t in events(fn, 0, (fn,)):
            if t is not None:
                keys[t].add(k)
    return {t: sorted(v) for t, v in keys.items()}


def match_sites(fns, glue, F, bl):
    """F's source calls (source_calls) against the original's bl list -> ({target: [(key, return address)]} for the
    targets whose call counts match, {target: (bl, source)} for the others)"""
    keys = source_calls(fns, glue, F)
    want = collections.defaultdict(list)
    for t, ret in bl:
        want[t].append(ret)
    ok, bad = {}, {}
    for t, rets in want.items():
        ks = keys.get(t, [])
        if len(ks) == len(rets):
            ok[t] = list(zip(ks, rets))
        else:
            bad[t] = (len(rets), len(ks))
    return ok, bad


def unit(out, k, scans):
    import gen
    uj = json.load(open(os.path.join(out, "units.json"), encoding="utf-8"))
    u = uj["units"][k]
    bl = json.load(open(os.path.join(out, "sites", "bl_%d.json" % k), encoding="utf-8"))
    macros = set(open(os.path.join(out, "sites", "macros.txt"), encoding="utf-8").read().split())
    fns, glue, by_src = {}, {}, {}
    for p in scans:
        d = json.load(open(p, encoding="utf-8"))
        by_src[d["src"]] = d["funcs"]
        for f, v in d["funcs"].items():
            fns[f] = v["e"]
            if v["a"]:
                glue[f] = v["a"]
    # the function each source defines (marks go into the source whose body holds the call)
    home = {}
    for s, funcs in by_src.items():
        for f, v in funcs.items():
            if v["a"]:
                home[v["a"]] = s
    texts = {s["path"]: open(s["path"], encoding="utf-8", errors="replace").read() for s in u["sources"]}
    noscope = set(open(os.path.join(out, "sites", "noscope.txt"), encoding="utf-8").read().split())
    for s in texts:
        macros.update(FUNC_MACRO.findall(texts[s]))
    # a source with its own WWHD_GLUE_FUNC (directly or through a header): no NativeScope to carry marks
    plain = {s for s, t in texts.items()
             if NOSCOPE.search(t) or any(os.path.basename(i) in noscope for i in INCLUDE.findall(t))}
    blanked = {s: blank_text(t) for s, t in texts.items()}
    starts = {s: [0] + [i + 1 for i, ch in enumerate(t) if ch == "\n"] for s, t in texts.items()}
    st = collections.Counter()
    wraps = collections.defaultdict(dict)  # source -> {(start, end): [(target, ret)]}
    report = []
    def body(src, F):
        """[start, end) of F's body in its source (WWHD_GLUE_FUNC's function), or None"""
        m = re.search(r"WWHD_GLUE_FUNC(?:_R)?\(\s*0x%08X\s*," % F, blanked.get(src, ""), re.I)
        if not m:
            return None
        t = blanked[src]
        o = t.rfind("{", 0, m.start())
        e = _match_fwd(t, o, "{", "}") if o >= 0 else None
        return None if e is None else (o + 1, e)

    for fa in sorted(bl):
        F = int(fa, 16)
        calls = [tuple(x) for x in bl[fa]]
        ok, bad = match_sites(fns, glue, F, calls)
        n = collections.Counter(t for t, _ in calls)
        src = home.get(F)
        rng = body(src, F) if src in texts and src not in plain else None
        for t, (nb, ns) in sorted(bad.items()):
            st["stale_multi" if nb > 1 else "stale_one"] += nb
            report.append("stale %08X %08X bl=%d src=%d" % (F, t, nb, ns))
        for t, sites in sorted(ok.items()):
            # every call to t in F gets its mark, or none does (an unmarked one could take another's mark)
            ext = []
            for key, ret in sites:
                line, col = key[0] if key else (0, 0)
                e = None
                if src in texts and 0 < line <= len(starts[src]):
                    e = call_extent(blanked[src], starts[src][line - 1] + col - 1, macros)
                if e is not None and not (rng and rng[0] <= e[0] and e[1] <= rng[1]):
                    e = None  # not in F's own body (or no scope for marks)
                ext.append(e)
            per = collections.Counter(ext)
            if None in per or any(v > 1 for v in per.values()):
                st["unmarked_multi" if n[t] > 1 else "unmarked_one"] += n[t]
                report.append("unmarked %08X %08X bl=%d" % (F, t, n[t]))
                continue
            for e, (key, ret) in zip(ext, sites):
                wraps[src].setdefault(e, []).append((t, ret))
                report.append("exact %08X %08X %08X bl=%d" % (F, t, ret, n[t]))
            st["exact_multi" if n[t] > 1 else "exact_one"] += n[t]
    for s, t in texts.items():
        ins = collections.defaultdict(list)  # offset -> [(order, text)]
        for (a, b), marks in wraps.get(s, {}).items():
            # openings: the wider wrap first; closings: the narrower first
            ins[a].append((0, -(b - a), "(" + ", ".join("GABI_SITE(0x%08Xu, 0x%08Xu)" % m for m in marks) + ", "))
            ins[b].append((-1, (b - a), ")"))
        outp, last = [], 0
        for off in sorted(ins):
            outp.append(t[last:off])
            outp += [x[2] for x in sorted(ins[off])]
            last = off
        outp.append(t[last:])
        path = sited_path(out, k, s)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        gen.write_if_changed(path, "".join(outp))
    gen.write_if_changed(os.path.join(out, "sites", "%d.txt" % k),
                         "# generated by tools/decomp_build/site_lr.py: site-exact LR of game unit %s (bl sites)\n%s\n%s\n" % (
                             u["name"], " ".join("%s=%d" % x for x in sorted(st.items())), "\n".join(report)))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if sys.argv[1] == "cc":
        cc(sys.argv[2:])
    elif sys.argv[1] == "prepare" and len(sys.argv) == 5:
        prepare(*(os.path.abspath(x) for x in sys.argv[2:5]))
    elif sys.argv[1] == "unit" and len(sys.argv) >= 4:
        unit(os.path.abspath(sys.argv[2]), int(sys.argv[3]), sys.argv[4:])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
