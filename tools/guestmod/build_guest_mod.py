#!/usr/bin/env python3
"""Install-time build of a guest mod (Mod SDK v2 prototype, docs/mod-sdk-v2.md).

usage: build_guest_mod.py PACKAGE_DIR --out CACHE_DIR [--base 0x7F000000] [--cc "xcrun clang"] [--json]

PACKAGE_DIR holds manifest.json (kind "guest") and the mod's PowerPC relocatable ELF. The mod is
translated to C (guestmod.py) and compiled by the local compiler into a module
CACHE_DIR/<key>/<id>.<dylib|so|dll>, where <key> covers everything the module depends on: the ELF,
the base address, the translator and module ABI versions, the compiler and its flags. A cached module
is reused; a port update that changes the translator or the ABI gives a new key and so a rebuild.
The game's code is not involved: modules call the game through the runtime's dispatch.

This is the step the mod manager's `build_guest_mod` setup step runs. With --json the last line of
stdout is {"ok": true, "module": PATH, "cached": bool, ...} or {"ok": false, "error": TEXT} for the
player (TEXT is short and readable; the full compiler output goes to CACHE_DIR/<key>/build.log).
"""
import argparse
import hashlib
import json
import os
import platform
import re
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import guestmod  # noqa: E402
from builds import by_name, canonical_build

REPO = os.path.dirname(os.path.dirname(HERE))
CFLAGS = ["-O2", "-ffp-contract=off", "-fno-strict-aliasing", "-w", "-fPIC", "-shared"]


def module_ext():
    s = platform.system()
    return ".dll" if s == "Windows" else ".dylib" if s == "Darwin" else ".so"


def default_cc():
    if os.environ.get("CC"):
        return shlex.split(os.environ["CC"])
    if platform.system() == "Darwin":
        return ["xcrun", "clang"]
    return ["clang"]


def abi_version(include):
    with open(os.path.join(include, "wwhd_guest_abi.h"), encoding="utf-8") as f:
        for line in f:
            if line.startswith("#define WWHD_GUEST_ABI_VERSION"):
                return line.split()[2]
    raise guestmod.ModError("wwhd_guest_abi.h without an ABI version")


def package_elf(pkg):
    """Read only package-relative ELF paths; return the validated manifest and ELF bytes."""
    pkg = Path(pkg).resolve()
    with (pkg / "manifest.json").open(encoding="utf-8") as f:
        man = json.load(f)
    if not isinstance(man, dict) or man.get("kind") != "guest":
        raise guestmod.ModError("not a guest mod package")
    mod_id = man.get("id", "")
    if not isinstance(mod_id, str) or not re.fullmatch(r"[a-z0-9_][a-z0-9_.-]{0,63}", mod_id):
        raise guestmod.ModError("invalid guest mod ID")
    g = man.get("guest", {})
    if not isinstance(g, dict) or type(g.get("api_version")) is not int or g["api_version"] != 1:
        raise guestmod.ModError("unsupported guest API (this game supports 1)")
    name = g.get("elf", "mod.elf")
    if (not isinstance(name, str) or not name or "\\" in name or ":" in name or "\0" in name or
            any(part in ("", ".", "..") for part in name.split("/")) or Path(name).is_absolute()):
        raise guestmod.ModError("invalid guest ELF path")
    elf_path = pkg
    for part in name.split("/"):
        elf_path /= part
        if elf_path.is_symlink():
            raise guestmod.ModError("guest ELF paths may not use symlinks")
    if not elf_path.is_file() or elf_path.stat().st_size > 64 * 1024 * 1024:
        raise guestmod.ModError("guest ELF is missing or larger than 64 MiB")
    return man, elf_path.read_bytes()


def translator_for(elf, base, game_build=None):
    if type(base) is not int or base & 0xFFFF or not guestmod.REGION_START <= base < guestmod.REGION_END:
        raise guestmod.ModError("guest base must be 64 KiB aligned within the mod region")
    try:
        return guestmod.Translator(guestmod.Elf(elf), base, game_build)
    except (struct.error, IndexError, UnicodeError) as e:
        raise guestmod.ModError("malformed guest ELF: " + str(e)) from e


def inspect_package(pkg, base, game_build=None):
    man, elf = package_elf(pkg)
    t = translator_for(elf, base, game_build)
    return {"ok": True, "id": man["id"], "base": base, "memory_size": t.end - base,
            "allocation_size": (t.end - base + 0xFFFF) & ~0xFFFF,
            "elf_sha256": hashlib.sha256(elf).hexdigest()}


