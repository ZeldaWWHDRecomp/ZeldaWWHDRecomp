#!/usr/bin/env python3
"""Decomp build fast mode, whole-image ThinLTO (cmake WWHD_DECOMP_FAST_WHOLE_LTO): one game unit's bitcode objects ->
one object whose only global definitions are the ones the unit shares on purpose (the exported native entries
wwhd_ne_*, protected, and default-visibility symbols such as the shared thread-local block); every hidden definition
becomes internal. That is the isolation `ld -r` + hidden -> local gives today, done in the IR, so the final link can
optimise across units (ThinLTO imports a callee's body by its address-named entry) without the units' duplicate
hidden symbols (ODR clashes, ext stubs with stand-in names) meeting.

usage: unit_ir.py OUT.o (--tools DIR | --liblto LIB | --libllvm LIB) --cc CLANG [--target T] [--native] OBJ...
       (default target: the IR's)

  --tools DIR   llvm-link / llvm-dis / llvm-as of the compiler's LLVM (NDK, Homebrew LLVM)
  --liblto LIB  instead of those: the compiler's own libLTO (Xcode / Command Line Tools, which ship no llvm-link): the
                same link of the unit's bitcode and the same rule (every hidden definition internal, every other one
                kept) through its C API (lto_codegen_write_merged_modules, symbols kept by lto_codegen_add_must_preserve_symbol)
  --libllvm LIB  instead of those: the LLVM shared library (libLLVM-NN.dll of llvm-mingw, which ships neither llvm-link
                nor libLTO; the decomp version on Windows, tools/installer/decomp_platform.py): the same link and the same
                rule as --tools, through the LLVM C API (LLVMLinkModules2, then every hidden definition internal)
  --tool NAME=PATH  one of llvm-link / llvm-dis / llvm-as by its own path (the Android app's, whose names differ;
                instead of or over --tools)
  --cc-arg ARG  one more argument of the compiler command (repeatable; the Android app's clang driver flags)
  --native     write a native object (one unit optimised alone, as today) instead of ThinLTO bitcode
Temporary files next to OUT.o (OUT.o.tmp.*), removed at the end. The IR text goes through files (no pipes), as the
Android app's process bridge takes no large input."""
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


def liblto_link(lib, out, objs):
    """libLTO (llvm-c/lto.h): the unit's bitcode objects linked into one module, every defined symbol whose scope is not
    hidden kept as it is, every hidden definition internalised (applyScopeRestrictions, run by write_merged_modules);
    returns the units' target triple"""
    import ctypes
    L = ctypes.CDLL(lib)
    vp, cp, b, u = ctypes.c_void_p, ctypes.c_char_p, ctypes.c_bool, ctypes.c_uint
    for name, res, args in (("lto_get_version", cp, []), ("lto_get_error_message", cp, []),
                            ("lto_module_create", vp, [cp]), ("lto_module_get_target_triple", cp, [vp]), ("lto_module_dispose", None, [vp]),
                            ("lto_module_get_num_symbols", u, [vp]), ("lto_module_get_symbol_name", cp, [vp, u]),
                            ("lto_module_get_symbol_attribute", u, [vp, u]), ("lto_codegen_create", vp, []),
                            ("lto_codegen_dispose", None, [vp]), ("lto_codegen_add_module", b, [vp, vp]),
                            ("lto_codegen_add_must_preserve_symbol", None, [vp, cp]),
                            ("lto_codegen_set_should_internalize", None, [vp, b]),
                            ("lto_codegen_write_merged_modules", b, [vp, cp])):
        f = getattr(L, name)
        f.restype, f.argtypes = res, args
    err = lambda what: sys.exit("unit_ir.py: %s: %s" % (what, (L.lto_get_error_message() or b"").decode()))
    DEF_MASK, UNDEFINED, SCOPE_MASK, INTERNAL, HIDDEN = 0x700, 0x400, 0x3800, 0x800, 0x1000
    cg = L.lto_codegen_create()
    if not cg:
        err("lto_codegen_create")
    keep = set()
    triple = None
    try:
        for o in objs:
            m = L.lto_module_create(o.encode())
            if not m:
                err(o)
            triple = triple or L.lto_module_get_target_triple(m).decode()
            for i in range(L.lto_module_get_num_symbols(m)):
                a = L.lto_module_get_symbol_attribute(m, i)
                if a & DEF_MASK != UNDEFINED and a & SCOPE_MASK not in (INTERNAL, HIDDEN):
                    keep.add(L.lto_module_get_symbol_name(m, i))
            failed = L.lto_codegen_add_module(cg, m)  # (copies the module's IR into the merged one)
            L.lto_module_dispose(m)
            if failed:
                err(o)
        for name in sorted(keep):
            L.lto_codegen_add_must_preserve_symbol(cg, name)
        L.lto_codegen_set_should_internalize(cg, True)
        if L.lto_codegen_write_merged_modules(cg, out.encode()):
            err(out)
    finally:
        L.lto_codegen_dispose(cg)
    return triple


