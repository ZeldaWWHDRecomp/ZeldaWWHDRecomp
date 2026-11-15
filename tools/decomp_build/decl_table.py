#!/usr/bin/env python3
"""Decomp build, fast mode: the decompilation's declaration table (decl_lint.py --handoff) made at build time from this
build's own translation, so native calls between decompiled functions are register-exact without a table in the repo.

usage: decl_table.py DECOMP_DIR GEN_DIR IMAGE RPX OUT_DIR [--jobs N] [--cache DIR | --no-cache]

Runs the decompilation's tools/verify/decl_lint.py in its port mode (--gen GEN_DIR, the decomp build's translation;
--image the gen.py image; --rpx the player's cking.rpx; this repository's runtime/ and tools/) with every output in
OUT_DIR, and writes OUT_DIR/handoff.tsv. Prints the table's path. Exit 1 when the decompilation has no port mode or the
run made no table (cmake then stops: fast mode needs the table).

Cached in two layers:
- OUT_DIR/inputs.sha256 holds a hash of every input (the decompilation's sources and tools, the translation, the image,
  the game code, this repository's runtime HLE bodies and headers, rpx.py, these scripts), independent of where they
  are: an unchanged hash reuses the table. The shared cache (decl_cache.py) keeps the table under the same hash, so
  another build directory with the same inputs gets it without a run.
- otherwise decl_lint runs with decl_cache.py's per-unit entries (per source: definitions, compiled call sites, ext stub
  users; per generated function: its Cfg; the live-in table and the RPX's disassembly): a change recomputes the units
  it touches and the call-site audit; the result is the full run's, byte for byte.
--cache DIR (default $WWHD_DECOMP_BUILD_CACHE, else the user cache directory: decl_cache.default_root) / --no-cache."""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import decl_cache  # noqa: E402


def tree_files(root, exts=None):
    for dp, ds, fs in os.walk(root):
        ds[:] = sorted(d for d in ds if not decl_cache.hidden(d))
        for f in sorted(x for x in fs if not decl_cache.hidden(x)):
            if exts is None or f.endswith(exts):
                yield os.path.join(dp, f)


# decl_lint's reader of the runtime (its _import_reads: the HLE(lib, name) bodies of runtime/src/**/*.cpp) and the
# only other uses of the runtime directory it has (the include directory); runtime_inputs relies on these
HLE_READER = ('re.finditer(r"HLE\\((\\w+),\\s*(\\w+)\\)\\s*\\{", t)',
              'glob.glob(os.path.join(runtime or os.path.join(ROOT, "runtime"), "src", "**", "*.cpp"), recursive=True)')
RUNTIME_USES = ('os.path.join(PATHS["runtime"], "include")', 'if PATHS["runtime"] != os.path.join(ROOT, "runtime")',
                '_import_reads(PATHS["runtime"])')
HLE = re.compile(r"HLE\((\w+),\s*(\w+)\)\s*\{")


def hle_bodies(text):
    """the HLE(lib, name) { ... } bodies of a runtime source as decl_lint's _import_reads cuts them"""
    out = []
    for m in HLE.finditer(text):
        i, depth = m.end(), 1
        while depth and i < len(text):
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        out.append(m.group(0) + text[m.end():i])
    return out


def runtime_inputs(decomp, root):
    """[(name, path or None, bytes or None)]: the part of ROOT/runtime/src the table depends on. When the
    decompilation's decl_lint.py reads it only through the known HLE reader (HLE_READER, RUNTIME_USES), that is the HLE
    bodies of its .cpp files (a runtime change elsewhere keeps the table); otherwise every .cpp and .h file."""
    src = os.path.join(root, "runtime", "src")
    try:
        with open(os.path.join(decomp, "tools", "verify", "decl_lint.py"), encoding="utf-8", errors="replace") as fh:
            lint = fh.read()
    except OSError:
        lint = ""
    # every use of PATHS["runtime"] is one of RUNTIME_USES (the occurrence lies inside one of them)
    known = all(r in lint for r in HLE_READER) and all(
        any(lint.find(u, max(0, m.start() - len(u)), m.end() + len(u)) >= 0 for u in RUNTIME_USES)
        for m in re.finditer(r'PATHS\["runtime"\]', lint))
    out = []
    for p in tree_files(src, (".cpp",) if known else (".cpp", ".h")):
        name = "port/" + os.path.relpath(p, root).replace(os.sep, "/")
        if not known:
            out.append((name, p, None))
            continue
        with open(p, encoding="utf-8", errors="replace") as fh:
            bodies = hle_bodies(fh.read())
        if bodies:
            out.append((name + "#hle", None, "\0".join(bodies).encode("utf-8", "surrogateescape")))
    return out


