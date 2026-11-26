#!/usr/bin/env python3
"""The decomp version's unit isolation with a real toolchain, on stub game units (no game, no decompilation): setup's
own merge commands (tools/installer/decomp_platform.merge_commands) on two units that each define the same hidden
function, the same hidden variable and the same inline function (one copy per unit, a comdat), plus an exported
entry wwhd_ne_unitN, a default-visibility variable shared with the program and a weak default function the program
overrides (as gabi.h's gmem_frame_info and the runtime's). The program is linked as setup links
the decomp version (the toolchain's compiler driver); run where it can run, it prints what each unit computed. Without
isolation the link fails (two definitions of the hidden function) or a unit uses the other unit's inline copy.

usage: test_unit_merge.py --toolchain NAME [--toolchain-dir DIR] [--mode fast|plain|both] [--work DIR] [--no-run]
  NAME   a toolchain of tools/installer/toolchains.json (zig-0.16.0, llvm-mingw-20260922, xcode-clt, ...)
  DIR    the unpacked toolchain (setup's WWHD_TOOLCHAIN_DIR; CI: the pinned archive it unpacked)
CI: the Linux release jobs (pinned zig) and the Windows release jobs (pinned llvm-mingw).
Locally on a Mac: --toolchain xcode-clt; zig's Linux target with --toolchain zig-0.16.0 --toolchain-dir <a zig 0.16
folder> --no-run (built and checked, not run); WWHD_DECOMP_LIBLLVM=<libLLVM.dylib> with a zig toolchain whose target is
Windows exercises the Windows path (unit_ir.py --libllvm)."""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "installer"))

HEADER = r"""#pragma once
extern "C" __attribute__((visibility("default"))) int g_shared;
inline __attribute__((noinline)) int pick() { return UNIT * 100 + g_shared; }  // each unit's own copy (comdat)
int helper();        // hidden, defined in every unit
extern int counter;  // hidden, defined in every unit
// a weak default in every unit that the program overrides (gabi.h's gmem_frame_info, which runtime/src/decomp/frames.cpp
// defines): on Windows a weak external per unit object; the program's own definition must win
extern "C" __attribute__((visibility("default"), weak)) int g_hook(int x) { return -x; }
"""
SOURCE_A = r"""#include "u.h"
int counter = UNIT;
int helper() { return pick() + counter + g_hook(UNIT); }
"""
GLUE = r"""#include "u.h"
#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)
extern "C" __attribute__((visibility("default"))) int CAT(wwhd_ne_unit, UNIT)() { counter += 1; return helper() * 10 + pick(); }
"""
MAIN = r"""#include <cstdio>
extern "C" __attribute__((visibility("default"))) int g_shared = 7;
extern "C" int g_hook(int x) { return x + 1000; }
extern "C" int wwhd_ne_unit0();
extern "C" int wwhd_ne_unit1();
int main() {
    int a = wwhd_ne_unit0(), b = wwhd_ne_unit1();
    std::printf("unit0 %d unit1 %d\n", a, b);
    return a == EXPECT0 && b == EXPECT1 ? 0 : 1;
}
"""


def expected(unit):
    pick = unit * 100 + 7
    return (pick + unit + 1 + unit + 1000) * 10 + pick


UNIT_FLAGS = ["-std=c++20", "-O2", "-fvisibility=hidden", "-fvisibility-inlines-hidden"]  # (the release's: -fvisibility)


def run(cmd, env):
    p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if p.returncode:
        raise SystemExit("FAILED (%d): %s\n%s" % (p.returncode, " ".join(cmd), p.stdout.decode("utf-8", "replace")))
    return p.stdout.decode("utf-8", "replace")


def hidden_globals(path):
    """ELF: the names of defined hidden symbols that are not local (none after the isolation)"""
    import elf_localize  # noqa: F401 (same folder)
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"\x7fELF":
        return None
    e = "<" if d[5] == 1 else ">"
    shoff, = struct.unpack_from(e + "Q", d, 0x28)
    shentsize, shnum = struct.unpack_from(e + "HH", d, 0x3A)
    sh = [struct.unpack_from(e + "IIQQQQIIQQ", d, shoff + i * shentsize) for i in range(shnum)]
    out = []
    for s in sh:
        if s[1] != 2:
            continue
        strtab = sh[s[6]]
        for i, sym in enumerate(struct.iter_unpack(e + "IBBHQQ", d[s[4]:s[4] + s[5]])):
            if sym[3] and sym[2] & 3 in (1, 2) and sym[1] >> 4 != 0:
                name = d[strtab[4] + sym[0]:d.index(b"\0", strtab[4] + sym[0])].decode()
                out.append(name)
            if sym[1] >> 4 == 0 and i >= s[7]:
                raise SystemExit("FAILED: a local symbol after sh_info in %s" % path)
    return out


