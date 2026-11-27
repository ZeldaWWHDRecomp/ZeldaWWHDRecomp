#!/usr/bin/env python3
"""Windows compile check of the decomp version (issue #678; CI: .github/workflows/decomp.yml, job windows-compile):
every source of every game unit compiled with the pinned llvm-mingw and setup's exact Windows unit options, as setup's
decomp step does on a player's PC, without the game and without a Windows machine (the Linux- or macOS-hosted
llvm-mingw of the same release, cross-compiling). Catches what only the Windows build of the decompilation trips over:
POSIX-only headers (<dlfcn.h>), libc++ / mingw-w64 differences (a missing <utility>), COFF-only link clashes.

usage: windows_compile_check.py --toolchain LLVM_MINGW_DIR [--triple T] [--mode fast|plain] [--decomp DIR]
                                [--work DIR] [--jobs N] [--link-smoke N] [--only SUBSTRING ...] [--list]

  --toolchain DIR  an unpacked llvm-mingw (its bin/TRIPLE-clang++); the same release as tools/installer/toolchains.json
  --triple T       x86_64-w64-mingw32 (default) or aarch64-w64-mingw32 (Windows on ARM)
  --mode           fast (default: what setup builds) or plain (setup --decomp-plain; the only one on Windows on ARM)
  --decomp DIR     the decompilation (default: the `decomp` submodule)
  --work DIR       working folder (default: build/windows-compile-check-TRIPLE-MODE; emptied first)
  --link-smoke N   also merge N small game units as setup does on Windows (unit_ir.py --native through the
                   toolchain's LLVM library) and link them into one program, so duplicate symbols between units
                   (#684: `.weak.gmem_frame_info.default..refptr.g_decomp_call_tab`) fail here; unresolved symbols (the
                   runtime's) are allowed. Default 5; 0: off.
  --only S         compile only the sources whose path contains S (repeatable; a quick re-check)
  --list           print the compile jobs (source, -iquote) and exit

What is compiled, step by step as tools/installer/decomp_setup.py build_once does it:
  1. the unit options: a CMake configure of this repository with WWHD_DECOMP_FLAGS_ONLY (CMakeLists.txt) and the given
     llvm-mingw, then tools/release/package.py's decomp_compile_flags, i.e. the manifest's "unit_flags" a Windows
     release built from this commit gives setup, plus decomp_platform.unit_flags_extra.
  2. tools/decomp_build/gen.py with setup's arguments and --no-game: the units, the glued sources, the fast-mode header
     copies (hinc) and the model data headers. Without the game, the glue's strings are placeholders, the model data is
     zeros and autoglue's checks against the game are off; the declarations and the code are the same as with the game.
  3. every source of every unit with setup's command (-iquote of a glued copy), each distinct command once (a source
     in several units compiles alike there: without the native-call tables below the commands are equal), plus two
     units' glue files (glue/0.cpp, the shared thread block's owner, and glue/1.cpp; the others differ only by name).
Not compiled: the native-call tables native/N.h (native_rules.py; they need the game translated with the unit list,
which needs the game) and hooks*.cpp (port code, compiled with the runtime's options; built in the release job). An
error therefore never comes from a game-derived file. Exit status 1 when anything fails; each failure names the source
(and the first error's file and line, a GitHub annotation in Actions)."""
import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import time
from types import SimpleNamespace

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "installer"))
sys.path.insert(0, os.path.join(ROOT, "tools", "release"))
import decomp_platform  # noqa: E402
import decomp_setup  # noqa: E402

TRIPLES = ("x86_64-w64-mingw32", "aarch64-w64-mingw32")
CMAKE_PROCESSOR = {"x86_64-w64-mingw32": "AMD64", "aarch64-w64-mingw32": "ARM64"}
GLUE_UNITS = 2  # glue/0.cpp and glue/1.cpp (step 3 above)
ERROR_LINE = re.compile(r"^(.*?):(\d+):(?:(\d+):)? (?:fatal )?error: (.*)$")


def fwd(p):
    return p.replace("\\", "/")


def local_flags(portable, paths):
    """the manifest's unit_flags (placeholders {sdk}/include, {decomp}, {tools}, {out}) as setup substitutes them, with
    {sdk}/include = this repository's runtime/include (the release copies it there) and {sdk}/decomp/include =
    runtime/src; any other {sdk} path is an error (a unit option the release would have to ship)"""
    m = dict(paths)
    out = []
    for a in portable:
        a = a.replace("{sdk}/decomp/include", fwd(os.path.join(ROOT, "runtime", "src")))
        a = a.replace("{sdk}/include", fwd(os.path.join(ROOT, "runtime", "include")))
        if "{sdk}" in a:
            raise SystemExit("windows_compile_check: unexpected unit option %s" % a)
        out.append(decomp_setup.subst(a, m))
    return out


