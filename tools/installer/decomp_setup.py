"""Setup's optional decomp version (docs/decomp-build.md, "In setup"): a second game binary built on the
player's machine with the decompiled game functions of the public decompilation (ZeldaWWHDDecomp/wwhd)
as compiled source, next to the normal game. Experimental and opt-in: offered on macOS in development
builds only, off by default; the normal game is always built first and stays playable when this fails.

What the release brings (tools/release/package.py --decomp-build), described in sdk/manifest.json
"decomp":
  "revision", "tree_sha256"  the decompilation's commit CI built against and the SHA-256 of its source
                          (SOURCE_PATHS, tree_digest). The source itself is not in the release: setup
                          downloads that commit from github.com/ZeldaWWHDDecomp/wwhd when the option is on
                          ("url", about "download_bytes"), unpacks SOURCE_PATHS into DATA/decomp-src/<12 hex>
                          and checks the unpacked tree against tree_sha256 (independent of how GitHub packs
                          the archive). Kept there: an update with the same pin downloads nothing.
  tools/decomp_build/     gen.py (function list, glue, unit list), glue.cpp, frames_vis.h
  sdk/decomp/obj/         the runtime objects that differ in the decomp build (main, build info, the
                          decomp runtime); everything else is the normal release's sdk/obj and sdk/lib
  sdk/decomp/include/     decomp/decomp.h for the generated hooks.cpp
  "link"                  the decomp game's link line: {gamecode} as usual, {decomp_glue} = hooks.o and
                          the game units
  "unit_flags", "hooks_flags"  how a game unit and hooks.cpp are compiled ({sdk}, {decomp}, {tools}, {out})

What is made here, from the player's own cking.rpx, in DATA/decomp-work (removed afterwards unless
--keep-work): gen.py's output (the game image, the model data headers, the glued sources, hooks.cpp,
the function list), the game code translated with that list, the objects. The result is
bin/wwhd-decomp (portable releases) or decomp/bin/wwhd (with its runtime files) otherwise.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

DECOMP_TEXT = (
    "Builds a second version of the game in which the game functions of the public decompilation "
    "(ZeldaWWHDDecomp/wwhd) run as compiled source instead of translated code. Experimental, for testing "
    "the decompilation; the normal version stays the default and is built first. Adds 10 to 30 minutes "
    "to setup and about 100 MB on disk, and downloads the decompilation's source (about 8 MB) from GitHub.")
DOCS_URL = "https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/blob/devel/docs/decomp-build.md"
ARCHIVE_URL = "https://github.com/ZeldaWWHDDecomp/wwhd/archive/%s.tar.gz"  # % the full commit
# the parts of the decompilation the decomp version is built from (tools/release/package.py hashes the same)
SOURCE_PATHS = ["wwhd_src", "tools/verify/units", "tools/verify/include", "tools/verify/src",
                "tools/verify/autoglue.py", "tools/verify/assets.py", "tools/verify/assets.txt",
                "tools/verify/mkimage.py", "LICENSE", "README.md"]
FETCH_FAILED = "Couldn't download the decomp source \u2014 check your internet connection and try again"
DEV_VERSION = re.compile(r"-devel\.[0-9a-f]+$|^development build$")


def is_dev_version(version):
    """A development build (release.yml devel builds: VERSION-devel.SHA8)."""
    return bool(DEV_VERSION.search(version or ""))


def offered(manifest, is_mac):
    """Setup offers the decomp version only on macOS, in a development build that brings its parts."""
    return bool(is_mac and isinstance(manifest.get("decomp"), dict) and is_dev_version(manifest.get("version")))


OS_FILES = re.compile(r"^(\.DS_Store|\._.*|Thumbs\.db|desktop\.ini)$", re.I)  # what file browsers add


def digest_entries(entries):
    """The tree hash of (relative path, SHA-256 of the file's bytes) pairs, the same on every platform:
    paths with '/' (a Windows '\\' becomes '/'), sorted bytewise (UTF-8), one "SHA-256 PATH\n" line each."""
    lines = sorted(((rel.replace("\\", "/").lstrip("/"), h) for rel, h in entries), key=lambda e: e[0].encode("utf-8"))
    return hashlib.sha256("".join("%s %s\n" % (h, rel) for rel, h in lines).encode("utf-8")).hexdigest()


def tree_digest(root):
    """SHA-256 over the relative path and SHA-256 of every file under root (digest_entries; files a file
    browser adds, such as Finder's .DS_Store, left out). The relative paths are made from the folder names
    os.walk gives below root, never from root itself, so a Windows extended-length root (\\\\?\\...) or
    either separator gives the same hash. tools/release/package.py writes it into the manifest."""
    entries = []
    for dp, dns, fns in os.walk(root):
        dns.sort()
        below = dp[len(root):] if dp.startswith(root) else os.path.relpath(dp, root)
        parts = [x for x in below.replace("\\", "/").split("/") if x and x != "."]
        for fn in fns:
            if OS_FILES.match(fn):
                continue
            h = hashlib.sha256()
            with open(os.path.join(dp, fn), "rb") as f:
                for chunk in iter(lambda: f.read(1 << 20), b""):
                    h.update(chunk)
            entries.append(("/".join(parts + [fn]), h.hexdigest()))
    return digest_entries(entries)


def in_source(rel):
    return any(rel == p or rel.startswith(p + "/") for p in SOURCE_PATHS)


def unpack_source(archive, dest):
    """SOURCE_PATHS of a GitHub archive (one top folder, wwhd-COMMIT/) into dest; regular files only,
    nothing outside dest"""
    with tarfile.open(archive, "r:*") as t:
        for m in t.getmembers():
            parts = m.name.split("/", 1)
            if len(parts) != 2 or not m.isfile() or not in_source(parts[1]):
                continue
            rel = parts[1]
            if rel.startswith("/") or ".." in rel.split("/"):
                raise ValueError("unsafe path in the archive: %s" % m.name)
            out = os.path.join(dest, *rel.split("/"))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with t.extractfile(m) as src, open(out, "wb") as f:
                shutil.copyfileobj(src, f)


def cache_dir(data_dir, rev):
    """DATA/decomp-src/<first 12 hex digits of the commit> (short names, as setup's other caches: issue #144)"""
    return os.path.join(data_dir, "decomp-src", rev[:12])


def fetch_source(s, data_dir, d):
    """The decompilation's source at the pinned commit (manifest "decomp": revision, tree_sha256, url,
    download_bytes) in cache_dir(): reused when it is there and checks out, else downloaded and checked.
    Older pins' copies are removed. Returns its folder; SetupError (FETCH_FAILED) otherwise.
    Setup's own file operations here use s.longpath (Windows extended-length paths; unchanged elsewhere).
    The decomp version is macOS-only: on Windows its deepest files (data\\decomp-src\\<12>\\ plus the
    decompilation's own 70-character paths, and decomp-work\\gen\\glued\\...) would exceed
    setup.DEEPEST_DATA_PATH, which a Windows decomp version has to take into account."""
    lp = getattr(s, "longpath", lambda p: p)
    rev, want = d["revision"], d["tree_sha256"]
    root = cache_dir(data_dir, rev)
    cache = os.path.dirname(root)
    if os.path.isdir(lp(root)) and tree_digest(lp(root)) == want:
        s.say("  Using the decomp source downloaded earlier (%s)." % rev[:7])
        return root
    shutil.rmtree(lp(cache), ignore_errors=True)  # another pin, or a damaged copy
    os.makedirs(lp(cache))
    mb = max(1, int(round(d.get("download_bytes", 0) / 1e6)))
    archive, tmp = os.path.join(cache, "src.tar.gz"), root + ".tmp"
    try:
        s.download(d.get("url") or ARCHIVE_URL % rev, archive, None, d.get("download_bytes", 0),
                   "Downloading the decomp source (about %d MB)" % mb)
        unpack_source(archive, lp(tmp))
        got = tree_digest(lp(tmp))
        if got != want:
            raise ValueError("the downloaded source does not match this release (SHA-256 %s, expected %s)" % (got, want))
        os.replace(lp(tmp), lp(root))
    except (s.SetupError, OSError, ValueError, tarfile.TarError) as e:
        s.LOG.write("decomp source: %s" % e)
        shutil.rmtree(lp(cache), ignore_errors=True)
        detail = getattr(s, "plain_text", str)(str(e)).splitlines()[0][:200]
        raise s.SetupError("%s (%s)" % (FETCH_FAILED, detail))
    finally:
        if os.path.exists(archive):
            os.remove(archive)
    s.say("  Decomp source %s downloaded and checked." % rev[:7])
    return root


def subst(arg, m):
    for k, v in m.items():
        arg = arg.replace("{%s}" % k, v)
    return arg


def fwd(p):
    return p.replace("\\", "/")


def exe_path(data_dir, portable):
    """Where the decomp game goes: next to the normal one in a portable release (it shares bin/'s runtime
    files and portable.txt, so settings, saves and shader caches), else decomp/bin/ in the data folder."""
    if portable:
        return os.path.join(data_dir, "bin", "wwhd-decomp")
    return os.path.join(data_dir, "decomp", "bin", "wwhd")


def build(s, ctx, tc, jobs, step=None):
    """Builds the decomp version; `s` is the setup module (run_logged, Progress, compile_gamecode, link_game,
    say, LOG, SetupError, PKG). Returns the path of the built game."""
    manifest, data_dir = ctx.manifest, ctx.data_dir
    d = manifest["decomp"]
    t_all = time.time()
    rev = d.get("revision", "")
    rpx = os.path.join(ctx.game_dir, "code", "cking.rpx")
    build_id = s.game_builds.identify(rpx)
    if build_id is not None and not build_id.canonical:
        raise s.SetupError("the decomp version works with the USA game only (yours is the %s build)" % build_id.name)
    lp = getattr(s, "longpath", lambda p: p)
    work = os.path.join(data_dir, "decomp-work")
    shutil.rmtree(lp(work), ignore_errors=True)
    os.makedirs(lp(work))
    out = os.path.join(work, "gen")
    times = {}

    # 0. the decompilation's source (downloaded once per pin, checked against the release's SHA-256)
    t0 = time.time()
    src = fetch_source(s, data_dir, d)
    times["source"] = time.time() - t0

    # 1. function list, glue and units: gen.py, reading the game for the model data and the source glue
    t0 = time.time()
    s.say("  Preparing the decompiled source (decomp %s) with your game..." % (rev[:7] or "?"))
    s.run_logged([sys.executable, os.path.join(s.PKG, "tools", "decomp_build", "gen.py"), src,
                  out, "--game", rpx, "--jobs", str(jobs)], what="preparing the decompiled source (gen.py)")
    with open(os.path.join(out, "units.json")) as f:
        units = json.load(f)["units"]
    times["gen"] = time.time() - t0

    # 2. the game code, translated with the decomp build's function list
    t0 = time.time()
    s.say("  Translating the game code for the decomp version...")
    gen_dir = os.path.join(work, "code")
    s.run_logged([sys.executable, os.path.join(s.PKG, "tools", "recomp", "recomp.py"), rpx, gen_dir,
                  "--decomp-hooks", os.path.join(out, "decomp_hooks.txt")],
                 env=dict(os.environ, WWHD_RECOMP_MOD_HOOKS="0"), what="translating the game code (decomp version)")
    times["translate"] = time.time() - t0

    # 3. compile: the game code, the game units, hooks.cpp
    t0 = time.time()
    objs = s.compile_gamecode(tc, manifest, gen_dir, os.path.join(work, "obj"), jobs)
    times["gamecode"] = time.time() - t0
    t0 = time.time()
    m = {"sdk": fwd(os.path.join(s.PKG, "sdk")), "decomp": fwd(src),
         "tools": fwd(os.path.join(s.PKG, "tools")), "out": fwd(out)}
    # debug information as for the game code (setup.debug_info_flags: off unless WWHD_SETUP_DEBUG_INFO)
    unit_flags = s.debug_info_flags([subst(a, m) for a in d["unit_flags"]])
    hooks_flags = s.debug_info_flags([subst(a, m) for a in d["hooks_flags"]])
    jobs_list = []  # (unit index, source, object, extra flags)
    for i, u in enumerate(units):
        udir = os.path.join(work, "units", str(i))
        os.makedirs(udir, exist_ok=True)
        for k, src in enumerate([x["path"] for x in u["sources"]] + [u["glue"]]):
            quote = u["sources"][k]["quote"] if k < len(u["sources"]) else None
            jobs_list.append((i, src, os.path.join(udir, "%d_%s.o" % (k, os.path.basename(src))),
                              ["-iquote", quote] if quote else []))
    total = len(jobs_list)
    pr = s.Progress("compiling the decompiled game")
    done, failures, lock = [0], [], threading.Lock()

    def one(job):
        _, src, obj, extra = job
        cmd = tc.cxx + unit_flags + extra + ["-c", fwd(src), "-o", fwd(obj)]
        p = subprocess.run(cmd, env=tc.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        with lock:
            if p.returncode:
                failures.append((src, p.stdout.decode("utf-8", "replace")))
                s.LOG.write("$ " + " ".join(cmd) + "\n" + p.stdout.decode("utf-8", "replace"))
            done[0] += 1
            pr.update(done[0], total, "%d of %d files" % (done[0], total))

    s.LOG.write("decomp unit flags: " + " ".join(tc.cxx + unit_flags))
    pr.update(0, total, "0 of %d files (%d at a time)" % (total, jobs), force=True)
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        for f in as_completed([ex.submit(one, j) for j in jobs_list]):
            f.result()
    if failures:
        src, text = failures[0]
        raise s.SetupError("compiling %s failed:\n%s" % (os.path.basename(src), "\n".join(text.strip().splitlines()[-20:])))
    pr.done("%d files" % total)
    # each unit becomes one object: hidden symbols turn local (`ld -r`), so units never see each other
    glue = []
    by_unit = {}
    for i, _, obj, _ in jobs_list:
        by_unit.setdefault(i, []).append(obj)
    for i, u in enumerate(units):
        o = os.path.join(work, "units", "%s.o" % u["name"])
        s.run_logged(["xcrun", "ld", "-r", "-o", o] + by_unit[i], env=tc.env, what="merging the game unit %s" % u["name"])
        glue.append(o)
    hooks_o = os.path.join(work, "hooks.o")
    s.run_logged(tc.cxx + hooks_flags + ["-c", os.path.join(out, "hooks.cpp"), "-o", hooks_o], env=tc.env,
                 what="compiling the decomp build's hooks")
    times["units"] = time.time() - t0

    # 4. link: the release's decomp link line with this game code, hooks and units
    t0 = time.time()
    exe = exe_path(data_dir, s.PORTABLE)
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    new = exe + ".new"
    s.link_game(tc, manifest, objs, work, new, recipe=d["link"], glue=[hooks_o] + glue)
    if not s.PORTABLE:
        for rf in manifest.get("runtime_files", []):
            shutil.copyfile(os.path.join(s.PKG, "sdk", "runtime", rf), os.path.join(os.path.dirname(exe), rf))
    os.replace(new, exe)
    times["link"] = time.time() - t0
    times["total"] = time.time() - t_all
    s.say("  Built the decomp version: %s (%d s: %s)" % (exe, times["total"], ", ".join(
        "%s %d s" % (k, v) for k, v in times.items() if k != "total")))
    s.LOG.write("decomp version times: " + json.dumps({k: round(v, 1) for k, v in times.items()}))
    if not getattr(ctx.args, "keep_work", False):
        shutil.rmtree(lp(work), ignore_errors=True)
    return exe, rev, len(units)
