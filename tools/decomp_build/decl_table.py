#!/usr/bin/env python3
"""Decomp build, fast mode: the decompilation's declaration table (decl_lint.py --handoff) made at build time from this
build's own translation, so native calls between decompiled functions are register-exact without a table in the repo.

usage: decl_table.py DECOMP_DIR GEN_DIR IMAGE RPX OUT_DIR [--jobs N]

Runs the decompilation's tools/verify/decl_lint.py in its port mode (--gen GEN_DIR, the decomp build's translation;
--image the gen.py image; --rpx the player's cking.rpx; this repository's runtime/ and tools/) with every output in
OUT_DIR, and writes OUT_DIR/handoff.tsv. Cached: OUT_DIR/inputs.sha256 holds a hash of every input (the decompilation's
sources and tools, the translation, the image, the game code, this repository's runtime HLE bodies and headers, rpx.py);
an unchanged hash reuses the table. Prints the table's path. Exit 1 when the decompilation has no port mode or the run
made no table (cmake then stops: fast mode needs the table)."""
import argparse
import hashlib
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))


def tree_files(root, exts=None):
    for dp, ds, fs in os.walk(root):
        ds.sort()
        for f in sorted(fs):
            if exts is None or f.endswith(exts):
                yield os.path.join(dp, f)


def inputs_hash(decomp, gen, image, rpx):
    h = hashlib.sha256(b"decl_table-1\n")
    groups = [
        (os.path.join(decomp, "wwhd_src"), None),
        (os.path.join(decomp, "tools", "verify", "include"), None),
        (os.path.join(decomp, "tools", "verify", "src"), None),
        (os.path.join(decomp, "tools", "verify", "units"), None),
        (os.path.join(ROOT, "runtime", "src"), (".cpp", ".h")),
        (os.path.join(ROOT, "runtime", "include"), None),
    ]
    files = [os.path.join(decomp, "tools", "verify", f) for f in
             ("decl_lint.py", "funcdb.py", "autoglue.py", "specfile.py", "decl_exceptions.tsv")]
    files += [os.path.join(ROOT, "tools", "rpx.py"), image, rpx]
    files += [os.path.join(gen, f) for f in sorted(os.listdir(gen)) if f.startswith("code_")]
    for root, exts in groups:
        files += list(tree_files(root, exts))
    for p in files:
        if not os.path.isfile(p):
            continue
        h.update(os.path.relpath(p, "/").encode() + b"\n")
        with open(p, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    for k in ("decomp", "gen", "image", "rpx", "out"):
        ap.add_argument(k)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    a = ap.parse_args()
    lint = os.path.join(a.decomp, "tools", "verify", "decl_lint.py")
    if not os.path.isfile(lint) or "--out-dir" not in open(lint, encoding="utf-8").read():
        sys.exit("decl_table.py: %s has no port mode (--out-dir): the decompilation is older than the build-time "
                 "declaration table" % lint)
    os.makedirs(a.out, exist_ok=True)
    table, stamp = os.path.join(a.out, "handoff.tsv"), os.path.join(a.out, "inputs.sha256")
    t0 = time.time()
    key = inputs_hash(a.decomp, a.gen, a.image, a.rpx)
    if os.path.isfile(table) and os.path.isfile(stamp) and open(stamp).read().strip() == key:
        print("decomp: declaration table up to date (%s)" % table)
        return 0
    for f in (stamp, table):  # this run's own outputs: a stale table must not survive a failed run
        if os.path.exists(f):
            os.remove(f)
    work = os.path.join(a.out, "work")
    cmd = [sys.executable, lint, "-j", str(a.jobs), "--gen", a.gen, "--image", a.image, "--rpx", a.rpx,
           "--runtime", os.path.join(ROOT, "runtime"), "--tools", os.path.join(ROOT, "tools"), "--out-dir", work,
           "--handoff", table + ".tmp"]
    with open(os.path.join(a.out, "decl_lint.log"), "w") as log:
        r = subprocess.run(cmd, cwd=a.decomp, stdout=log, stderr=subprocess.STDOUT)
    # exit 1 = some under-declared calls (listed in the log): the table is still complete; native_rules.py keeps the
    # files with such calls on guest calls
    if r.returncode not in (0, 1) or not os.path.isfile(table + ".tmp"):
        sys.exit("decl_table.py: decl_lint.py failed (exit %d), see %s" % (r.returncode, os.path.join(a.out, "decl_lint.log")))
    os.replace(table + ".tmp", table)
    with open(stamp, "w") as fh:
        fh.write(key + "\n")
    print("decomp: declaration table made from this build's translation in %.0f s (%s)" % (time.time() - t0, table))
    return 0


if __name__ == "__main__":
    sys.exit(main())
