#!/usr/bin/env python3
"""Code mods for the Apple TV app: build them on the Mac into WindWakerHD.app.

An Apple TV can't build a code mod (no compiler; it runs only code signed into the app), so the Mac does what
setup and the mod manager do there, for tvOS:

  1. Prepare the mods in a game on the Mac with code mods on (or headless, WWHD_MOD_MANAGER_DIR and the
     mod-manager test aids): install them, switch them on. That mod manager folder is the input.
  2. tvos/bundle_mods.py --app build/tvos/WindWakerHD.app --manager <that ModManager folder> [--sign <identity>]
     builds each switched-on guest mod for tvOS with the port's builder (tools/guestmod/build_guest_mod.py) at
     the guest address the mod manager gave it, into the app's GuestMods (<id>.dylib, <id>.json), and copies the
     packages and the profile into the app's ModManager (with seed.txt, their digest).
  3. Sign and install the app as usual (tvos/README.md). --sign signs the modules first, with the app's identity.

The app needs game code translated with mod hooks (setup's code-mod build). On the TV it copies the seed into
its mod manager storage when the seed changed (gfx/host_tvos.mm), and loads each module from the app
(mods/guest_mods.cpp bundled_module). Mod options change on the TV as anywhere; another set of mods needs
another app.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
# WWHD_TVOS as in the app's own build: it moves the game's memory (ppc.h PPC_MEM_BASE)
TVOS_CC = ["xcrun", "--sdk", "appletvos", "clang", "-target", "arm64-apple-tvos17.0", "-DWWHD_TVOS=1"]


def guest_mods(manager):
    """the switched-on guest mods of the active profile: [(id, package folder, guest base)]"""
    profiles = json.loads((manager / "profiles.json").read_text(encoding="utf-8"))
    profile = profiles["profiles"][profiles["active"]]
    regions = profiles.get("guest_regions", {})
    out = []
    for mod_id, on in sorted(profile.get("enabled", {}).items()):
        package = manager / "Mods" / mod_id
        manifest = json.loads((package / "manifest.json").read_text(encoding="utf-8"))
        if not on or manifest.get("kind") != "guest":
            continue
        if mod_id not in regions:
            sys.exit("%s has no guest address yet: start the game once with it on (its setup), then run this again" % mod_id)
        out.append((mod_id, package, int(regions[mod_id]["base"])))
    return profiles, out


def seed_digest(folder):
    h = hashlib.sha256()
    for f in sorted(p for p in folder.rglob("*") if p.is_file()):
        h.update(str(f.relative_to(folder)).encode() + b"\0" + f.read_bytes())
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", required=True, type=Path, help="WindWakerHD.app")
    ap.add_argument("--manager", required=True, type=Path, help="the prepared ModManager folder")
    ap.add_argument("--build", default="USA", help="the game build (USA or EU), as the app's game files")
    ap.add_argument("--include", type=Path, default=REPO / "runtime" / "include", help="the runtime's guest ABI headers")
    ap.add_argument("--sign", help="code signing identity for the modules (the app's)")
    a = ap.parse_args()
    if not (a.app / "Info.plist").is_file():
        sys.exit("%s is not an app" % a.app)
    profiles, mods = guest_mods(a.manager)
    modules, seed = a.app / "GuestMods", a.app / "ModManager"
    for folder in (modules, seed):
        if folder.exists():
            shutil.rmtree(folder)
    modules.mkdir()
    (seed / "Mods").mkdir(parents=True)
    with tempfile.TemporaryDirectory() as cache:
        for mod_id, package, base in mods:
            r = subprocess.run([sys.executable, str(REPO / "tools" / "guestmod" / "build_guest_mod.py"), str(package),
                                "--base", str(base), "--build", a.build, "--json", "--out", cache,
                                "--include", str(a.include), "--cc-json", json.dumps(TVOS_CC)],
                               capture_output=True, text=True)
            result = json.loads(r.stdout or "{}")
            if r.returncode or not result.get("ok"):
                sys.exit("%s: %s" % (mod_id, result.get("error") or r.stderr.strip()))
            shutil.copyfile(result["module"], modules / (mod_id + ".dylib"))
            (modules / (mod_id + ".json")).write_text(json.dumps({
                "elf_sha256": result["elf_sha256"], "allocation_size": result["allocation_size"],
                "base": base, "build": a.build}, indent=1) + "\n", encoding="utf-8")
            if a.sign:
                subprocess.run(["codesign", "--force", "--sign", a.sign, "--timestamp=none",
                                str(modules / (mod_id + ".dylib"))], check=True)
            print("built %s for tvOS (guest base %08X)" % (mod_id, base))
    for package in sorted((a.manager / "Mods").iterdir()):
        if package.is_dir():
            shutil.copytree(package, seed / "Mods" / package.name, symlinks=False)
    profiles.pop("guest_prepared", None)  # (the Mac's module paths)
    (seed / "profiles.json").write_text(json.dumps(profiles, indent=1) + "\n", encoding="utf-8")
    (seed / "seed.txt").write_text(seed_digest(seed), encoding="utf-8")
    print("%d mod(s) into %s" % (len(mods), a.app))


if __name__ == "__main__":
    main()