def compile_jobs(units, out, glue=range(GLUE_UNITS), only=None):
    """[(unit index, source, extra options)]: every unit's sources as decomp_setup.build_once lists them (its native-call
    table pre-included when there is one, -iquote of a glued copy), with the glue file of the units in `glue`.
    only: substrings of the source paths to keep (no glue files then)."""
    jobs, glue = [], set(glue)
    for i, u in enumerate(units):
        table = os.path.join(out, "native", "%d.h" % i)
        native = ["-include", fwd(table)] if os.path.isfile(table) else []
        for s in u["sources"] + ([{"path": u["glue"]}] if i in glue and not only else []):
            if only and not any(o in fwd(s["path"]) for o in only):
                continue
            jobs.append((i, s["path"], native + (["-iquote", s["quote"]] if s.get("quote") else [])))
    return jobs


def neutral_tables(out, fast):
    """the units' native-call tables native/K.h without a single native call (setup's come from native_rules.py with
    the game translated by the unit list): native_rules.unit_headers with no rules, so every call takes the guest path
    and every VERIFY function gets an empty GABI_NE_ADDR; fast mode's tables also declare the empty Sa / Acc lists
    (--stackarg, --accessor in decomp_setup.FAST_RULES_ARGS)"""
    import native_rules
    native_rules.unit_headers(out, [], fast, None, fast and "--stackarg" in decomp_setup.FAST_RULES_ARGS,
                              {} if fast and "--accessor" in decomp_setup.FAST_RULES_ARGS else None)


def shown_path(path, decomp, out):
    """a compiler's file name as the repository's path: a glued copy or a fast-mode header copy as the decompilation's
    own file (decomp/...), the rest relative to the repository when below it"""
    p = fwd(os.path.normpath(path))
    for base, to in ((os.path.join(out, "glued"), decomp), (os.path.join(out, "hinc"),
                                                            os.path.join(decomp, "wwhd_src", "include"))):
        b = fwd(os.path.normpath(base)) + "/"
        if p.startswith(b):
            p = fwd(os.path.join(to, p[len(b):]))
            break
    r = fwd(os.path.normpath(ROOT)) + "/"
    return p[len(r):] if p.startswith(r) else p


def first_error(text, decomp, out):
    """(file, line, message) of the compiler output's first error, or None"""
    for ln in text.splitlines():
        m = ERROR_LINE.match(ln.strip())
        if m:
            return shown_path(m.group(1), decomp, out), int(m.group(2)), m.group(4)
    return None


def toolchain(args):
    cxx = os.path.join(args.toolchain, "bin", args.triple + "-clang++" + decomp_platform.EXE_SUFFIX)
    if not os.path.isfile(cxx):
        raise SystemExit("windows_compile_check: no %s (--toolchain: an unpacked llvm-mingw)" % cxx)
    cc = os.path.join(args.toolchain, "bin", args.triple + "-clang" + decomp_platform.EXE_SUFFIX)
    return SimpleNamespace(kind="llvm-mingw", cxx=[cxx], cc=[cc], env=None)


def run(cmd, what, **kw):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)
    if p.returncode:
        sys.stdout.write(p.stdout.decode("utf-8", "replace"))
        raise SystemExit("windows_compile_check: %s failed (%d): %s" % (what, p.returncode, " ".join(cmd)))
    return p.stdout.decode("utf-8", "replace")


def unit_flags(args, tc, work, fast):
    """step 1: the release's unit_flags for this commit and toolchain (portable form)"""
    import package
    b = os.path.join(work, "flags")
    cmd = ["cmake", "-S", ROOT, "-B", b, "-DCMAKE_SYSTEM_NAME=Windows",
           "-DCMAKE_SYSTEM_PROCESSOR=" + CMAKE_PROCESSOR[args.triple], "-DCMAKE_BUILD_TYPE=Release",
           "-DCMAKE_C_COMPILER=" + tc.cc[0], "-DCMAKE_CXX_COMPILER=" + tc.cxx[0], "-DWWHD_DECOMP=ON",
           "-DWWHD_DECOMP_RUNTIME_ONLY=ON", "-DWWHD_DECOMP_FAST=" + ("ON" if fast else "OFF"),
           "-DWWHD_DECOMP_FLAGS_ONLY=ON", "-DWWHD_DECOMP_CCACHE=OFF", "-DWWHD_DECOMP_DIR=" + args.decomp]
    if shutil.which("ninja"):
        cmd += ["-G", "Ninja"]
    run(cmd, "configuring the unit options (cmake WWHD_DECOMP_FLAGS_ONLY)")
    flags, _ = package.decomp_compile_flags(b, args.decomp, hooks=False)
    return flags


