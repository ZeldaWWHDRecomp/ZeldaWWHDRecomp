#!/usr/bin/env python3
"""Maintainers (macOS, both renderers): distill a code-free shader template to one state per translated shader,
and check that the distilled one prepares everything the full one prepares.

usage: distill_template.py TEMPLATE OUT --game GAME_DIR --wwhd BUILT_WWHD [--work DIR] [--keep]

1. shaderprep.py build: the full head start from the game files, with the template state of each record;
2. wwhd --warm-shaders with Metal and with Vulkan (WWHD_VK_METAL_RECIPES=1), WWHD_HEADSTART_MAP naming the shader
   each record translates to and the Vulkan pipeline each Metal recipe gives;
3. shaderprep.py distill: one state per distinct shader per renderer and stage, plus the states of one Metal
   recipe per distinct derived Vulkan pipeline; recipes renamed to the kept states' keys (passes the release guard);
4. coverage check: the same two warms from the distilled template. Metal must give the same number of distinct
   shaders and pipelines, Vulkan every recipe the full template gave. OUT is only written when it passes.

The work folder holds game-derived head starts and caches: it stays on this computer and is removed at the end
unless --keep. Distill again after any change of the shader translation (the keys are the translator's).
Idea after SwitchWakerHD's shader manifest (thanks @Pulpparty / @depende3000).
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)
import shaderprep  # noqa: E402
import shaderprep_format  # noqa: E402


def warm(wwhd, game, headstart, work, name, renderer, extra=None):
    """--warm-shaders as setup runs it, on a fresh live folder -> (map path, live folder, log text)."""
    live = os.path.join(work, "live-" + name)
    shutil.rmtree(live, ignore_errors=True)
    os.makedirs(live)
    mapping = os.path.join(work, "map-%s.txt" % name)
    env = dict(os.environ, WWHD_GAME=game, WWHD_HEADSTART=headstart, WWHD_SHADER_CACHE=os.path.join(live, "shaders.bin"),
               WWHD_VK_SHADER_CACHE=os.path.join(live, "vulkan-shaders"), WWHD_VK_PIPELINE_CACHE_DIR=live,
               WWHD_RENDERER_RUNTIME=renderer, WWHD_NO_AUDIO="1", WWHD_HIDDEN_WINDOWS="1", WWHD_NO_GAMEPAD="1",
               WWHD_NO_HOST_INPUT="1", WWHD_HEADSTART_MAP=mapping, **(extra or {}))
    env.pop("WWHD_VK_PIPELINE_CACHE", None)
    run = subprocess.run([wwhd, "--game", game, "--warm-shaders"], env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, errors="replace")
    log = "".join(l + "\n" for l in run.stdout.splitlines() if not l.startswith("WWHD-PROGRESS"))
    with open(os.path.join(work, "warm-%s.log" % name), "w") as f:
        f.write(log)
    if run.returncode:
        raise RuntimeError("%s warm failed (exit %d), see %s" % (name, run.returncode, f.name))
    return mapping, live, log


def metal_counts(log):
    m = re.search(r"\[warm\] distinct: (\d+) shaders, (\d+) pipelines", log)
    if not m:
        raise RuntimeError("the Metal warm printed no distinct counts (a build without WWHD_HEADSTART_MAP?)")
    return int(m.group(1)), int(m.group(2))


def vulkan_recipes(live):
    out = set()
    for path in glob.glob(os.path.join(live, "*.recipes")):
        with open(path, "rb") as f:
            for p in shaderprep_format.vulkan_database(f.read()):
                out.add(json.dumps(p, sort_keys=True))
    return out


def main():
    a = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    a.add_argument("template")
    a.add_argument("out")
    a.add_argument("--game", required=True, help="the game folder (with content/)")
    a.add_argument("--wwhd", required=True, help="a wwhd built from this checkout")
    a.add_argument("--work", help="work folder (default: a temporary one)")
    a.add_argument("--keep", action="store_true", help="keep the work folder")
    o = a.parse_args()
    game = os.path.abspath(o.game)
    work = os.path.abspath(o.work) if o.work else tempfile.mkdtemp(prefix="wwhd-distill-")
    os.makedirs(work, exist_ok=True)
    content = os.path.join(game, "content")
    try:
        def build(template, name):
            hs = os.path.join(work, name + ".headstart.bin")
            sm = os.path.join(work, name + ".state-map.txt")
            shaderprep.cmd_build(content, hs, template, 0, True, jobs=shaderprep.default_jobs(), state_map=sm)
            return hs, sm
        print("== full template")
        hs, sm = build(o.template, "full")
        m_map, _, m_log = warm(o.wwhd, game, hs, work, "full-metal", "metal")
        v_map, v_live, _ = warm(o.wwhd, game, hs, work, "full-vulkan", "vulkan", {"WWHD_VK_METAL_RECIPES": "1"})
        print("== distill")
        candidate = os.path.join(work, "distilled.bin")
        if shaderprep.cmd_distill(candidate, o.template, sm, [m_map, v_map]):
            return 1
        print("== coverage check")
        dhs, _ = build(candidate, "distilled")
        _, _, dm_log = warm(o.wwhd, game, dhs, work, "distilled-metal", "metal")
        _, dv_live, _ = warm(o.wwhd, game, dhs, work, "distilled-vulkan", "vulkan", {"WWHD_VK_METAL_RECIPES": "1"})
        full_m, dist_m = metal_counts(m_log), metal_counts(dm_log)
        full_v, dist_v = vulkan_recipes(v_live), vulkan_recipes(dv_live)
        print("Metal: %d shaders, %d pipelines (full) / %d, %d (distilled)" % (full_m + dist_m))
        print("Vulkan recipes: %d (full) / %d (distilled), %d missing, %d extra" %
              (len(full_v), len(dist_v), len(full_v - dist_v), len(dist_v - full_v)))
        if full_m != dist_m or full_v - dist_v:
            print("Coverage check failed: %s not written" % o.out)
            return 1
        shutil.copyfile(candidate, o.out)
        print("written: %s (%d bytes)" % (o.out, os.path.getsize(o.out)))
        return 0
    finally:
        if not o.keep:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