def inputs_hash(decomp, gen, image, rpx, root=None):
    """every input of the table, each named relative to its place (decomp/..., gen/..., port/..., image, rpx): the same
    inputs in another build directory or checkout give the same hash. Of the runtime's sources, only what decl_lint
    reads (runtime_inputs)."""
    root = root or ROOT
    h = hashlib.sha256(b"decl_table-4\n")
    v = os.path.join(decomp, "tools", "verify")
    named = [("decomp/tools/verify/" + f, os.path.join(v, f)) for f in sorted(os.listdir(v))
             if os.path.isfile(os.path.join(v, f)) and not decl_cache.hidden(f)]
    for base, label, sub, exts in ((decomp, "decomp", "wwhd_src", None),
                                   (decomp, "decomp", os.path.join("tools", "verify", "include"), None),
                                   (decomp, "decomp", os.path.join("tools", "verify", "src"), None),
                                   (decomp, "decomp", os.path.join("tools", "verify", "units"), None),
                                   (root, "port", os.path.join("runtime", "include"), None)):
        named += [(label + "/" + os.path.relpath(p, base), p) for p in tree_files(os.path.join(base, sub), exts)]
    named += [("port/tools/rpx.py", os.path.join(root, "tools", "rpx.py"))]
    named += [("port/tools/decomp_build/" + f, os.path.join(HERE, f))
              for f in ("decl_table.py", "decl_lint_run.py", "decl_cache.py")]
    named += [("image", image), ("rpx", rpx)]
    named += [("gen/" + f, os.path.join(gen, f)) for f in sorted(os.listdir(gen)) if f.startswith("code_")]
    for name, p, data in [(n, p, None) for n, p in named] + runtime_inputs(decomp, root):
        if data is not None:
            h.update(name.encode() + b"\0" + data + b"\n")
        elif os.path.isfile(p):
            h.update(name.encode() + b"\0")
            with open(p, "rb") as fh:
                for chunk in iter(lambda: fh.read(1 << 20), b""):
                    h.update(chunk)
            h.update(b"\n")
    return h.hexdigest()


def remove_if_exists(path):
    """this script's own earlier outputs (stale ones must not survive into a new run)"""
    if os.path.exists(path):
        os.remove(path)


def unit_keys(decomp, gen, rpx):
    """the keys of the live-in table and the RPX's disassembly, and the file name decl_lint gives the latter"""
    tools = decl_cache.tool_hash(decomp)
    try:
        import capstone
        cs = getattr(capstone, "__version__", "?")
    except ImportError:
        cs = "-"
    livein = hashlib.sha256(("livein\n%s\n%s" % (tools, decl_cache.gen_hash(gen))).encode()).hexdigest()
    targets = hashlib.sha256(("targets\n%s\n%s\n" % (tools, cs)).encode() + decl_cache.file_digest(rpx)).hexdigest()
    st = os.stat(rpx)
    return livein, targets, "targets3-%d-%d.pkl" % (st.st_size, st.st_mtime_ns)