def cache_key(elf, mod_id, base, cc, include, game_build=None):
    version = subprocess.run(cc + ["--version"], capture_output=True, text=True)
    if version.returncode:
        raise guestmod.ModError("the local compiler is unavailable")
    h = hashlib.sha256()
    game_build = game_build or canonical_build()
    parts = [elf, mod_id.encode(), b"%08X" % base, guestmod.TRANSLATOR_VERSION.encode(),
             abi_version(include).encode(),
             json.dumps(vars(game_build), sort_keys=True, default=lambda v: vars(v)).encode(),
             json.dumps([cc, version.stdout, CFLAGS, module_ext()]).encode()]
    # Version strings alone miss edits between releases. Hash the actual translation/ABI inputs.
    inputs = [Path(__file__), Path(guestmod.__file__), Path(HERE).parent / "recomp" / "ppc2c.py",
              Path(include) / "ppc.h", Path(include) / "wwhd_guest_abi.h",
              Path(HERE).parent / "recomp" / "builds.py"]
    parts.extend(path.read_bytes() for path in inputs)
    for part in parts:
        h.update(hashlib.sha256(part).digest())
    return h.hexdigest()


def build(pkg, out, base, cc, include, game_build=None):
    man, elf = package_elf(pkg)
    t = translator_for(elf, base, game_build)
    key = cache_key(elf, man["id"], base, cc, include, game_build)
    d = os.path.join(out, key)
    mod = os.path.join(d, man["id"] + module_ext())
    metadata = {"key": key, "base": base, "game_build": t.build.name, "memory_size": t.end - base,
                "allocation_size": (t.end - base + 0xFFFF) & ~0xFFFF,
                "elf_sha256": hashlib.sha256(elf).hexdigest()}
    if os.path.isfile(mod):
        return {"ok": True, "module": mod, "cached": True, **metadata}
    os.makedirs(d, exist_ok=True)
    t0 = time.time()
    src = os.path.join(d, "module.c")
    with open(src, "w") as f:
        f.write(t.emit(man["id"]))
    t1 = time.time()
    cmd = cc + CFLAGS + ["-I", include, src, "-o", mod + ".tmp"]
    p = subprocess.run(cmd, capture_output=True, text=True)
    with open(os.path.join(d, "build.log"), "w") as f:
        f.write("$ %s\n%s%s" % (" ".join(cmd), p.stdout, p.stderr))
    if p.returncode != 0:
        raise guestmod.ModError("the local compiler could not build the mod (see %s)" % os.path.join(d, "build.log"))
    os.replace(mod + ".tmp", mod)
    return {"ok": True, "module": mod, "cached": False, **metadata, "translate_s": round(t1 - t0, 3),
            "compile_s": round(time.time() - t1, 3), "functions": len(t.entries), "hooks": len(t.hooks),
            "services": t.services}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package")
    ap.add_argument("--out", help="module cache directory (required unless --inspect)")
    ap.add_argument("--inspect", action="store_true", help="report memory layout without compiling")
    ap.add_argument("--build", default="USA", help="installed game build (USA or EU)")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=guestmod.REGION_START,
                    help="guest address of the mod (the mod manager assigns one per enabled mod)")
    compiler_args = ap.add_mutually_exclusive_group()
    compiler_args.add_argument("--cc-json", help="compiler argument vector as JSON (for setup/manager)")
    compiler_args.add_argument("--cc", help="compiler command (default: $CC, xcrun clang on macOS, clang)")
    ap.add_argument("--include", default=os.path.join(REPO, "runtime", "include"),
                    help="runtime headers (ppc.h, wwhd_guest_abi.h); sdk/include in a release")
    ap.add_argument("--zig-cache", help="setup-selected Zig global cache directory")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    try:
        game_build = by_name(a.build)
        if game_build is None:
            raise guestmod.ModError("unknown installed game build: " + a.build)
        if a.zig_cache:
            os.environ["ZIG_GLOBAL_CACHE_DIR"] = os.path.abspath(a.zig_cache)
        if a.inspect:
            r = inspect_package(a.package, a.base, game_build)
        elif not a.out:
            raise guestmod.ModError("--out is required when building")
        else:
            cc = json.loads(a.cc_json) if a.cc_json else shlex.split(a.cc) if a.cc else default_cc()
            if not isinstance(cc, list) or not cc or any(not isinstance(x, str) or not x or "\0" in x for x in cc):
                raise guestmod.ModError("invalid compiler argument vector")
            r = build(a.package, a.out, a.base, cc, a.include, game_build)
    except (guestmod.ModError, OSError, ValueError, KeyError) as e:
        r = {"ok": False, "error": str(e)}
    print(json.dumps(r) if a.json else ("inspected %s" % r["id"] if a.inspect and r["ok"] else
                                  "built %s" % r["module"] if r["ok"] else "error: " + r["error"]))
    sys.exit(0 if r["ok"] else 1)


if __name__ == "__main__":
    main()
