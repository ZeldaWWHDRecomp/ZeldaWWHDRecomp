#!/usr/bin/env python3
"""The packaged Metal shader library: every shader of your game compiled ahead of time into one
metallib, so the game compiles none at run time and keeps no shader text (gfx/metal_draw.mm,
"packaged shader library").

Setup builds it as part of shader preparation (shader_prebuild.py) when Xcode 26 or newer is
installed; it downloads Xcode's Metal toolchain itself the first time (`xcodebuild -downloadComponent
MetalToolchain`, a separate download since Xcode 26). An older Xcode's compiler lacks the safe math
mode the game compiles with (-fmetal-math-mode=safe), so without Xcode 26 setup skips it and the game
compiles its shaders during play, as before. The Metal warm writes every
shader's MSL (WWHD_SHADERLIB_SOURCES), this compiles them as the game does at run time (Metal 3.0,
safe math) and links shaders.metallib into the generation, next to headstart.bin, where the game
finds it. Built from your own game's shaders, it stays on your devices.

By hand (an Apple TV or iPhone build's resources):
  shader_library.py <sources folder> <out.metallib> [--sdk macosx|appletvos|iphoneos] [-j N]
A shaders.metallib built into an Apple TV or iPhone app contains shaders made from your game: such a
build is for your own devices and must not be distributed.
"""
import argparse
import concurrent.futures
import glob
import os
import plistlib
import re
import subprocess
import sys
import tempfile
import time

MIN_OS = {"macosx": "-mmacosx-version-min=13.0", "appletvos": "-mtvos-version-min=17.0",
          "iphoneos": "-mios-version-min=17.0"}
TOOLCHAIN_HINT = "xcodebuild -downloadComponent MetalToolchain"

# A function definition at the start of a line: a return type, its name, "(" (not a statement such as
# "else if (").
FUNC = re.compile(r"^([A-Za-z_][\w:<>]*(?:[ \t]+[\w:<>*&]+)*)[ \t]+\**([A-Za-z_]\w*)[ \t]*\(")
NOT_A_TYPE = {"else", "if", "return", "for", "while", "do", "switch", "case", "static", "inline", "fragment",
              "vertex", "kernel", "template", "struct", "using", "typedef", "namespace", "constexpr"}


def private_helpers(src):
    """Each shader's helper functions (clampFI32, mul_nonIEEE, ...) made static: one library holds
    thousands of shaders, which define the same helpers and would collide when linked. The entry
    point (fragment / vertex / kernel) stays visible: the game finds it by name."""
    out = []
    for line in src.split("\n"):
        m = FUNC.match(line)
        if m and m.group(1).split()[0] not in NOT_A_TYPE:
            line = "static " + line
        out.append(line)
    return "\n".join(out)


def xcode_dir():
    """Xcode's developer folder, or None. The active developer folder can be the Command Line Tools
    (setup's own requirement), which have no Metal compiler, while Xcode is installed too."""
    if os.environ.get("DEVELOPER_DIR"):
        return os.environ["DEVELOPER_DIR"]
    try:
        active = subprocess.run(["xcode-select", "-p"], capture_output=True, text=True, timeout=30).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        active = ""
    if ".app/Contents/Developer" in active:
        return active
    found = sorted(glob.glob("/Applications/Xcode*.app/Contents/Developer"))
    return found[-1] if found else None


def xcode_major():
    """Xcode's major version (26 for Xcode 26.1), or 0 without Xcode"""
    xcode = xcode_dir()
    if not xcode:
        return 0
    try:
        with open(os.path.join(os.path.dirname(xcode), "version.plist"), "rb") as f:
            return int(str(plistlib.load(f).get("CFBundleShortVersionString", "0")).split(".")[0])
    except (OSError, ValueError, plistlib.InvalidFileException):
        return 0


def _env():
    env = os.environ.copy()
    xcode = xcode_dir()
    if xcode:
        env["DEVELOPER_DIR"] = xcode
    return env