def can_run(tc):
    import decomp_platform
    kind = decomp_platform.toolchain_kind(tc)
    target = " ".join(tc.cxx)
    if kind == "xcode-clt":
        return sys.platform == "darwin"
    if kind == "llvm-mingw":
        return os.name == "nt"
    if kind == "zig":
        import platform
        m = {"amd64": "x86_64", "arm64": "aarch64"}.get(platform.machine().lower(), platform.machine().lower())
        return sys.platform.startswith("linux") and ("-target %s-linux" % m) in target
    return False


def one_mode(tc, work, fast, run_it):
    import decomp_platform
    env = tc.env or dict(os.environ)
    flags = UNIT_FLAGS + (["-flto=thin"] if fast else []) + decomp_platform.unit_flags_extra(tc, fast)
    units = []
    for u in (0, 1):
        d = os.path.join(work, "u%d" % u)
        os.makedirs(d, exist_ok=True)
        for name, text in (("u.h", HEADER), ("a.cpp", SOURCE_A), ("glue.cpp", GLUE)):
            with open(os.path.join(d, name), "w") as f:
                f.write(text)
        objs = []
        for src in ("a.cpp", "glue.cpp"):
            o = os.path.join(d, src + ".o")
            run(tc.cxx + flags + ["-DUNIT=%d" % u, "-iquote", d, "-c", os.path.join(d, src), "-o", o], env)
            objs.append(o)
        out = os.path.join(work, "unit%d.o" % u)
        for cmd in decomp_platform.merge_commands(tc, ROOT, out, objs, fast):
            run(cmd, env)
        hg = hidden_globals(out)
        if hg:
            raise SystemExit("FAILED: hidden symbols still global in %s: %s" % (out, ", ".join(hg)))
        units.append(out)
    main_src = os.path.join(work, "main.cpp")
    with open(main_src, "w") as f:
        f.write(MAIN.replace("EXPECT0", str(expected(0))).replace("EXPECT1", str(expected(1))))
    main_o = os.path.join(work, "main.o")
    run(tc.cxx + ["-std=c++20", "-O2", "-c", main_src, "-o", main_o], env)
    exe = os.path.join(work, "stub" + (".exe" if decomp_platform.toolchain_kind(tc) == "llvm-mingw" else ""))
    run(tc.cxx + [main_o] + units + ["-o", exe], env)
    what = "fast (ThinLTO bitcode)" if fast else "plain"
    if not run_it:
        print("ok %s: units merged and isolated, program linked (not run on this host)" % what)
        return
    p = subprocess.run([exe], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = p.stdout.decode("utf-8", "replace").strip()
    want = "unit0 %d unit1 %d" % (expected(0), expected(1))
    if p.returncode or text != want:
        raise SystemExit("FAILED %s: the program printed %r (exit %d), expected %r" % (what, text, p.returncode, want))
    print("ok %s: %s" % (what, text))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--toolchain", required=True)
    ap.add_argument("--toolchain-dir")
    ap.add_argument("--mode", choices=("fast", "plain", "both"), default="both")
    ap.add_argument("--work")
    ap.add_argument("--no-run", action="store_true")
    a = ap.parse_args()
    if a.toolchain_dir:
        os.environ["WWHD_TOOLCHAIN_DIR"] = os.path.abspath(a.toolchain_dir)
    sys.path.insert(0, HERE)
    import setup
    work = a.work or tempfile.mkdtemp(prefix="wwhd-unit-merge-")
    os.makedirs(work, exist_ok=True)
    try:
        tc = setup.get_toolchain(a.toolchain, work, None)
        print("toolchain: %s (%s)" % (tc.desc, " ".join(tc.cxx)))
        run_it = not a.no_run and can_run(tc)
        for fast in ((True, False) if a.mode == "both" else (a.mode == "fast",)):
            one_mode(tc, os.path.join(work, "fast" if fast else "plain"), fast, run_it)
    finally:
        if not a.work:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
