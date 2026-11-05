# SPDX-License-Identifier: MPL-2.0
"""Randomized games made in setup (the in-app randomizer, tools/rando's wwhd-rando).

Each seed keeps only the files the randomizer changed, beside the installed game, and its own build of the
game code (a code_mods variant: switching between the game as it is and the seeds made is a selection the
game reads at its next start, no build when the variant is cached):

  <data>/randomizer/settings/       the randomizer's settings for the next seed (config.yaml, preferences.yaml)
  <data>/randomizer/seeds.json      the seeds made: id, seed hash words, permalink, RPX SHA-256, ...
  <data>/randomizer/seeds/<id>/     one seed: its settings, logs (spoiler log), last.json, out/ (changed files)
  <data>/randomizer/places.tsv      the spoiler map's places, from the player's own stage files (places.py)

A seed made with the official TWWHD Randomizer (import_seed: its output folder) is kept the same way, marked
"imported" (it has no permalink, seed name or spoiler log of its own).

<id> is the first 16 hex digits of the randomized RPX's SHA-256: the game finds its seed's files by its own
build identity (runtime/src/randomizer_identity.h overlay_directory). Saves, backups and save states are
separate per seed already (the full SHA-256, randomizer_paths.h).
"""
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

REGISTRY_FORMAT = 1
PRESETS = ("beginner", "standard", "everything")
OUTPUT_ROOM = 1 << 30  # a seed's changed files (about 600 MB) and its logs
LANGUAGE_NOTE = ("Randomized games use the installed game's own languages: the randomizer changes those texts, so "
                 "a language source from another region is not used while a randomized game is played.")
IDENTITY_FILES = ("code/cking.rpx", "code/app.xml", "meta/meta.xml")  # a seed's build identity: always kept


def rando_dir(data_dir):
    return Path(data_dir) / "randomizer"


def tool(setup):
    """(command, data folder) of the shipped wwhd-rando; WWHD_RANDO_EXE / WWHD_RANDO_DATA for a build of it"""
    exe = os.environ.get("WWHD_RANDO_EXE") or os.path.join(setup.PKG, "tools", "bin", "wwhd-rando" + setup.EXE_SUFFIX)
    data = os.environ.get("WWHD_RANDO_DATA") or os.path.join(setup.PKG, "tools", "rando", "data")
    if not os.path.isfile(exe) or not os.path.isdir(data):
        raise setup.SetupError("This release has no randomizer (tools/bin/wwhd-rando and tools/rando/data are missing)")
    return exe, data


# How a command runs: (command) -> (exit code, stdout text, stderr text). Setup on Android sets one that runs the
# APK's executable through its native process bridge (tools/android/setup_adapter.py); else a subprocess.
RUNNER = None
TOOL = None  # (command, data folder) set by that host instead of the release's tools/bin


def call(setup, work, *args, log=True):
    """one wwhd-rando command; its JSON result, SetupError with the randomizer's message when it refuses"""
    exe, data = TOOL or tool(setup)
    cmd = ([sys.executable] if exe.endswith(".py") else []) + [exe, "--data", data, "--work", str(work)] + [str(a) for a in args]
    if log:
        setup.LOG.write("$ " + " ".join(cmd))
    if RUNNER:
        code, out, err = RUNNER(cmd)
    else:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        code, out, err = p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")
    if log and err.strip():
        setup.LOG.write(err[-20000:])
    try:
        res = json.loads([l for l in out.strip().splitlines() if l.startswith("{")][-1])
    except (ValueError, IndexError):
        raise setup.SetupError("the randomizer stopped (exit code %d):\n%s" % (code, "\n".join(err.strip().splitlines()[-15:])))
    if not res.get("ok"):
        raise setup.SetupError(res.get("error") or "the randomizer failed")
    return res


def settings_dir(data_dir):
    d = rando_dir(data_dir) / "settings"
    d.mkdir(parents=True, exist_ok=True)
    return d


def atomic_json(path, value):
    path = Path(path)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(value, indent=1, sort_keys=True), encoding="utf-8")
    os.replace(tmp, path)


