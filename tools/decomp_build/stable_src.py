#!/usr/bin/env python3
"""Stable source paths for a build directory shared by several worktrees (android/build_native.sh STABLE_SRC=1).

usage: stable_src.py [--adopt] ROOT NAME=TREE [NAME=TREE ...]

Points the links ROOT/NAME at the trees (e.g. ROOT/src -> a port worktree, ROOT/decomp-src -> its decompilation).
A build configured through the links (cmake -S ROOT/src, WWHD_DECOMP_DIR=ROOT/decomp-src) has the same paths in
every command, generated file and stamp whichever worktree it builds, so:
- ninja does not rebuild for a new path (no command, depfile or log rewrite: no rehome step),
- ccache (CCACHE_BASEDIR at or above ROOT) hits for every file another worktree compiled the same way,
- gen.py's and native_rules.py's input stamps and the declaration table's stamp stay valid when the content is the
  same (they hash their paths too).

ninja decides by file times, not content, so switching trees fixes the times first. ROOT/stable_src.json records,
for every file of each tree as the last run left it (git ls-files -co --exclude-standard: tracked and untracked,
not ignored), its content hash and time. For the new tree, a file whose content equals the recorded one gets the
recorded time (to ninja nothing changed), every other file the current time (newer than any output: it and what
depends on it rebuild). Then the record is rewritten for the new tree. Run it before every build (the record must be
what the build saw: an edit made after the run is seen as an edit by the next run and by ninja alike).

--adopt: record the trees as they are and point the links at them, changing no file's time: for a build folder that
was just moved to the links (its paths rewritten from these trees to ROOT/NAME), so the next switch knows what it saw.

Files outside the listing (ignored files, e.g. build outputs inside a tree) keep their times: a build must not read
ignored files of the tree. The record is ROOT's own; the trees' files get only their times changed.
"""
import hashlib
import json
import os
import subprocess
import sys
import time

RECORD = "stable_src.json"


def listing(tree):
    """the files of TREE as git lists them (tracked and untracked, not ignored); every file below TREE otherwise"""
    r = subprocess.run(["git", "-C", tree, "ls-files", "-co", "--exclude-standard", "-z"], capture_output=True)
    if r.returncode == 0:
        names = [n for n in r.stdout.decode("utf-8", "surrogateescape").split("\0") if n]
    else:
        names = []
        for dp, ds, fs in os.walk(tree):
            ds[:] = [d for d in ds if d != ".git"]
            names += [os.path.relpath(os.path.join(dp, f), tree) for f in fs]
    return sorted(n for n in names if os.path.isfile(os.path.join(tree, n)) and not os.path.islink(os.path.join(tree, n)))


def digest(path):
    h = hashlib.sha1()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def switch(root, links, now=None, adopt=False):
    """links: {name: tree}. Returns {name: (kept, touched)}: files that got their recorded time, files that got NOW.
    adopt: record every file with its own time instead (no time is changed; all count as kept)."""
    now = time.time_ns() if now is None else now
    rec_path = os.path.join(root, RECORD)
    try:
        with open(rec_path) as fh:
            rec = json.load(fh)
    except (OSError, ValueError):
        rec = {}
    out, stats = {}, {}
    for name, tree in sorted(links.items()):
        tree = os.path.realpath(tree)
        if not os.path.isdir(tree):
            sys.exit("stable_src.py: no directory %s" % tree)
        old = rec.get(name, {})
        same_tree = old.get("tree") == tree
        files_old = old.get("files", {})
        files, kept, touched = {}, 0, 0
        for rel in listing(tree):
            p = os.path.join(tree, rel)
            st = os.stat(p)
            o = files_old.get(rel)
            # the same file as recorded (same tree, inode, size and time): its content is the recorded one
            if o and same_tree and o[2] == st.st_ino and o[3] == st.st_size and o[1] == st.st_mtime_ns:
                files[rel] = o
                kept += 1
                continue
            h = digest(p)
            if adopt:
                files[rel] = [h, st.st_mtime_ns, st.st_ino, st.st_size]
                kept += 1
                continue
            if o and o[0] == h:
                if st.st_mtime_ns != o[1]:
                    os.utime(p, ns=(st.st_atime_ns, o[1]))
                mt = o[1]
                kept += 1
            else:
                os.utime(p, ns=(st.st_atime_ns, now))
                mt = now
                touched += 1
            files[rel] = [h, mt, st.st_ino, st.st_size]
        link = os.path.join(root, name)
        if not (os.path.islink(link) and os.path.realpath(link) == tree):
            if os.path.exists(link) and not os.path.islink(link):
                sys.exit("stable_src.py: %s exists and is not a link" % link)
            tmp = link + ".tmp"
            if os.path.islink(tmp):
                os.remove(tmp)  # (this script's own unfinished link)
            os.symlink(tree, tmp)
            os.replace(tmp, link)
        out[name] = {"tree": tree, "files": files}
        stats[name] = (kept, touched)
    for name in rec:  # links not named this time keep their record
        out.setdefault(name, rec[name])
    tmp = rec_path + ".tmp"
    with open(tmp, "w") as fh:
        json.dump(out, fh, separators=(",", ":"))
    os.replace(tmp, rec_path)
    return stats


def main():
    args = sys.argv[1:]
    adopt = args[:1] == ["--adopt"]
    args = args[1:] if adopt else args
    if len(args) < 2 or any("=" not in a for a in args[1:]):
        sys.exit(__doc__.strip().splitlines()[2])
    root = os.path.abspath(args[0])
    os.makedirs(root, exist_ok=True)
    links = dict(a.split("=", 1) for a in args[1:])
    t0 = time.time()
    for name, (kept, touched) in sorted(switch(root, links, adopt=adopt).items()):
        print("stable_src: %s -> %s: %d files as before, %d new or changed" % (
            os.path.join(root, name), os.path.realpath(links[name]), kept, touched))
    print("stable_src: %.1f s" % (time.time() - t0))


if __name__ == "__main__":
    main()
