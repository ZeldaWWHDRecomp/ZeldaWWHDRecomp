#!/usr/bin/env python3
"""Decomp build: hand the results of decompiled functions to gabi's NativeScope (native calls, form N).

A decompiled function entered natively (a plain C++ call from another decompiled function of its unit) must leave
the guest registers as the guest path does: Entry::run stores the result in r3 / r4 / f1 (store_ret), and decompiled
code passes registers on implicitly (a call declared with fewer arguments than its callee takes hands over what the
registers still hold; the verifier compared exactly that). The C++ return value never reaches the guest registers
by itself, so for a body whose result is a scalar or a pointer this rewrites every

    return EXPR;    ->    return wwhd_native_scope_.ret<wwhd_ret_t_>((EXPR));

(a braced EXPR without the extra parentheses) and its `WWHD_GLUE_FUNC(` to `WWHD_GLUE_FUNC_R(`. gabi.h's
WWHD_GLUE_FUNC lets only a void function go native, so a body this leaves alone (another result type, a lambda or a
local class inside, which could hold returns of their own, or a return that is not a statement of its own: inside a
macro argument or a #define in the body) stays a guest call when nested.

Only the decomp build's glued copies are rewritten; the decompilation's harness never sees this.
"""
import re

FUNC = re.compile(r"\bWWHD_GLUE_FUNC\(")
SCALARS = {
    "BOOL", "boolean", "bool", "u8", "u16", "u32", "s8", "s16", "s32", "f32", "f64", "int", "unsigned", "unsigned int",
    "char", "signed char", "unsigned char", "short", "unsigned short", "float", "double", "uint8_t", "uint16_t",
    "uint32_t", "int8_t", "int16_t", "int32_t", "Pair32", "gabi::Pair32", "cPhs_State", "fpc_ProcID",
}
# a lambda introducer (`[...](`, `[...] {`, `[...] mutable`) or a local class with a body: their returns are not the
# function's own
LAMBDA = re.compile(r"\[[^\[\]\n]*\]\s*(?:\(|\{|mutable\b|->)")
LOCAL_CLASS = re.compile(r"\b(?:struct|class|union)\s+\w*\s*(?::[^;{]*)?\{")


def scalar_type(t):
    t = " ".join(t.replace("*", " * ").split())
    t = re.sub(r"^const\s+|\s+const$", "", t).strip()
    if t.endswith("*"):
        return True
    return t in SCALARS


def _skip_literal(s, i):
    """s[i] starts a string, char literal or comment: the index after it (else None)"""
    c = s[i]
    if c == "/" and s.startswith("//", i):
        j = s.find("\n", i)
        return len(s) if j < 0 else j
    if c == "/" and s.startswith("/*", i):
        j = s.find("*/", i + 2)
        return len(s) if j < 0 else j + 2
    if c in "\"'":
        if c == "'" and i > 0 and (s[i - 1].isalnum() or s[i - 1] == "_"):
            return None  # a digit separator (1'000) or a u8'x' prefix handled below
        if c == '"' and i > 0 and s[i - 1] == "R":
            m = re.match(r'R"([^(\s]*)\(', s[i - 1:])
            if m:
                end = s.find(")" + m.group(1) + '"', i)
                return len(s) if end < 0 else end + len(m.group(1)) + 2
        j = i + 1
        while j < len(s) and s[j] != c:
            j += 2 if s[j] == "\\" else 1
        return j + 1
    return None


def code_mask(s):
    """per character: True where it is code (not inside a string, char literal or comment)"""
    mask = bytearray(b"\x01") * len(s)
    i = 0
    while i < len(s):
        if s[i] in "\"'/":
            j = _skip_literal(s, i)
            if j is not None and j > i + (0 if s[i] != "/" else 1):
                for k in range(i, min(j, len(s))):
                    mask[k] = 0
                i = j
                continue
        i += 1
    return mask


def _match(s, mask, i, open_c, close_c):
    """s[i] == open_c: the index of its partner"""
    d = 0
    for j in range(i, len(s)):
        if not mask[j]:
            continue
        if s[j] == open_c:
            d += 1
        elif s[j] == close_c:
            d -= 1
            if d == 0:
                return j
    raise ValueError("unbalanced %s at %d" % (open_c, i))


def _split_top(s):
    out, d, cur = [], 0, ""
    for ch in s:
        if ch in "(<[{":
            d += 1
        elif ch in ")>]}":
            d -= 1
        if ch == "," and d == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def rewrite(text):
    """-> (text, n_rewritten, n_left): see the module comment"""
    mask = code_mask(text)
    # macros whose definition returns (libjpeg's HUFF_DECODE): a body that uses one has a return the rewrite cannot see
    ret_macros = set()
    for d in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+(\w+)((?:[^\n]*\\\n)*[^\n]*)", text, re.M):
        if re.search(r"\breturn\b", d.group(2)):
            ret_macros.add(d.group(1))
    ret_use = re.compile(r"\b(?:%s)\b" % "|".join(map(re.escape, sorted(ret_macros)))) if ret_macros else None
    edits = []  # (start, end, replacement), applied back to front
    n_rew = n_left = 0
    for m in FUNC.finditer(text):
        if not mask[m.start()]:
            continue
        args_open = m.end() - 1
        args_close = _match(text, mask, args_open, "(", ")")
        args = _split_top(text[args_open + 1:args_close])
        if len(args) < 2 or args[1] == "void":
            continue
        # the body: the `{` before the macro, to its partner
        b = text.rfind("{", 0, m.start())
        while b >= 0 and not mask[b]:
            b = text.rfind("{", 0, b)
        if b < 0 or text[b + 1:m.start()].strip():
            n_left += 1
            continue
        end = _match(text, mask, b, "{", "}")
        stmt_end = text.index(";", args_close)
        body = text[stmt_end + 1:end]
        bmask = mask[stmt_end + 1:end]
        code = "".join(ch if bmask[k] else " " for k, ch in enumerate(body))
        if (not scalar_type(args[1]) or LAMBDA.search(code) or LOCAL_CLASS.search(code)
                or (ret_use and ret_use.search(code))):
            n_left += 1
            continue
        local = []
        plain = True
        for r in re.finditer(r"\breturn\b", code):
            i = r.end()
            line = code[code.rfind("\n", 0, r.start()) + 1:r.start()]
            if line.lstrip().startswith("#"):
                plain = False  # inside a #define in the body
                break
            d = 0
            j = i
            while j < len(code):
                ch = code[j]
                if ch in "([{":
                    d += 1
                elif ch in ")]}":
                    d -= 1
                    if d < 0:
                        break
                elif ch == ";" and d == 0:
                    break
                j += 1
            if j >= len(code) or code[j] != ";":
                plain = False  # not a statement of its own (a macro argument: CHECK_BIT_BUFFER(s, n, return -1))
                break
            expr = body[i:j].strip()
            if not expr:
                continue
            base = stmt_end + 1
            inner = expr if expr.startswith("{") else "(" + expr + ")"
            local.append((base + r.start(), base + j, "return wwhd_native_scope_.ret<wwhd_ret_t_>(%s)" % inner))
        if not plain:
            n_left += 1
            continue
        edits.extend(local)
        edits.append((m.start(), m.end(), "WWHD_GLUE_FUNC_R("))
        n_rew += 1
    for s, e, rep in sorted(edits, reverse=True):
        text = text[:s] + rep + text[e:]
    return text, n_rew, n_left