# LLVM C API enums (llvm-c/Core.h)
LL_EXTERNAL, LL_LINKONCE_ANY, LL_LINKONCE_ODR, LL_WEAK_ANY, LL_WEAK_ODR, LL_INTERNAL = 0, 2, 3, 5, 6, 8
LL_DEFAULT_VIS, LL_HIDDEN_VIS = 0, 1
INTERNALIZED = (LL_EXTERNAL, LL_LINKONCE_ANY, LL_LINKONCE_ODR, LL_WEAK_ANY, LL_WEAK_ODR)  # (internalize()'s LINK)


def libllvm_link(lib, out, objs):
    """The LLVM C API (an LLVM shared library): the unit's bitcode objects linked into one module (LLVMLinkModules2),
    then internalize()'s rule on the module itself: every hidden definition (function, variable, alias) with external,
    linkonce or weak linkage becomes internal with default visibility, and every internal function and variable loses
    its comdat (a local symbol needs none). Writes the module to `out`; returns its target triple."""
    import ctypes
    if os.name == "nt" and hasattr(os, "add_dll_directory"):  # (the DLL's own libc++ / libunwind next to it)
        os.add_dll_directory(os.path.dirname(os.path.abspath(lib)))
    L = ctypes.CDLL(lib)
    vp, cp, i, u = ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_uint
    for name, res, args in (("LLVMContextCreate", vp, []), ("LLVMContextDispose", None, [vp]),
                            ("LLVMCreateMemoryBufferWithContentsOfFile", i, [cp, ctypes.POINTER(vp), ctypes.POINTER(cp)]),
                            ("LLVMDisposeMemoryBuffer", None, [vp]), ("LLVMDisposeMessage", None, [vp]),
                            ("LLVMParseBitcodeInContext2", i, [vp, vp, ctypes.POINTER(vp)]),
                            ("LLVMLinkModules2", i, [vp, vp]), ("LLVMDisposeModule", None, [vp]),
                            ("LLVMWriteBitcodeToFile", i, [vp, cp]), ("LLVMGetTarget", cp, [vp]),
                            ("LLVMGetFirstFunction", vp, [vp]), ("LLVMGetNextFunction", vp, [vp]),
                            ("LLVMGetFirstGlobal", vp, [vp]), ("LLVMGetNextGlobal", vp, [vp]),
                            ("LLVMGetFirstGlobalAlias", vp, [vp]), ("LLVMGetNextGlobalAlias", vp, [vp]),
                            ("LLVMIsDeclaration", i, [vp]), ("LLVMGetLinkage", u, [vp]), ("LLVMSetLinkage", None, [vp, u]),
                            ("LLVMGetVisibility", u, [vp]), ("LLVMSetVisibility", None, [vp, u]),
                            ("LLVMGetComdat", vp, [vp]), ("LLVMSetComdat", None, [vp, vp])):
        f = getattr(L, name)
        f.restype, f.argtypes = res, args
    ctx = L.LLVMContextCreate()
    dest = None
    try:
        for o in objs:
            buf, msg, mod = vp(), cp(), vp()
            if L.LLVMCreateMemoryBufferWithContentsOfFile(o.encode(), ctypes.byref(buf), ctypes.byref(msg)):
                sys.exit("unit_ir.py: %s: %s" % (o, (msg.value or b"").decode(errors="replace")))
            failed = L.LLVMParseBitcodeInContext2(ctx, buf, ctypes.byref(mod))  # (reads it all: the buffer can go)
            L.LLVMDisposeMemoryBuffer(buf)
            if failed:
                sys.exit("unit_ir.py: %s: not LLVM bitcode (compile the unit with -flto=thin)" % o)
            if dest is None:
                dest = mod
            elif L.LLVMLinkModules2(dest, mod):  # (mod is destroyed either way)
                sys.exit("unit_ir.py: linking %s failed" % o)
        if dest is None:
            sys.exit("unit_ir.py: no objects")

        def each(first, nxt):
            v = first(dest)
            while v:
                yield v
                v = nxt(v)
        globals_ = list(each(L.LLVMGetFirstFunction, L.LLVMGetNextFunction)) + \
            list(each(L.LLVMGetFirstGlobal, L.LLVMGetNextGlobal))
        aliases = list(each(L.LLVMGetFirstGlobalAlias, L.LLVMGetNextGlobalAlias))
        for v in globals_ + aliases:
            if (not L.LLVMIsDeclaration(v) and L.LLVMGetVisibility(v) == LL_HIDDEN_VIS
                    and L.LLVMGetLinkage(v) in INTERNALIZED):
                L.LLVMSetLinkage(v, LL_INTERNAL)
                L.LLVMSetVisibility(v, LL_DEFAULT_VIS)
        for v in globals_:  # (aliases have no comdat)
            if L.LLVMGetLinkage(v) == LL_INTERNAL and L.LLVMGetComdat(v):
                L.LLVMSetComdat(v, None)
        if L.LLVMWriteBitcodeToFile(dest, out.encode()):
            sys.exit("unit_ir.py: cannot write %s" % out)
        return (L.LLVMGetTarget(dest) or b"").decode()
    finally:
        if dest is not None:
            L.LLVMDisposeModule(dest)
        L.LLVMContextDispose(ctx)


