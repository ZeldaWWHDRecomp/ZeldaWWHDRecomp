#!/usr/bin/env python3
"""Decomp build fast mode, whole-image ThinLTO (cmake WWHD_DECOMP_FAST_WHOLE_LTO): one game unit's bitcode objects ->
one object whose only global definitions are the ones the unit shares on purpose (the exported native entries
wwhd_ne_*, protected, and default-visibility symbols such as the shared thread-local block); every hidden definition
becomes internal. That is the isolation `ld -r` + hidden -> local gives today, done in the IR, so the final link can
optimise across units (ThinLTO imports a callee's body by its address-named entry) without the units' duplicate
hidden symbols (ODR clashes, ext stubs with stand-in names) meeting.

usage: unit_ir.py OUT.o --tools DIR --cc CLANG [--target T] [--native] OBJ...  (default target: the IR's)

  --tools DIR  llvm-link / llvm-dis / llvm-as of the compiler's LLVM (NDK, Homebrew LLVM)
  --native     write a native object (one unit optimised alone, as today) instead of ThinLTO bitcode
Temporary files next to OUT.o (OUT.o.tmp.*), removed at the end."""
import os
import re
import subprocess
import sys

LINK = r"(?:(?:linkonce_odr|linkonce|weak_odr|weak)\s+)?"
DEF_FN = re.compile(r"^define " + LINK + r"hidden ", re.M)
DEF_GV = re.compile(r"^(@[^ ]+ = )" + LINK + r"hidden (?=(?:dso_local |unnamed_addr |local_unnamed_addr |"
                    r"thread_local(?:\([a-z]+\))? |addrspace\(\d+\) |externally_initialized )*(?:global|constant|alias))",
                    re.M)
COMDAT_FN = re.compile(r"(^define internal [^\n]*?) comdat(?:\(\$[^)]+\))?", re.M)
COMDAT_GV = re.compile(r"(^@[^ ]+ = internal [^\n]*?), comdat(?:\(\$[^)]+\))?", re.M)


def internalize(text):
    """every hidden definition (functions, variables, aliases; any linkage but declarations) -> internal, without its
    comdat (a local symbol needs none); returns (text, count)"""
    text, a = DEF_FN.subn("define internal ", text)
    text, b = DEF_GV.subn(r"\1internal ", text)
    text = COMDAT_FN.sub(r"\1", text)
    text = COMDAT_GV.sub(r"\1", text)
    return text, a + b


def main():
    a = sys.argv[1:]
    out = a.pop(0)
    opt = {}
    objs = []
    while a:
        x = a.pop(0)
        if x in ("--tools", "--cc", "--target"):
            opt[x] = a.pop(0)
        elif x == "--native":
            opt[x] = True
        else:
            objs.append(x)
    t = opt["--tools"]
    tmp = out + ".tmp"
    run = lambda *c: subprocess.run(c, check=True)
    try:
        run(os.path.join(t, "llvm-link"), "-o", tmp + ".bc", *objs)
        ll = subprocess.run([os.path.join(t, "llvm-dis"), tmp + ".bc", "-o", "-"], check=True, capture_output=True,
                            text=True).stdout
        ll, n = internalize(ll)
        subprocess.run([os.path.join(t, "llvm-as"), "-o", tmp + ".i.bc"], input=ll, text=True, check=True)
        cc = [opt["--cc"], "-c", "-O2", "-Wno-override-module", "-fPIC"]
        m = re.search(r'^target triple = "([^"]+)"', ll, re.M)  # the units' own target (clang would override it)
        if "--target" in opt or m:
            cc.append("--target=" + opt.get("--target", m.group(1) if m else ""))
        if "--native" not in opt:
            cc.append("-flto=thin")
        run(*cc, tmp + ".i.bc", "-o", out)
    finally:
        for s in (".bc", ".i.bc"):
            try:
                os.remove(tmp + s)
            except OSError:
                pass


if __name__ == "__main__":
    main()
