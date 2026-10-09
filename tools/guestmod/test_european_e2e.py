#!/usr/bin/env python3
"""Opt-in EUR heart-ticker startup test; requires a hooked EUR binary and a playable save.

Game input is read only. Saves, settings, modules and shader caches stay in a new output
directory. This is a functional test, not a performance measurement or CI unit test.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--save", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--ppc-clang", default=os.environ.get("WWHD_PPC_CLANG", "clang"))
    parser.add_argument("--ppc-lld", default=os.environ.get("WWHD_PPC_LLD", "ld.lld"))
    args = parser.parse_args()
    if os.getloadavg()[0] > 30 or shutil.disk_usage(args.out.parent).free < 15e9:
        parser.error("wait for load1 <= 30 and at least 15 GB free")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    save, manager = out / "save", out / "manager"
    shutil.copytree(args.save, save)
    for path in save.rglob("*"):
        path.chmod(0o755 if path.is_dir() else 0o644)
    package = manager / "Mods/heart-ticker"
    package.mkdir(parents=True)
    shutil.copy2(ROOT / "examples/guest-mods/heart-ticker/manifest.json", package / "manifest.json")
    subprocess.run([args.ppc_clang, "--target=powerpc-unknown-eabi", "-mcpu=750", "-O2", "-ffreestanding",
                    "-fno-builtin", "-nostdlib", "-fno-jump-tables", "-ffunction-sections", "-fdata-sections",
                    "-I" + str(ROOT / "runtime/guest/include"), "-c",
                    str(ROOT / "examples/guest-mods/heart-ticker/mod.c"), "-o", str(package / "mod.o")], check=True)
    subprocess.run([args.ppc_lld, "-m", "elf32ppc", "-r", str(package / "mod.o"),
                    "-o", str(package / "mod.elf")], check=True)
    (manager / "profiles.json").write_text(json.dumps({"format_version": 1, "active": "Default",
        "profiles": {"Default": {"enabled": {"heart-ticker": True}}}}))
    config = out / "guest-sdk.json"
    config.write_text(json.dumps({"format_version": 1, "python": [sys.executable],
        "compiler": ["clang"], "builder": str(ROOT / "tools/guestmod/build_guest_mod.py"),
        "include": str(ROOT / "runtime/include")}))
    env = {k: v for k, v in os.environ.items() if not k.startswith("WWHD_")}
    env.update({"WWHD_CODE_MODS": "1", "WWHD_NO_AUDIO": "1", "WWHD_NO_GAMEPAD": "1",
        "WWHD_NO_HOST_INPUT": "1", "WWHD_HIDDEN_WINDOWS": "1", "WWHD_UNCAPPED": "1",
        "WWHD_RENDERER_RUNTIME": "metal", "WWHD_MOD_MANAGER_DIR": str(manager),
        "WWHD_TEST_TRUST_NATIVE_MODS": "heart-ticker", "WWHD_GUEST_BUILD_CONFIG": str(config),
        "WWHD_SHADER_CACHE": str(out / "shaders.bin"), "WWHD_SETTINGS": str(out / "settings.ini"),
        "WWHD_DISPLAY_SETTINGS": str(out / "display.plist"), "XDG_CONFIG_HOME": str(out / "config"),
        "WWHD_STATE_DIR": str(out / "states"), "WWHD_TEST_ORIGIN": "3000", "WWHD_TEST_END": "12",
        "WWHD_PRESS": ",".join(f"{frame}-{frame+8}:8000" for frame in range(120, 1800, 30))})
    with (out / "run.log").open("w") as log:
        proc = subprocess.Popen([str(args.binary.resolve()), "--game", str(args.game.resolve()),
                                 "--save", str(save)], cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 180
            while proc.poll() is None and not (out / "test_done").exists() and time.monotonic() < deadline:
                time.sleep(1)
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
    log = (out / "run.log").read_text(errors="replace")
    checks = {"european_build": "(EU build," in log,
              "module_loaded": "[guestmods] loaded " in log,
              "return_hook": "heart-ticker: first return hook" in log,
              "life_updates": "heart-ticker: life (quarter hearts)" in log,
              "game_call": "heart-ticker: smoothed by the game's cLib_addCalc2" in log,
              "completed": (out / "test_done").exists()}
    (out / "result.json").write_text(json.dumps(checks, indent=2) + "\n")
    print(json.dumps(checks))
    raise SystemExit(0 if all(checks.values()) else 1)


if __name__ == "__main__":
    main()