def link_smoke(args, tc, work, units, objs, fast):
    """--link-smoke: unit 0 (the shared thread block's owner) and the smallest other units, merged as setup merges them
    on Windows and linked into one program (duplicate definitions fail; the runtime's symbols may stay unresolved)"""
    lib = sorted(f for d in ("bin", "lib") if os.path.isdir(os.path.join(args.toolchain, d))
                 for f in [os.path.join(args.toolchain, d, x) for x in os.listdir(os.path.join(args.toolchain, d))]
                 if re.match(r"libLLVM(-\d+)?\.(dll|so|dylib)$|libLLVM\.so\.\d+", os.path.basename(f)))
    if not lib:
        raise SystemExit("windows_compile_check: no LLVM library (libLLVM*) in %s for the unit merge" % args.toolchain)
    os.environ["WWHD_DECOMP_LIBLLVM"] = lib[-1]  # (decomp_platform.libllvm looks for a DLL, on Windows)
    env = dict(os.environ)
    merged = []
    for i in smoke_units(units, args.link_smoke):
        u = units[i]
        out = os.path.join(work, "smoke", "unit%d.o" % i)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        uobjs = objs[i]
        t0 = time.time()
        for cmd in decomp_platform.merge_commands(tc, ROOT, out, uobjs, fast):
            run(cmd, "merging unit %s (%s)" % (u["name"], os.path.basename(cmd[1])), env=env)
        print("link smoke: unit %d %s merged (%d objects, %.1f s)" % (i, u["name"], len(uobjs), time.time() - t0),
              flush=True)
        merged.append(out)
    main = os.path.join(work, "smoke", "main.cpp")
    with open(main, "w") as f:
        f.write("int main() { return 0; }\n")
    exe = os.path.join(work, "smoke", "smoke.exe")
    p = subprocess.run(tc.cxx + [main] + merged + ["-o", exe, "-Wl,-Xlink=/force:unresolved"],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = p.stdout.decode("utf-8", "replace")
    dups = sorted(set(re.findall(r"duplicate symbol: (\S+)", text)))
    if p.returncode:
        print(text)
        print("FAILED: link smoke of %d merged units%s" % (len(merged), (": duplicate symbols " + ", ".join(dups))
                                                           if dups else ""))
        if os.environ.get("GITHUB_ACTIONS"):
            print("::error title=decomp Windows link smoke::%d merged units do not link%s" % (
                len(merged), (": duplicate " + ", ".join(dups[:5])) if dups else ""))
        return False
    print("link smoke: %d merged units link into one program (%d symbols of the runtime left unresolved)"
          % (len(merged), len(set(re.findall(r"undefined symbol: (\S+)", text)))))
    return True


def smoke_units(units, n):
    """unit 0 and the n-1 units with the fewest sources (cheap to merge), in unit order"""
    if n <= 0 or not units:
        return []
    rest = sorted(range(1, len(units)), key=lambda i: (len(units[i]["sources"]), i))[:n - 1]
    return [0] + sorted(rest)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--toolchain", required=True)
    ap.add_argument("--triple", default=TRIPLES[0], choices=TRIPLES)
    ap.add_argument("--mode", default="fast", choices=decomp_setup.MODES)
    ap.add_argument("--decomp", default=os.path.join(ROOT, "decomp"))
    ap.add_argument("--work")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--link-smoke", type=int, default=5)
    ap.add_argument("--only", action="append")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()
    args.toolchain, args.decomp = os.path.abspath(args.toolchain), os.path.abspath(args.decomp)
    if not os.path.isfile(os.path.join(args.decomp, "tools", "verify", "include", "gabi.h")):
        raise SystemExit("windows_compile_check: no decompilation at %s (git submodule update --init decomp)"
                         % args.decomp)
    fast = args.mode == "fast"
    work = os.path.abspath(args.work or os.path.join(ROOT, "build", "windows-compile-check-%s-%s" % (
        args.triple.split("-")[0], args.mode)))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    tc = toolchain(args)
    t_all = time.time()
    print(run(tc.cxx + ["--version"], "the compiler").splitlines()[0])

    t0 = time.time()
    portable = unit_flags(args, tc, work, fast)
    out = os.path.join(work, "gen")
    gen = [sys.executable, os.path.join(HERE, "gen.py"), args.decomp, out, "--no-game", "--jobs", str(args.jobs),
           "--game-build", "--hook-shard", str(decomp_setup.HOOK_SHARD)] + (decomp_setup.FAST_GEN_ARGS if fast else [])
    sys.stdout.write(run(gen, "gen.py --no-game"))
    with open(os.path.join(out, "units.json")) as f:
        units = json.load(f)["units"]
    flags = local_flags(portable, {"decomp": fwd(args.decomp), "tools": fwd(os.path.join(ROOT, "tools")),
                                   "out": fwd(out)}) + decomp_platform.unit_flags_extra(tc, fast)
    print("unit options (%s, %s mode): %s" % (args.triple, args.mode, " ".join(flags)))
    neutral_tables(out, fast)
    smoke = [] if args.only else smoke_units(units, args.link_smoke)
    jobs = compile_jobs(units, out, set(range(GLUE_UNITS)) | set(smoke), args.only)
    if args.list:
        for i, src, extra in jobs:
            print("%s %s  (%s)" % (src, " ".join(extra), units[i]["name"]))
        return 0
    print("prepared in %.0f s: %d game units, %d compile jobs" % (time.time() - t0, len(units), len(jobs)), flush=True)

    os.makedirs(os.path.join(work, "obj"))
    objs = {i: [] for i in smoke}  # the link smoke's objects, per unit (the others are not kept)

    def one(k):
        i, src, extra = jobs[k]
        obj = os.devnull
        if i in objs:
            obj = os.path.join(work, "obj", "%d_%s.o" % (k, os.path.basename(src)))
            objs[i].append(obj)
        p = subprocess.run(tc.cxx + flags + extra + ["-c", fwd(src), "-o", fwd(obj)], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT)
        return k, p.returncode, p.stdout.decode("utf-8", "replace")

    t0 = time.time()
    failed = []
    with concurrent.futures.ThreadPoolExecutor(max(1, args.jobs)) as ex:
        for n, (k, rc, text) in enumerate(ex.map(one, range(len(jobs))), 1):
            if rc:
                failed.append((k, text))
            if n % 250 == 0 or n == len(jobs):
                print("compiled %d of %d (%d failed, %.0f s)" % (n, len(jobs), len(failed), time.time() - t0),
                      flush=True)
    for k, text in failed:
        i, src, _ = jobs[k]
        err = first_error(text, args.decomp, out)
        print("\nFAILED: %s (game unit %s)" % (shown_path(src, args.decomp, out), units[i]["name"]))
        lines = [ln for ln in text.splitlines() if ln.strip()]
        print("\n".join("    " + ln for ln in lines[:12]) + ("\n    ..." if len(lines) > 12 else ""))
        if os.environ.get("GITHUB_ACTIONS") and err:
            print("::error file=%s,line=%d,title=decomp Windows compile (%s)::%s: %s" % (
                err[0], err[1], args.triple, shown_path(src, args.decomp, out), err[2]))
    ok = not failed
    bad_units = {jobs[k][0] for k, _ in failed}
    if smoke and not bad_units & set(smoke):  # (also after failures elsewhere: both kinds of defect in one run)
        ok = link_smoke(args, tc, work, units, objs, fast) and ok
    elif smoke:
        print("link smoke: skipped (a source of its units failed)")
    print("\n%s: %d of %d compile jobs failed (%s, %s mode, decomp %s), %.0f s in all" % (
        "FAILED" if not ok else "OK", len(failed), len(jobs), args.triple, args.mode,
        decomp_setup_rev(args.decomp), time.time() - t_all))
    if failed:
        print("failing sources: " + ", ".join(sorted({shown_path(jobs[k][1], args.decomp, out) for k, _ in failed})))
    return 0 if ok else 1


def decomp_setup_rev(decomp):
    try:
        return subprocess.run(["git", "-C", decomp, "rev-parse", "--short=8", "HEAD"], capture_output=True,
                              text=True).stdout.strip() or "?"
    except OSError:
        return "?"


if __name__ == "__main__":
    sys.exit(main())