def toolchain_available(sdk="macosx"):
    """Apple's Metal compiler can run (Xcode 26 and later ship it as a separate download)."""
    try:
        return subprocess.run(["xcrun", "-sdk", sdk, "metal", "--version"], capture_output=True,
                              timeout=60, env=_env()).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def ensure_toolchain(log=print, cancel=lambda: False, sdk="macosx"):
    """The Metal compiler, downloaded through Xcode the first time when it is missing. False without
    Xcode, or when the download failed or was cancelled."""
    if toolchain_available(sdk):
        return True
    if not xcode_dir():
        return False
    try:
        child = subprocess.Popen(["xcodebuild", "-downloadComponent", "MetalToolchain"], env=_env(),
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    except OSError:
        return False
    while child.poll() is None:
        if cancel():
            child.terminate()
            child.wait()
            raise InterruptedError("Metal toolchain download cancelled")
        line = child.stdout.readline()
        if line.strip():
            log(line.rstrip())
        elif not line:
            time.sleep(0.2)
    for line in child.stdout:
        if line.strip():
            log(line.rstrip())
    return child.returncode == 0 and toolchain_available(sdk)


def build(sources, out, sdk="macosx", jobs=None, progress=None, cancel=lambda: False):
    """Compiles every .metal of `sources` and links `out` (written in place only when complete).
    progress(done, total) as the compiles finish. Returns (shaders, failed); raises when nothing
    could be built. A shader that fails to compile is left out: the game compiles it itself."""
    files = sorted(f for f in os.listdir(sources) if f.endswith(".metal"))
    if not files:
        raise RuntimeError("no shader sources to build")
    env = _env()
    with tempfile.TemporaryDirectory(prefix="wwhd-shaderlib-") as tmp:
        def compile_one(name):
            if cancel():
                return name, None
            src, air = os.path.join(tmp, name), os.path.join(tmp, name[:-6] + ".air")
            with open(os.path.join(sources, name)) as f, open(src, "w") as g:
                g.write(private_helpers(f.read()))
            r = subprocess.run(["xcrun", "-sdk", sdk, "metal", "-c", "-std=metal3.0", "-fmetal-math-mode=safe",
                                MIN_OS[sdk], src, "-o", air], capture_output=True, text=True, env=env)
            return name, air if r.returncode == 0 else None

        airs = []
        with concurrent.futures.ThreadPoolExecutor(jobs or os.cpu_count() or 8) as pool:
            for done, (_, air) in enumerate(pool.map(compile_one, files), 1):
                if air:
                    airs.append(air)
                if progress:
                    progress(done, len(files))
        if cancel():
            raise InterruptedError("Shader library skipped")
        if not airs:
            raise RuntimeError("no shader compiled")
        # one link through a response file (the list is too long for a command line)
        listing = os.path.join(tmp, "airs.txt")
        with open(listing, "w") as f:
            f.write("\n".join(airs))
        partial = str(out) + ".partial"
        r = subprocess.run(["xcrun", "-sdk", sdk, "metallib", "@" + listing, "-o", partial],
                           capture_output=True, text=True, env=env)
        if r.returncode:
            raise RuntimeError("metallib: " + (r.stderr or r.stdout).strip()[-2000:])
        os.replace(partial, out)
    return len(airs), len(files) - len(airs)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("sources")
    ap.add_argument("out")
    ap.add_argument("--sdk", default="macosx", choices=sorted(MIN_OS))
    ap.add_argument("-j", "--jobs", type=int)
    a = ap.parse_args()
    if not ensure_toolchain(sdk=a.sdk):
        sys.exit("the Metal toolchain is not available: install Xcode, then " + TOOLCHAIN_HINT)
    shown = [0]

    def progress(done, total):
        if done * 10 // total != shown[0]:
            shown[0] = done * 10 // total
            print(f"{done} of {total} compiled", flush=True)

    shaders, failed = build(a.sources, a.out, a.sdk, a.jobs, progress)
    print(f"{a.out}: {shaders} shaders ({failed} failed to compile and are left to the game), "
          f"{os.path.getsize(a.out) / 2**20:.1f} MB")


if __name__ == "__main__":
    main()
