#!/usr/bin/env python3
"""Opt-in local HUD frame check using the player's own game, saves and full state.

Requires Pillow. Outputs contain private game images: never upload or package them.
This is a functional check, not a timing benchmark. Each invocation runs one case.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bench"))
from run_bench import other_games as running_games, other_benchmarks


def other_games(own_pid=None):
    return running_games(own_pid) + other_benchmarks()


def disk_ok(path):
    if shutil.disk_usage(path).free < 15 * 1024**3:
        raise RuntimeError("free disk is below 15 GiB")


def inspect_frames(root, first, count, package):
    from PIL import Image
    # Exact original-art colours, sampled in each intended HUD region. These
    # checks detect disappearance; screenshots still need a visual placement audit.
    colours = [(46, 177, 83), (226, 62, 58), (45, 125, 224),
               (239, 193, 52), (155, 168, 184)]
    regions = [(1155, 125, 50, 50), (1122, 171, 38, 38),
               (1122, 91, 38, 38), (1084, 131, 38, 38), (1178, 72, 56, 32)]
    observations = []
    screens = ("tv", "drc") if package == "hud-demo" else ("tv",)
    for frame in range(first, first + count):
        for screen in screens:
            suffix = "_present.png" if screen == "tv" else "_present_drc.png"
            path = root / ("frame_%d%s" % (frame, suffix))
            with Image.open(path) as image:
                image = image.convert("RGB")
                scale = image.height / 720
                if package == "hud-demo":
                    boxes, targets = [(28, 108, 32, 32)], [(242, 178, 63)]
                    offset = 0
                else:
                    boxes, targets = regions, colours
                    offset = image.width - 1280 * scale
                counts = []
                for (x, y, w, h), colour in zip(boxes, targets):
                    crop = image.crop((round(x * scale + offset), round(y * scale),
                                       round((x+w) * scale + offset), round((y+h) * scale)))
                    counts.append(sum(pixel == colour for pixel in crop.getdata()))
                observations.append({"frame": frame, "screen": screen, "pixels": counts})
    return observations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "game", "save", "state", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--renderer", choices=("metal", "vulkan"), required=True)
    parser.add_argument("--package", choices=("hud-demo", "button-icons"), required=True)
    parser.add_argument("--fps", type=int, choices=(30, 60), default=30)
    parser.add_argument("--layout", choices=("labels", "position", "custom"), default="labels")
    parser.add_argument("--wide", action="store_true", help="21:9 TV at 2560x1080")
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--timeout", type=float, default=300)
    parser.add_argument("--keep-frames", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.frames <= 300:
        parser.error("frames must be between 1 and 300")
    repo = Path(__file__).resolve().parents[2]
    args.binary, args.game, args.save, args.state, args.out = (
        p.resolve() for p in (args.binary, args.game, args.save, args.state, args.out))
    if other_games():
        parser.error("another game or benchmark is running; retry in a quiet window")
    disk_ok(args.out.parent)
    if args.out.exists():
        parser.error("output directory must be new (private saves and caches are never reused)")
    source = repo / "examples" / "guest-mods" / args.package
    if not (source / "mod.elf").is_file():
        parser.error("build the example's PowerPC mod.elf first")
    args.out.mkdir()
    root = args.out
    shutil.copytree(args.save, root / "save")
    for item in (root / "save").rglob("*"):
        item.chmod(0o755 if item.is_dir() else 0o644)
    (root / "states").mkdir()
    shutil.copy2(args.state, root / "states" / "slot1.bin")
    package_root = root / "manager" / "Mods" / args.package
    shutil.copytree(source, package_root, ignore=shutil.ignore_patterns("*.o"))
    (root / "manager" / "profiles.json").write_text(json.dumps({
        "active": "Default", "format_version": 1,
        "guest_regions": {args.package: {"base": 2130706432, "size": 327680}},
        "profiles": {"Default": {"enabled": {args.package: True}}}}))
    (root / "guest-sdk.json").write_text(json.dumps({
        "format_version": 1, "python": ["python3"], "compiler": ["clang"],
        "builder": str(repo / "tools/guestmod/build_guest_mod.py"),
        "include": str(repo / "runtime/include")}))
    layouts = {"labels": dict(A="A", B="B", X="X", Y="Y"),
               "position": dict(A="B", B="A", X="Y", Y="X"),
               "custom": dict(A="X", B="B", X="A", Y="Y")}
    (root / "controls.json").write_text(json.dumps({"version": 1, "controller": layouts[args.layout]}))
    first = 700
    env = {k: v for k, v in os.environ.items() if not k.startswith("WWHD_")}
    env.update({"WWHD_CODE_MODS": "1", "WWHD_NO_AUDIO": "1", "WWHD_NO_HOST_INPUT": "1",
                "WWHD_HIDDEN_WINDOWS": "1", "WWHD_UNCAPPED": "1",
                "WWHD_RENDERER_RUNTIME": args.renderer, "WWHD_DRC_MODE": "window",
                "WWHD_MOD_MANAGER_DIR": str(root / "manager"),
                "WWHD_TEST_TRUST_NATIVE_MODS": args.package,
                "WWHD_GUEST_BUILD_CONFIG": str(root / "guest-sdk.json"),
                "WWHD_SHADER_CACHE": str(root / "shaders.bin"),
                "WWHD_VK_SHADER_CACHE": str(root / "vkshaders"),
                "WWHD_VK_PIPELINE_CACHE": str(root / "vkpipelines.bin"),
                "WWHD_SETTINGS": str(root / "settings.ini"),
                "WWHD_DISPLAY_SETTINGS": str(root / "display.plist"),
                "WWHD_CONTROLS": str(root / "controls.json"),
                "XDG_CONFIG_HOME": str(root / "config"),
                "WWHD_STATE_DIR": str(root / "states"), "WWHD_STATE_LOAD_AT": "450:1",
                "WWHD_TEST_ORIGIN": "650", "WWHD_TEST_END": "30",
                "WWHD_PRESS": ",".join("%d-%d:8000" % (f, f+8) for f in range(150, 450, 30)),
                "WWHD_DUMP_FRAMES": ",".join(map(str, range(first, first + args.frames))),
                "WWHD_DUMP_PRESENT": "1", "WWHD_SIM_SCREEN": "2560x1080" if args.wide else "1280x720"})
    if args.wide:
        env["WWHD_ASPECT"] = "21:9"
    if args.fps == 60:
        env["WWHD_INTERP_AT_STEP"] = "510"
    # Recheck immediately before launching; there is no cross-worker game lock.
    if other_games():
        raise RuntimeError("another game started while preparing the case")
    with (root / "runtime.log").open("w") as log:
        process = subprocess.Popen([str(args.binary), "--game", str(args.game),
                                    "--save", str(root / "save")], cwd=root, env=env,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + args.timeout
            while process.poll() is None and not (root / "test_done").exists():
                disk_ok(root)
                if other_games(process.pid):
                    raise RuntimeError("another game or benchmark started; case interrupted")
                if time.monotonic() > deadline:
                    raise RuntimeError("game case timed out")
                time.sleep(1)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    if not (root / "test_done").exists():
        raise RuntimeError("game exited before test_done; inspect private runtime.log")
    observations = inspect_frames(root, first, args.frames, args.package)
    expected = args.package == "hud-demo" or args.layout == "labels"
    passed = all(all(n >= 20 for n in row["pixels"]) if expected
                 else all(n < 20 for n in row["pixels"]) for row in observations)
    report = {"package": args.package, "renderer": args.renderer, "fps": args.fps,
              "layout": args.layout, "wide": args.wide, "frames": args.frames,
              "colour_presence_pass": passed, "observations": observations,
              "limitations": "Colour presence does not prove fades, contextual visibility or live preset changes."}
    (root / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    if not args.keep_frames:
        for path in root.glob("frame_*.png"):
            path.unlink()
    # Full states are large; preserve evidence, not the disposable input copies.
    shutil.rmtree(root / "states")
    shutil.rmtree(root / "save")
    print(json.dumps({k: v for k, v in report.items() if k != "observations"}))
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