def load(data_dir):
    p = rando_dir(data_dir) / "seeds.json"
    try:
        reg = json.loads(p.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {"format": REGISTRY_FORMAT, "seeds": []}
    except (OSError, ValueError) as e:
        raise ValueError("cannot read %s: %s" % (p, e))
    if reg.get("format") != REGISTRY_FORMAT or not isinstance(reg.get("seeds"), list):
        raise ValueError("%s is not a seed list this setup knows" % p)
    return reg


def save(data_dir, reg):
    rando_dir(data_dir).mkdir(parents=True, exist_ok=True)
    atomic_json(rando_dir(data_dir) / "seeds.json", reg)


def seed_dir(data_dir, seed_id):
    if len(seed_id) != 16 or any(c not in "0123456789abcdef" for c in seed_id):
        raise ValueError("not a seed id: %r" % seed_id)
    return rando_dir(data_dir) / "seeds" / seed_id


def find(data_dir, which):
    """a seed by its id (or an unambiguous start of it) or by its number in the list (1, 2, ...)"""
    seeds = load(data_dir)["seeds"]
    if str(which).isdigit() and 0 < int(which) <= len(seeds) and len(str(which)) < 4:
        return seeds[int(which) - 1]
    hits = [s for s in seeds if s["id"].startswith(str(which).lower())]
    return hits[0] if len(hits) == 1 else None


def trim_unchanged(out, game):
    """removes the output files that are the same as the game's (the randomizer writes some unchanged): (count, bytes)"""
    count = size = 0
    for p in sorted(Path(out).rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(out)
        g = Path(game) / rel
        if rel.as_posix() in IDENTITY_FILES:
            continue  # (the build's identity: kept even if one were the same)
        try:
            if g.is_file() and g.stat().st_size == p.stat().st_size and same_file(g, p):
                size += p.stat().st_size
                p.unlink()
                count += 1
        except OSError:
            continue
    return count, size


def same_file(a, b):
    with open(a, "rb") as fa, open(b, "rb") as fb:
        while True:
            x, y = fa.read(1 << 20), fb.read(1 << 20)
            if x != y:
                return False
            if not x:
                return True


def ensure_places(setup, data_dir, game_dir):
    """the spoiler map's places from the player's own stage files, once per install (places.py); a failure only leaves
    the map out"""
    out = rando_dir(data_dir) / "places.tsv"
    if out.is_file():
        return str(out)
    try:
        locations = rando_dir(data_dir) / "locations.json"
        res = call(setup, settings_dir(data_dir), "locations", log=False)
        locations.write_text(json.dumps(res), encoding="utf-8")
        sys.path.insert(0, os.path.join(str(setup.PKG), "tools", "rando"))
        import places  # tools/rando/places.py
        rows = places.build(game_dir, places.load_locations(str(locations)))
        places.write(rows, str(out))
        setup.LOG.write("spoiler map places: %d locations, %d with a spot" % (len(rows), sum(1 for r in rows if r[3])))
        return str(out)
    except Exception as e:  # noqa: BLE001 (the map is optional)
        setup.LOG.write("spoiler map places skipped: %s" % e)
        return None


def check_base(setup, ctx):
    """the installed game a seed is made from: the USA release as it is, the regular (not decomp) release"""
    if ctx.manifest.get("variant") == "decomp":
        raise setup.SetupError("Randomized games need the regular port release, not the decomp version.")
    if not setup.valid_game_folder(ctx.game_dir):
        raise setup.SetupError("No installed game: run setup with your game first, then make a randomized game.")
    if setup.game_folder_title(ctx.game_dir) == setup.RANDOMIZER_TITLE:
        raise setup.SetupError("The installed game is a randomized one (an output folder installed by an earlier setup). It "
                               "still plays, with its saves. To make or import randomized games, install the game as it "
                               "is (Change the game); the randomized game's saves stay, and importing the same output "
                               "folder again plays it with them.")
    build = setup.check_game_version(ctx.game_dir)
    if build.name not in ("USA", "EU"):
        raise setup.SetupError("Randomized games can be made from the USA and European releases (yours is the %s one)." % build.name)
    return build


def configure(setup, data_dir, preset=None, permalink=None, settings_file=None, seed=None):
    """applies the requested settings to the settings for the next seed; returns the permalink"""
    work = settings_dir(data_dir)
    if settings_file:
        src = Path(settings_file)
        shutil.copyfile(src, work / "config.yaml")
        prefs = src.with_name("preferences.yaml")
        if prefs.is_file():
            shutil.copyfile(prefs, work / "preferences.yaml")
    if preset:
        if preset not in PRESETS:
            raise setup.SetupError("no randomizer preset %s (%s)" % (preset, ", ".join(PRESETS)))
        call(setup, work, "preset", preset)
    if permalink:
        call(setup, work, "load-permalink", permalink)
    if seed:
        call(setup, work, "set-seed", seed)
    return call(setup, work, "permalink")["permalink"]


def make(setup, ctx, say=print, preset=None, permalink=None, settings_file=None, seed=None, new_seed=False):
    """a new randomized game from the installed game; returns its registry entry (not built yet)"""
    check_base(setup, ctx)
    data_dir = Path(ctx.data_dir)
    if setup.language_sources(str(data_dir)):
        say("  " + LANGUAGE_NOTE)
    if new_seed:
        call(setup, settings_dir(data_dir), "new-seed")
    link = configure(setup, data_dir, preset, permalink, settings_file, seed)
    for s in load(data_dir)["seeds"]:
        if s.get("permalink") == link and seed_dir(data_dir, s["id"]).is_dir():
            say("  These settings and seed made %s already (%s)." % (s["id"], s.get("hash", "")))
            return s
    if setup.free_space(str(data_dir)) < OUTPUT_ROOM + (2 << 30):
        raise setup.SetupError("not enough free disk space in %s for a randomized game (3 GB needed: its files and its build)" % data_dir)
    root = rando_dir(data_dir) / "seeds"
    root.mkdir(parents=True, exist_ok=True)
    tmp = root / (".new-%d-%d" % (os.getpid(), int(time.time())))
    tmp.mkdir()
    try:
        for f in ("config.yaml", "preferences.yaml"):
            shutil.copyfile(settings_dir(data_dir) / f, tmp / f)
        t0 = time.time()
        say("  Randomizing (TWWHD Randomizer)...")
        res = call(setup, tmp, "generate", "--game", ctx.game_dir, "--out", tmp / "out")
        n, size = trim_unchanged(tmp / "out", ctx.game_dir)
        rpx = tmp / "out" / "code" / "cking.rpx"
        with open(rpx, "rb") as f:
            from hashlib import sha256
            digest = sha256(f.read()).hexdigest()
        seed_id = digest[:16]
        final = seed_dir(data_dir, seed_id)
        # the port's compatibility check of the randomized code (randomizer_compat.py), before anything is built
        setup.check_game_version(str(tmp / "out"))
        if final.exists():
            shutil.rmtree(setup.longpath(str(tmp)))  # (made by this run)
            tmp = None
        else:
            os.replace(tmp, final)
            tmp = None
        spoiler = res.get("spoiler_log") or ""
        entry = {"id": seed_id, "rpx_sha256": digest, "hash": res.get("hash", ""), "permalink": res.get("permalink", link),
                 "seed": res.get("seed", ""), "version": res.get("version", ""), "release": res.get("release", ""),
                 "created": time.strftime("%Y-%m-%d %H:%M:%S"), "spoiler_log": spoiler_relative(final, spoiler)}
        reg = load(data_dir)
        reg["seeds"] = [s for s in reg["seeds"] if s["id"] != seed_id] + [entry]
        save(data_dir, reg)
        say("  Randomized game %s (%s) made in %d s; %d unchanged files left out (%d MB)." % (
            seed_id, entry["hash"], time.time() - t0, n, size >> 20))
        ensure_places(setup, data_dir, ctx.game_dir)
        return entry
    finally:
        if tmp is not None and tmp.exists():
            shutil.rmtree(setup.longpath(str(tmp)), ignore_errors=True)  # (made by this run, failed)


def copy_changed(src, game, out):
    """copies the files of the game folder `src` that differ from the installed game's into `out` (the build's identity
    files always): (copied count, copied bytes, left-out count, left-out bytes)"""
    copied = size = skipped = skipped_size = 0
    for p in sorted(Path(src).rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(src)
        g = Path(game) / rel
        n = p.stat().st_size
        if rel.as_posix() not in IDENTITY_FILES and g.is_file() and g.stat().st_size == n and same_file(g, p):
            skipped, skipped_size = skipped + 1, skipped_size + n
            continue
        dst = Path(out) / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, dst)
        copied, size = copied + 1, size + n
    return copied, size, skipped, skipped_size


IMPORT_FOLDER = ("Choose the output folder of the official TWWHD Randomizer (it contains code, content and meta), or its "
                 "code/cking.rpx.")


def import_seed(setup, ctx, path, say=print):
    """a seed made with the official TWWHD Randomizer (its output folder, or that folder's code/cking.rpx), kept like the
    seeds made here: only its files that differ from the installed game, in seeds/<id>/out. The same compatibility check
    as every randomized game runs on the copy before it is kept (setup.check_game_version); select() then builds it like
    a seed made here. Its saves are its own by its RPX's SHA-256, the same folder a setup that installed this output folder
    as the game used. Returns its registry entry (not built yet)."""
    base = check_base(setup, ctx)
    if base.name != "USA":
        raise setup.SetupError("Seeds from the official TWWHD Randomizer are USA games. Install the USA game as it is to "
                               "play them (yours is the %s one)." % ("European" if base.name == "EU" else base.name))
    folder = setup.selected_game_folder(path) if path else None
    if not folder:
        raise setup.SetupError(IMPORT_FOLDER)
    if setup.game_folder_title(folder) != setup.RANDOMIZER_TITLE:
        raise setup.SetupError("This is not a randomized game (its title is not the randomizer's). " + IMPORT_FOLDER)
    data_dir = Path(ctx.data_dir)
    if os.path.realpath(folder).startswith(os.path.realpath(str(rando_dir(data_dir))) + os.sep):
        raise setup.SetupError("This randomized game is in setup already: choose it under Games.")
    digest = setup.file_sha256(os.path.join(folder, "code", "cking.rpx"))
    seed_id = digest[:16]
    for s in load(data_dir)["seeds"]:
        if s["id"] == seed_id and (seed_dir(data_dir, seed_id) / "out" / "code" / "cking.rpx").is_file():
            say("  This seed was imported already (%s)." % seed_id)
            return s
    if setup.free_space(str(data_dir)) < OUTPUT_ROOM + (2 << 30):
        raise setup.SetupError("not enough free disk space in %s for a randomized game (3 GB needed: its files and its build)" % data_dir)
    root = rando_dir(data_dir) / "seeds"
    root.mkdir(parents=True, exist_ok=True)
    tmp = root / (".import-%d-%d" % (os.getpid(), int(time.time())))
    tmp.mkdir()
    try:
        t0 = time.time()
        say("  Importing the randomized game from %s..." % folder)
        n, size, left, left_size = copy_changed(folder, ctx.game_dir, tmp / "out")
        rpx = tmp / "out" / "code" / "cking.rpx"
        if setup.file_sha256(str(rpx)) != digest:
            raise setup.SetupError("The randomized game changed while it was imported. Import it again once the randomizer is done.")
        # the port's compatibility check of the randomized code (randomizer_compat.py), on the copy that is built
        build = setup.check_game_version(str(tmp / "out"))
        if getattr(build, "randomizer", False) is not True:
            raise setup.SetupError("This is not a randomized game. " + IMPORT_FOLDER)
        final = seed_dir(data_dir, seed_id)
        if final.exists():
            raise setup.SetupError("A folder for this seed is already there but incomplete (%s). Delete the seed in Games "
                                   "first, then import it again." % final)
        os.replace(tmp, final)
        tmp = None
        entry = {"id": seed_id, "rpx_sha256": digest, "hash": "", "permalink": "", "seed": "", "version": "",
                 "release": "USA", "created": time.strftime("%Y-%m-%d %H:%M:%S"), "spoiler_log": "", "imported": True}
        reg = load(data_dir)
        reg["seeds"] = [s for s in reg["seeds"] if s["id"] != seed_id] + [entry]
        save(data_dir, reg)
        say("  Imported randomized game %s in %d s: %d files (%d MB); %d files the same as the installed game left out "
            "(%d MB). The output folder is no longer needed." % (seed_id, time.time() - t0, n, size >> 20, left, left_size >> 20))
        return entry
    finally:
        if tmp is not None and tmp.exists():
            shutil.rmtree(setup.longpath(str(tmp)), ignore_errors=True)  # (made by this run, failed)


def spoiler_relative(final, spoiler):
    """the spoiler log's path relative to the seed folder ("" none)"""
    if not spoiler:
        return ""
    logs = final / "logs"
    name = Path(spoiler).name
    return ("logs/" + name) if (logs / name).is_file() else ""


def select(setup, ctx, which, hooks, status_file=None, cancel_file=None):
    """the game the next start plays: "vanilla" (the game as it is) or a seed; builds its game code when not cached"""
    import code_mods
    import copy
    c = copy.copy(ctx)
    if which != "vanilla":
        s = find(ctx.data_dir, which)
        if not s:
            raise setup.SetupError("no randomized game %s (setup --list-seeds)" % which)
        out = seed_dir(ctx.data_dir, s["id"]) / "out"
        if not (out / "code" / "cking.rpx").is_file():
            raise setup.SetupError("the files of randomized game %s are missing; make it again" % s["id"])
        c.game_dir = str(out)
    # the game as it is was the one selected so far (setup's install keeps its build in code-builds): note its
    # build before the selection changes, so the game's settings can switch back to it without setup
    before = load(ctx.data_dir)
    if before.get("playing", "vanilla") == "vanilla":
        try:
            active = json.loads((Path(ctx.data_dir) / "code-mods-active.json").read_text(encoding="utf-8"))
            if active.get("state") == "ready" and active.get("fingerprint"):
                before.setdefault("vanilla_builds", {})["hooks" if active.get("hooks") else "plain"] = active["fingerprint"]
                save(ctx.data_dir, before)
        except (OSError, ValueError, AttributeError):
            pass
    result = code_mods.rebuild(setup, c, hooks, status_file, cancel_file)
    reg = load(ctx.data_dir)
    reg["playing"] = "vanilla" if which == "vanilla" else s["id"]
    # the build of each game (code_mods' code-builds/<fingerprint>), so the game's settings can switch to a built one
    # without setup (with or without code-mod support: "hooks" / "plain")
    kind = "hooks" if hooks else "plain"
    if which == "vanilla":
        reg.setdefault("vanilla_builds", {})[kind] = result.get("fingerprint", "")
    else:
        for x in reg["seeds"]:
            if x["id"] == s["id"]:
                x.setdefault("builds", {})[kind] = result.get("fingerprint", "")
    save(ctx.data_dir, reg)
    return result


def delete(setup, ctx, which):
    """removes a seed's files and its entry (its saves stay: they are the player's); not the one being played"""
    s = find(ctx.data_dir, which)
    if not s:
        raise setup.SetupError("no randomized game %s" % which)
    reg = load(ctx.data_dir)
    if reg.get("playing") == s["id"]:
        raise setup.SetupError("randomized game %s is the one the game starts; choose another one first" % s["id"])
    d = seed_dir(ctx.data_dir, s["id"])
    if d.exists():
        shutil.rmtree(setup.longpath(str(d)))
    reg["seeds"] = [x for x in reg["seeds"] if x["id"] != s["id"]]
    save(ctx.data_dir, reg)
    return s


def built(data_dir, builds, hooks=None):
    """a game's build is still in code-builds (with code-mod support `hooks`, or either when None)"""
    kinds = ("hooks", "plain") if hooks is None else ("hooks" if hooks else "plain",)
    return any((Path(data_dir) / "code-builds" / builds[k][:16] / "ready.json").is_file()
               for k in kinds if (builds or {}).get(k))


def listing(data_dir, hooks=None):
    reg = load(data_dir)
    out = []
    for i, s in enumerate(reg["seeds"], 1):
        d = seed_dir(data_dir, s["id"])
        out.append(dict(s, number=i, files=(d / "out" / "code" / "cking.rpx").is_file(),
                        built=built(data_dir, s.get("builds"), hooks),
                        spoiler_log=str(d / s["spoiler_log"]) if s.get("spoiler_log") else ""))
    places = rando_dir(data_dir) / "places.tsv"
    return {"seeds": out, "playing": reg.get("playing", "vanilla"), "places": str(places) if places.is_file() else ""}
