# SPDX-License-Identifier: MPL-2.0
"""Updating from an earlier release folder (portable releases keep everything in <release>/data).

A new release unpacked next to the old one finds it (earlier_releases) and, when the player chooses to,
builds the new game from the old folder's game files and caches, then brings the old data folder over
(transfer): game files, saves, settings, controls, shader caches, mods, randomizer seeds, the compiler
and the build caches. Where the file system has copy-on-write clones (APFS, Btrfs, XFS) the data is
cloned: no extra space, and the old release keeps working. Elsewhere it is moved, and the old release
does not start its game any more (the setup window says so before the player confirms); the old game
binary is kept as data/bin.previous.

The transfer is journaled (update-from.json in the new data folder): each top-level entry is cloned to a
temporary name and renamed into place, or moved with one rename, so an interrupted transfer is resumed
where it stopped by the next setup start. Nothing in the old folder is deleted: a move takes entries away,
a clone leaves them, and the old release folder itself stays where it is.
"""
import json
import os
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # (the Windows release's isolated Python)
import build_cache  # noqa: E402

JOURNAL = "update-from.json"
NOTE = "DATA-MOVED.txt"
# what is not brought over: the old release's own build outputs and working folders, its logs and locks;
# the new release builds its own
SKIP = {"bin", "bin.staging", "bin.previous", "work", "decomp-work", "code-builds", "code-build.lock",
        "code-mods-active.json", "code-mods-status.json", "setup.log", "setup-window.log", JOURNAL,
        "install.json", "shader-prebuild-cancel"}


def _read_json(path):
    try:
        with open(path, encoding="utf-8") as f:
            value = json.load(f)
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError):
        return {}


def release_info(folder, platform):
    """{root, data, version, installed, state} of an earlier release folder with a prepared game, or None."""
    root = Path(folder)
    data = root / "data"
    state = _read_json(data / "install.json")
    if not state or state.get("platform") != platform or state.get("placeholder_code"):
        return None
    exe = state.get("exe") or ""
    exe = exe if os.path.isabs(exe) else str(data / exe)
    game = state.get("game_dir") or "game"
    game = game if os.path.isabs(game) else str(data / game)
    if not os.path.isfile(os.path.join(game, "code", "cking.rpx")):
        return None
    return {"root": str(root), "data": str(data), "version": state.get("version", "?"),
            "installed": state.get("installed", ""), "state": state, "exe": exe, "game_dir": game,
            "game_inside": os.path.abspath(game).startswith(os.path.abspath(str(data)) + os.sep)}


def earlier_releases(pkg, platform):
    """Earlier release folders next to this one (the usual place: the same Downloads or games folder), newest
    first. A folder whose data moved to a newer release has no install.json and is not listed."""
    pkg = os.path.abspath(pkg)
    parent = os.path.dirname(pkg)
    found = []
    try:
        names = os.listdir(parent)
    except OSError:
        return []
    for name in names:
        root = os.path.join(parent, name)
        if os.path.abspath(root) == pkg or not os.path.isdir(root) or not os.path.isfile(os.path.join(root, "portable.txt")):
            continue
        info = release_info(root, platform)
        if info:
            found.append(info)
    return sorted(found, key=lambda i: i["installed"], reverse=True)


def transfer_mode(old_data, new_data):
    return "clone" if build_cache.can_clone(old_data, new_data) else "move"


def plan(old_data, new_data):
    """The entries of the old data folder to bring over; raises ValueError for one the new folder already has."""
    entries = []
    for name in sorted(os.listdir(old_data)):
        if name in SKIP or name.endswith((".partial", ".initial", ".tmp", ".cloning")) or name.startswith(".wwhd-clone-probe"):
            continue
        if os.path.lexists(os.path.join(new_data, name)):
            raise ValueError("%s is already in %s" % (name, new_data))
        entries.append(name)
    return entries


def start(info, new_data, mode, version, log=lambda text: None):
    """Writes the journal (before anything is brought over) and returns it."""
    old_data = info["data"]
    journal = {"schema": 1, "from": info["root"], "from_data": old_data, "from_version": info["version"],
               "to_version": version, "mode": mode, "entries": plan(old_data, new_data), "done": [],
               "started": time.strftime("%Y-%m-%d %H:%M:%S"), "complete": False}
    _write(new_data, journal)
    log("update from %s (%s): %s %d entries" % (info["root"], info["version"], mode, len(journal["entries"])))
    return journal


def _write(new_data, journal):
    path = os.path.join(new_data, JOURNAL)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(journal, f, indent=1)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def pending(new_data):
    """The journal of a transfer that has not finished (an interrupted update), or None."""
    journal = _read_json(os.path.join(new_data, JOURNAL))
    return journal if journal and not journal.get("complete") else None


def transfer(journal, new_data, log=lambda text: None, after_entry=None):
    """Brings the journal's entries over (resumable). after_entry(name): a test hook."""
    old_data = journal["from_data"]
    for name in journal["entries"]:
        if name in journal["done"]:
            continue
        src, dst = os.path.join(old_data, name), os.path.join(new_data, name)
        if journal["mode"] == "clone":
            tmp = dst + ".cloning"
            if os.path.isdir(tmp):
                shutil.rmtree(tmp)  # an interrupted clone of this entry (this transfer made it)
            elif os.path.lexists(tmp):
                os.remove(tmp)
            if not os.path.lexists(dst):
                if not build_cache.clone_tree(src, tmp):
                    raise OSError("cloning %s failed" % src)
                os.replace(tmp, dst)
        else:
            if os.path.lexists(src) and not os.path.lexists(dst):
                os.replace(src, dst)  # one rename: the entry is in one place or the other
            elif not os.path.lexists(dst):
                raise OSError("%s is in neither folder" % name)
        journal["done"].append(name)
        _write(new_data, journal)
        if after_entry:
            after_entry(name)
    if journal["mode"] == "move":
        old_bin = os.path.join(old_data, "bin")
        previous = os.path.join(new_data, "bin.previous")
        if os.path.isdir(old_bin) and not os.path.lexists(previous):
            os.replace(old_bin, previous)  # the old game binary: the one previous build kept
            with open(os.path.join(previous, ".wwhd-previous-build.json"), "w", encoding="utf-8") as f:
                json.dump({"version": journal["from_version"], "from": journal["from"]}, f)
        note = os.path.join(journal["from"], NOTE)
        if not os.path.exists(note):
            try:
                with open(note, "w", encoding="utf-8") as f:
                    f.write("The game files, saves and settings of this Wind Waker HD release were moved to the newer "
                            "release in\n%s\nwhen it was updated (%s). This folder does not start the game any more; "
                            "you can delete it.\n" % (os.path.dirname(new_data), time.strftime("%Y-%m-%d")))
            except OSError:
                pass
    journal["complete"] = True
    journal["finished"] = time.strftime("%Y-%m-%d %H:%M:%S")
    _write(new_data, journal)
    log("update transfer complete (%d entries, %s)" % (len(journal["entries"]), journal["mode"]))
    return journal
