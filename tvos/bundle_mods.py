#!/usr/bin/env python3
"""Code mods for the Apple TV, iPhone and iPad app: build them on the Mac into WindWakerHD.app.

These devices can't build a code mod (no compiler; they run only code signed into the app), so the Mac does
what setup and the mod manager do there, for the app's platform (from its Info.plist: tvOS or iOS):

  1. Prepare the mods in a game on the Mac with code mods on (or headless, WWHD_MOD_MANAGER_DIR and the
     mod-manager test aids): install them, switch them on. That mod manager folder is the input.
  2. tvos/bundle_mods.py --app build/tvos/WindWakerHD.app --manager <that ModManager folder> [--sign <identity>]
     builds each switched-on guest mod for the app's platform with the port's builder (tools/guestmod/build_guest_mod.py) at
     the guest address the mod manager gave it, into the app's GuestMods (<id>.dylib, <id>.json), and copies the
     packages and the profile into the app's ModManager (with seed.txt, their digest).
  3. Sign and install the app as usual (tvos/README.md). --sign signs the modules first, with the app's identity.

The app needs game code translated with mod hooks (setup's code-mod build). On the device it copies the seed into
its mod manager storage when the seed changed (gfx/host_tvos.mm), and loads each module from the app
(mods/guest_mods.cpp bundled_module). Mod options change on the device as anywhere; another set of mods needs
another app.
"""
import argparse
import hashlib
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
if not (REPO / "tools" / "guestmod").is_dir():  # a release: this file is tools/apple/bundle_mods.py
    REPO = REPO.parent


def compiler(app):
    """clang for the app's platform and minimum OS (Info.plist: CFBundleSupportedPlatforms, MinimumOSVersion)"""
    info = plistlib.loads((app / "Info.plist").read_bytes())
    platform = (info.get("CFBundleSupportedPlatforms") or ["?"])[0]
    sdk, os_name = {"AppleTVOS": ("appletvos", "tvos"), "iPhoneOS": ("iphoneos", "ios")}.get(platform, (None, None))
    if not sdk:
        sys.exit("%s is for %s: only Apple TV, iPhone and iPad apps take bundled mods" % (app, platform))
    # WWHD_TVOS as in the app's own build: it moves the game's memory (ppc.h PPC_MEM_BASE)
    return platform, ["xcrun", "--sdk", sdk, "clang", "-target",
                      "arm64-apple-%s%s" % (os_name, info.get("MinimumOSVersion", "17.0")), "-DWWHD_TVOS=1"]


def plan(manager):
    """what goes into the app from the active profile: (profiles with the left-out mods switched off,
    [(id, package folder, guest base)] to build, [{id, name, reason}] left out). setup's apple_device.py
    shows the same plan before building."""
    profiles = json.loads((manager / "profiles.json").read_text(encoding="utf-8"))
    profile = profiles["profiles"][profiles["active"]]
    regions = profiles.get("guest_regions", {})
    out, left_out = [], []
    for mod_id, on in sorted(profile.get("enabled", {}).items()):
        if not on:
            continue
        package = manager / "Mods" / mod_id
        try:
            manifest = json.loads((package / "manifest.json").read_text(encoding="utf-8"))
        except (OSError, ValueError):
            manifest = None
        name = (manifest or {}).get("name") or mod_id
        if manifest is None:
            reason = "its package is missing from the mod manager"
        elif manifest.get("kind") != "guest":
            continue
        elif mod_id not in regions:
            # not set up on the Mac yet (the app would refuse it); setup's build starts the Mac's game to set it up
            reason = "it has no guest address yet (the game on this Mac gives it one when it starts with the mod on)"
        else:
            out.append((mod_id, package, int(regions[mod_id]["base"])))
            continue
        profile["enabled"][mod_id] = False  # off in the app's profile
        left_out.append({"id": mod_id, "name": name, "reason": reason, "needs_address": mod_id not in regions and manifest is not None})
    return profiles, out, left_out


def default_include():
    """the guest ABI headers: a release's sdk/include, else the checkout's runtime/include"""
    release = REPO / "sdk" / "include"
    return release if (release / "wwhd_guest_abi.h").is_file() else REPO / "runtime" / "include"


def guest_mod_builder():
    return REPO / "tools" / "guestmod" / "build_guest_mod.py"


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
    ap.add_argument("--include", type=Path, default=default_include(), help="the runtime's guest ABI headers")
    ap.add_argument("--sign", help="code signing identity for the modules (the app's)")
    a = ap.parse_args()
    if not (a.app / "Info.plist").is_file():
        sys.exit("%s is not an app" % a.app)
    platform, cc = compiler(a.app)
    profiles, mods, left_out = plan(a.manager)
    for m in left_out:
        print("%s is left out: %s" % (m["name"], m["reason"]))
    modules, seed = a.app / "GuestMods", a.app / "ModManager"
    for folder in (modules, seed):
        if folder.exists():
            shutil.rmtree(folder)
    modules.mkdir()
    (seed / "Mods").mkdir(parents=True)
    with tempfile.TemporaryDirectory() as cache:
        for mod_id, package, base in mods:
            r = subprocess.run([sys.executable, str(guest_mod_builder()), str(package),
                                "--base", str(base), "--build", a.build, "--json", "--out", cache,
                                "--include", str(a.include), "--cc-json", json.dumps(cc)],
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
            print("built %s for %s (guest base %08X)" % (mod_id, platform, base))
    for package in sorted((a.manager / "Mods").iterdir()):
        if package.is_dir():
            shutil.copytree(package, seed / "Mods" / package.name, symlinks=False)
    profiles.pop("guest_prepared", None)  # (the Mac's module paths)
    (seed / "profiles.json").write_text(json.dumps(profiles, indent=1) + "\n", encoding="utf-8")
    (seed / "seed.txt").write_text(seed_digest(seed), encoding="utf-8")
    print("%d mod(s) into %s" % (len(mods), a.app))


if __name__ == "__main__":
    main()