def main():
    a = sys.argv[1:]
    out = a.pop(0)
    opt = {}
    objs = []
    tool, cc_args = {}, []
    while a:
        x = a.pop(0)
        if x in ("--tools", "--liblto", "--libllvm", "--cc", "--target"):
            opt[x] = a.pop(0)
        elif x == "--tool":
            name, _, path = a.pop(0).partition("=")
            tool[name] = path
        elif x == "--cc-arg":
            cc_args.append(a.pop(0))
        elif x == "--native":
            opt[x] = True
        else:
            objs.append(x)
    tmp = out + ".tmp"
    run = lambda *c: subprocess.run(c, check=True)
    try:
        if "--liblto" in opt:
            triple = liblto_link(opt["--liblto"], tmp + ".i.bc", objs)
            m = re.match(r"(.+)", triple or "")  # the units' own target (clang would override it)
        elif "--libllvm" in opt:
            triple = libllvm_link(opt["--libllvm"], tmp + ".i.bc", objs)
            m = re.match(r"(.+)", triple or "")
        else:
            t = lambda name: tool.get(name) or os.path.join(opt["--tools"], name)
            run(t("llvm-link"), "-o", tmp + ".bc", *objs)
            run(t("llvm-dis"), tmp + ".bc", "-o", tmp + ".ll")
            with open(tmp + ".ll", encoding="utf-8") as f:
                ll = f.read()
            ll, n = internalize(ll)
            with open(tmp + ".i.ll", "w", encoding="utf-8") as f:
                f.write(ll)
            run(t("llvm-as"), "-o", tmp + ".i.bc", tmp + ".i.ll")
            m = re.search(r'^target triple = "([^"]+)"', ll, re.M)  # the units' own target (clang would override it)
        cc = [opt["--cc"]] + cc_args + ["-c", "-O2", "-Wno-override-module", "-fPIC"]
        if "--target" in opt or m:
            cc.append("--target=" + opt.get("--target", m.group(1) if m else ""))
        if "--native" not in opt:
            cc.append("-flto=thin")
        run(*cc, tmp + ".i.bc", "-o", out)
    finally:
        for s in (".bc", ".i.bc", ".ll", ".i.ll"):
            try:
                os.remove(tmp + s)
            except OSError:
                pass


if __name__ == "__main__":
    main()
