#!/usr/bin/env python3
"""Write a placeholder build/gen for build checks without the game.

usage: stubgen.py OUTDIR [--decomp-hooks FILE]

recomp.py needs your own cking.rpx. This script instead emits the same file layout (funcs.h,
code_000.c, table.c, imports.c) with every guest function the runtime refers to (hooks and direct
calls) defined as a stub that halts via ppc_unimplemented. The result compiles and links the full
runtime and renderer on any host, so CI and porting work can check the build; the executable
cannot run the game (startup refuses it: the entry point matches no RPX).
--decomp-hooks FILE: the same for the decomp build (CMake WWHD_DECOMP=ON): the listed functions are hooks
as with recomp.py --decomp-hooks.
"""
import glob
import os
import re
import shutil
import sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.normpath(os.path.join(here, "..", ".."))


def hook_lists(decomp=False):
    hooks, sites = set(), set()
    files = [os.path.join(here, "hooks.txt")] + sorted(glob.glob(os.path.join(glob.escape(here), "hooks_*.txt")))
    if decomp:  # the decomp build's own runtime replacements, as recomp.py --decomp-hooks reads them
        files += sorted(glob.glob(os.path.join(glob.escape(here), "decomp_only_hooks_*.txt")))
        files.append(os.path.join(here, "decomp_hostrt_hooks.txt"))  # the guest runtime's host replacements
    for hp in files:
        for line in open(hp):
            line = line.split("#")[0].strip()
            if line.startswith("@"):
                sites.add(int(line[1:], 16))
            elif line:
                hooks.add(int(line, 16))
    return hooks, sites


def runtime_refs():
    funcs = set()
    pat = re.compile(r"\bf_([0-9A-F]{8})(?:_orig)?\b")
    for ext in ("c", "cpp", "h", "mm"):
        for p in glob.glob(os.path.join(glob.escape(root), "runtime", "src", "**", "*." + ext), recursive=True):
            with open(p, errors="replace") as f:
                funcs.update(int(m, 16) for m in pat.findall(f.read()))
    return funcs


def decomp_list(path):
    out = set()
    for line in open(path):
        line = line.split("#")[0].strip()
        if line:
            out.add(int(line, 16))
    return out


def main(outdir, decomp_hooks=None):
    os.makedirs(outdir, exist_ok=True)
    hooks, sites = hook_lists(decomp=bool(decomp_hooks))
    if decomp_hooks:
        hooks |= decomp_list(decomp_hooks)
    funcs = sorted(runtime_refs() | hooks)
    with open(os.path.join(outdir, "funcs.h"), "w") as f:
        f.write('#pragma once\n#include "ppc.h"\n\n')
        for e in funcs:
            f.write("void f_%08X(Cpu* __restrict c);\n" % e)
        for e in sorted(hooks):
            f.write("void f_%08X_orig(Cpu* __restrict c);\nvoid hook_%08X(Cpu* c);\n" % (e, e))
        for e in sorted(sites):
            f.write("void site_%08X(Cpu* c);\n" % e)
    with open(os.path.join(outdir, "code_000.c"), "w") as f:
        f.write('#include "funcs.h"\n\n/* placeholder: written by tools/recomp/stubgen.py, not game code */\n')
        for e in funcs:
            if e in hooks:
                f.write("void f_%08X(Cpu* __restrict c) { hook_%08X(c); }\n" % (e, e))
                f.write("void f_%08X_orig(Cpu* __restrict c) { ppc_unimplemented(c, 0x%08Xu, 0); }\n" % (e, e))
            else:
                f.write("void f_%08X(Cpu* __restrict c) { ppc_unimplemented(c, 0x%08Xu, 0); }\n" % (e, e))
        # keep the site hooks referenced, as the recompiled code does
        f.write("\nvoid stubgen_sites(Cpu* c) {\n")
        for e in sorted(sites):
            f.write("    site_%08X(c);\n" % e)
        f.write("}\n")
    with open(os.path.join(outdir, "table.c"), "w") as f:
        f.write('#include "funcs.h"\n#include "recomp_table.h"\n\n')
        f.write("const RecompEntry g_recomp_funcs[] = {\n")
        for e in funcs:
            f.write("    {0x%08Xu, f_%08X},\n" % (e, e))
        f.write("};\nconst unsigned g_recomp_func_count = %d;\n\n" % len(funcs))
        f.write("const RecompImport g_recomp_imports[] = {{0, 0, \"\", \"\", 0, 0}};\n")
        f.write("const unsigned g_recomp_import_count = 0;\n")
        f.write("const uint32_t g_recomp_entry_point = 0u; /* placeholder: matches no RPX */\n")
        if decomp_hooks:
            f.write("const uint32_t g_recomp_decomp_kept[] = {0u};\nconst unsigned g_recomp_decomp_kept_count = 0;\n")
            f.write("const uint32_t g_recomp_decomp_import_calls[][2] = {{0u, 0u}};\n"
                    "const unsigned g_recomp_decomp_import_call_count = 0;\n")
        # Hooks off: like recomp.py without --mod-hooks, emit no registration.
        # The runtime owns the hook count, flag pointer and body pointer.
    with open(os.path.join(outdir, "imports.c"), "w") as f:
        f.write('#include "funcs.h"\n')
    with open(os.path.join(outdir, "report.txt"), "w") as f:
        f.write("placeholder from stubgen.py: %d stub functions, %d hooks, %d sites\n" % (len(funcs), len(hooks), len(sites)))
    if decomp_hooks:
        shutil.copyfile(decomp_hooks, os.path.join(outdir, "decomp_hooks.txt"))
    print("wrote %s: %d stub functions, %d hooks, %d sites" % (outdir, len(funcs), len(hooks), len(sites)))


if __name__ == "__main__":
    args = sys.argv[1:]
    decomp = None
    if len(args) == 3 and args[1] == "--decomp-hooks":
        decomp = args[2]
        args = args[:1]
    if len(args) != 1:
        sys.exit(__doc__)
    main(args[0], decomp)