def main():
    ap = argparse.ArgumentParser()
    for k in ("decomp", "gen", "image", "rpx", "out"):
        ap.add_argument(k)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--cache", help="the shared cache's root (default: $WWHD_DECOMP_BUILD_CACHE or the user cache "
                    "directory; 'off' disables it)")
    ap.add_argument("--no-cache", action="store_true", help="no shared cache (every unit computed: a full run)")
    a = ap.parse_args()
    lint = os.path.join(a.decomp, "tools", "verify", "decl_lint.py")
    if not os.path.isfile(lint) or "--out-dir" not in open(lint, encoding="utf-8").read():
        sys.exit("decl_table.py: %s has no port mode (--out-dir): the decompilation is older than the build-time "
                 "declaration table" % lint)
    os.makedirs(a.out, exist_ok=True)
    table, stamp, logf = (os.path.join(a.out, f) for f in ("handoff.tsv", "inputs.sha256", "decl_lint.log"))
    t0 = time.time()
    key = inputs_hash(a.decomp, a.gen, a.image, a.rpx)
    if os.path.isfile(table) and os.path.isfile(stamp) and open(stamp).read().strip() == key:
        print("decomp: declaration table up to date (%s)" % table)
        return 0
    for f in (stamp, table):
        remove_if_exists(f)
    root = None
    if not a.no_cache:
        root = decl_cache.default_root() if a.cache is None else (
            None if a.cache.lower() in ("off", "0", "no", "none") else os.path.abspath(os.path.expanduser(a.cache)))
    store = decl_cache.Store(root) if root else None
    if store is not None and store.get_file("handoff", key, table + ".tmp"):
        store.get_file("handoff", key + "-log", logf)
        os.replace(table + ".tmp", table)
        with open(stamp, "w") as fh:
            fh.write(key + "\n")
        print("decomp: declaration table from the shared cache in %.0f s (%s; cache %s)" % (time.time() - t0, table, root))
        return 0
    work = os.path.join(a.out, "work")
    os.makedirs(os.path.join(work, "decl_lint"), exist_ok=True)
    # decl_lint makes its live-in table only when the file is missing, and reuses its index of the translation by file
    # times: both are made for this translation (the live-in table from the cache when it holds this translation's)
    livein = os.path.join(work, "livein.tsv")
    for f in (livein, os.path.join(work, "genindex.pkl")):
        remove_if_exists(f)
    if store is not None:
        lk, tk, tname = unit_keys(a.decomp, a.gen, a.rpx)
        store.get_file("livein", lk, livein)
        tpath = os.path.join(work, "decl_lint", tname)
        if not os.path.isfile(tpath):
            store.get_file("targets", tk, tpath)
    # through decl_lint_run.py: autoglue gets the zero-filled sections of RPX, not of the decompilation's own
    # game/code/cking.rpx (a link it does not ship); with the cache, its per-unit entries
    cmd = [sys.executable, os.path.join(HERE, "decl_lint_run.py"), "-j", str(a.jobs), "--gen", a.gen, "--image", a.image, "--rpx", a.rpx,
           "--runtime", os.path.join(ROOT, "runtime"), "--tools", os.path.join(ROOT, "tools"), "--out-dir", work,
           "--handoff", table + ".tmp"]
    with open(logf, "w") as log:
        r = subprocess.run(cmd, cwd=a.decomp, stdout=log, stderr=subprocess.STDOUT,
                           env=dict(os.environ, WWHD_DECL_DECOMP=a.decomp, WWHD_DECL_RPX=a.rpx, WWHD_DECL_CACHE=root or ""))
    # exit 1 = some under-declared calls (listed in the log): the table is still complete; native_rules.py keeps the
    # files with such calls on guest calls
    if r.returncode not in (0, 1) or not os.path.isfile(table + ".tmp"):
        sys.exit("decl_table.py: decl_lint.py failed (exit %d), see %s" % (r.returncode, logf))
    os.replace(table + ".tmp", table)
    with open(stamp, "w") as fh:
        fh.write(key + "\n")
    cached = ""
    if store is not None:
        store.put_file("handoff", key, table)
        store.put_file("handoff", key + "-log", logf)
        store.put_file("livein", lk, livein)
        if os.path.isfile(tpath):
            store.put_file("targets", tk, tpath)
        store.prune()
        cached = "; cache %s" % root
    print("decomp: declaration table made from this build's translation in %.0f s (%s%s)" % (time.time() - t0, table, cached))
    return 0


if __name__ == "__main__":
    sys.exit(main())
