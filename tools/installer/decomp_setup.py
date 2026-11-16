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
from types import SimpleNamespace

DECOMP_TEXT = (
    "Builds a second version of the game in which the game functions of the public decompilation "
    "(ZeldaWWHDDecomp/wwhd) run as compiled source instead of translated code, in its fast mode (it starts with "
    "Vulkan). Experimental; the normal version stays the default and is built first. Adds about an hour to setup "
    "on a busy Mac (less on a quiet one), needs about 4 GB free while it builds and about 800 MB afterwards, uses up "
    "to about 4.5 GB of memory, and downloads the decompilation's source (about 17 MB) from GitHub.")
DOCS_URL = "https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/blob/devel/docs/decomp-build.md"
ARCHIVE_URL = "https://github.com/ZeldaWWHDDecomp/wwhd/archive/%s.tar.gz"  # % the full commit
# the parts of the decompilation the decomp version is built from (tools/release/package.py hashes the same)
# (all of tools/verify: fast mode's declaration table runs its decl_lint.py with the modules it uses)
SOURCE_PATHS = ["wwhd_src", "tools/verify", "LICENSE", "README.md"]
FETCH_FAILED = "Couldn't download the decomp source \u2014 check your internet connection and try again"
DEV_VERSION = re.compile(r"-devel\.[0-9a-f]+$|^development build$")


def is_dev_version(version):
    """A development build (release.yml devel builds: VERSION-devel.SHA8)."""
    return bool(DEV_VERSION.search(version or ""))


def offered(manifest, is_mac):
    """Setup offers the decomp version on macOS, in every release that brings its parts (development builds and,
    since 2026-11-16, regular releases)."""
    return bool(is_mac and isinstance(manifest.get("decomp"), dict))


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




# fast mode (cmake/Decomp.cmake WWHD_DECOMP_FAST and the switches it defaults to ON; test_setup.py DecompFastArgs checks
# them against Decomp.cmake): gen.py's extra outputs and native_rules.py's fast rules
FAST_GEN_ARGS = ["--hooked-bodies", "--hcall", "--ptr-ic", "--stackarg"]
FAST_RULES_ARGS = ["--lift", "autoframe,local,tailptr", "--fast", "--hcall", "--a3-fn", "--a3-stub", "--leaf", "--bare",
                   "--bare-entry", "--unbased", "--stackarg", "--accessor"]
HOOK_SHARD = 4096  # gen.py --hook-shard (cmake WWHD_DECOMP_HOOK_SHARD): one hooks.cpp took 30-37 min on a Mac
WHOLE_LTO_MIN_RAM = 12 << 30  # cmake WWHD_DECOMP_FAST_WHOLE_LTO=AUTO (WWHD_DECOMP_FAST_WHOLE_LTO_MIN_RAM_GB)
# what a build needs free in the data folder (the work folder at its largest plus the result; measured on a Mac,
# docs/decomp-build.md "In setup"), checked before it starts
SPACE_NEEDED = {"fast": 4 << 30, "plain": 3 << 30}
# fast mode's declaration table (decl_table.py) runs the decompilation's decl_lint.py, which disassembles the game
# code with capstone (BSD-3-Clause; a py3-none wheel: no compiled Python module, its own libcapstone). Downloaded
# once into DATA/decomp-src, checked by its SHA-256.
CAPSTONE = {
    "arm64": {"url": "https://files.pythonhosted.org/packages/8f/43/29cd1dbdb2b55bf339bca36444809503b8311dcc04898726a7e35b47ac86/"
                     "capstone-5.0.7-py3-none-macosx_11_0_arm64.whl",
              "sha256": "acb89f5bf6f625745a104a3a44819d3acea173228055c1eadc60d2282ae490bb", "size": 1200084},
    "x86_64": {"url": "https://files.pythonhosted.org/packages/b7/6c/f02847b3385651eb00a6a27049bd12982cdd61e981b248e7f0dc8ed15756/"
                      "capstone-5.0.7-py3-none-macosx_10_9_x86_64.whl",
               "sha256": "a9f64e3d75d8c4d7b3d26bba153b2992aadcf6b8d57674b4ef176b4ecdd9822f", "size": 1188995},
}
MODES = ("fast", "plain")


def mode_offered(manifest, mode):
    """fast mode needs the release's fast parts (tools/release/package.py --decomp-fast-build); plain is always there"""
    d = manifest.get("decomp") or {}
    return mode == "plain" or isinstance(d.get("fast"), dict)


def fetch_capstone(s, data_dir):
    """capstone for decl_lint.py: DATA/decomp-src/capstone-<sha8>, the folder to put on PYTHONPATH"""
    import platform
    import zipfile
    pin = CAPSTONE.get(platform.machine())
    if not pin:
        raise s.SetupError("the decomp version's fast mode has no disassembler for this processor (%s)" % platform.machine())
    root = os.path.join(data_dir, "decomp-src", "capstone-" + pin["sha256"][:8])
    if os.path.isfile(os.path.join(root, "capstone", "__init__.py")):
        return root
    os.makedirs(os.path.dirname(root), exist_ok=True)
    whl, tmp = root + ".whl", root + ".tmp"
    try:
        s.download(pin["url"], whl, pin["sha256"], pin["size"], "Downloading the disassembler for the decomp version")
        shutil.rmtree(tmp, ignore_errors=True)
        with zipfile.ZipFile(whl) as z:
            for m in z.infolist():
                if m.filename.startswith("/") or ".." in m.filename.split("/"):
                    raise ValueError("unsafe path in the wheel: %s" % m.filename)
            z.extractall(tmp)
        os.replace(tmp, root)
    except (s.SetupError, OSError, ValueError, zipfile.BadZipFile) as e:
        shutil.rmtree(tmp, ignore_errors=True)
        raise s.SetupError("%s (the disassembler: %s)" % (FETCH_FAILED, str(e).splitlines()[0][:200]))
    finally:
        if os.path.exists(whl):
            os.remove(whl)
    return root


def tool_path(tc, name):
    """the toolchain's NAME as one path (xcrun -f on the Mac)"""
    if tc.cxx and os.path.basename(tc.cxx[0]) == "xcrun":
        return subprocess.run(["xcrun", "-f", name], capture_output=True, text=True, check=True).stdout.strip()
    return shutil.which(name) or name


def liblto_of(tc):
    """the compiler's own libLTO.dylib (cmake/Decomp.cmake: <resource dir>/../../libLTO.dylib), or None"""
    try:
        rd = subprocess.run(tc.cxx + ["-print-resource-dir"], capture_output=True, text=True, env=tc.env).stdout.strip()
    except OSError:
        return None
    lib = os.path.abspath(os.path.join(rd, "..", "..", "libLTO.dylib")) if rd else ""
    return lib if os.path.isfile(lib) else None


def total_memory():
    try:
        return int(subprocess.check_output(["sysctl", "-n", "hw.memsize"]))
    except (OSError, ValueError, subprocess.CalledProcessError):
        return 0


def whole_lto(tc):
    """(on, libLTO, why): fast mode's whole-image ThinLTO link, as cmake's WWHD_DECOMP_FAST_WHOLE_LTO=AUTO decides it
    (WWHD_DECOMP_WHOLE_LTO=0/1 forces it for tests)"""
    lib = liblto_of(tc)
    force = os.environ.get("WWHD_DECOMP_WHOLE_LTO")
    if not lib:
        return False, None, "the compiler has no libLTO"
    if force in ("0", "1"):
        return force == "1", lib, "WWHD_DECOMP_WHOLE_LTO=" + force
    mem = total_memory()
    if mem < WHOLE_LTO_MIN_RAM:
        return False, lib, "%d GB of memory, the whole-image link needs %d" % (mem >> 30, WHOLE_LTO_MIN_RAM >> 30)
    return True, lib, "%d GB of memory" % (mem >> 30)


MOD_HOOKS_FILE = "decomp-mod-hooks.txt"  # DATA/: the game functions installed code mods hook (kept across repairs)


def read_hook_list(path):
    """hex addresses, one per line (# comments, blank lines) -> set of ints; a missing file -> empty"""
    out = set()
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                w = line.split("#", 1)[0].strip()
                if w:
                    out.add(int(w, 16))
    except (OSError, ValueError):
        pass
    return out


DEFAULT_HOOKS = os.path.join("tools", "decomp_build", "decomp_mod_hooks_default.txt")  # (in the release, versioned)


def guest_hook_args(s, data_dir, mode, code_mods):
    """The guest mods' hook points in the decomp version: fast mode with code-mod support keeps the functions code mods
    hook enterable (gen.py --guest-hooks; native_rules.py reads gen.py's guest_hooked.txt): the release's default list
    (the catalogue's code mods, DEFAULT_HOOKS) and DATA/decomp-mod-hooks.txt (the installed mods' own, written by the
    probe below and by the decomp version itself), merged into DATA/decomp-work/guest-hooks.txt. Off or plain build:
    none. Returns (gen args, native_rules args)."""
    if mode != "fast" or not code_mods or not gen_has_guest_hooks(s):
        return [], []
    hooks = read_hook_list(os.path.join(s.PKG, DEFAULT_HOOKS)) | read_hook_list(os.path.join(data_dir, MOD_HOOKS_FILE))
    path = os.path.join(data_dir, "decomp-work", "guest-hooks.txt")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("".join("%08X\n" % a for a in sorted(hooks)))
    return ["--guest-hooks", path], []


def gen_has_guest_hooks(s):
    """gen.py --guest-hooks (the decomp version's code-mod support in fast mode); without it the build stays as before"""
    try:
        with open(os.path.join(s.PKG, "tools", "decomp_build", "gen.py"), encoding="utf-8") as f:
            return "--guest-hooks" in f.read()
    except OSError:
        return False


def probe_mod_hooks(s, ctx, exe):
    """The game functions the installed code mods hook, as the decomp version reports them
    (WWHD_DECOMP_MOD_HOOKS_PROBE: it registers the guest mods and exits without a window). None when it cannot say."""
    tmp = os.path.join(ctx.data_dir, "decomp-work", "mod-hooks-probe.txt")
    if os.path.exists(tmp):
        os.remove(tmp)
    env = dict(os.environ, WWHD_DECOMP_MOD_HOOKS_PROBE=tmp, WWHD_CODE_MODS="1", WWHD_HIDDEN_WINDOWS="1",
               WWHD_NO_AUDIO="1", WWHD_NO_GAMEPAD="1")
    try:
        p = subprocess.run([exe, "--game", ctx.game_dir, "--save", os.path.join(ctx.data_dir, "save")], cwd=ctx.data_dir,
                           env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    except (OSError, subprocess.TimeoutExpired) as e:
        s.LOG.write("decomp mod-hook probe: %s" % e)
        return None
    if p.returncode != 0 or not os.path.isfile(tmp):
        s.LOG.write("decomp mod-hook probe: exit %d, no list\n%s" % (p.returncode, p.stdout.decode("utf-8", "replace")[-2000:]))
        return None
    return read_hook_list(tmp)


def code_mods_changed(state, hooks):
    """Seam for the code-mod rebuild (code_mods.rebuild, the game's Settings > Mods switch): the normal game was rebuilt
    with code-mod support `hooks`. The decomp version, built with the install's earlier choice, is marked stale
    (install.json "decomp_stale"; the next repair builds it again, and the game says so in Settings > Mods). Returns True
    when it changed the state."""
    if not state.get("decomp_exe") or bool(state.get("decomp_code_mods")) == bool(hooks):
        return False
    state["decomp_stale"] = "built %s code-mod support" % ("without" if hooks else "with")
    return True


def build(s, ctx, tc, jobs, mode="fast", code_mods=False, step=None):
    """See build_once; with code-mod support in fast mode the built game is asked which functions the installed code
    mods hook (probe_mod_hooks): a function the build does not keep enterable is added to DATA/decomp-mod-hooks.txt and
    the units are prepared, compiled (unchanged ones from the object cache) and linked again."""
    exe, rev, nunits, info = build_once(s, ctx, tc, jobs, mode, code_mods, keep_work=True)
    if mode == "fast" and code_mods and gen_has_guest_hooks(s):
        path = os.path.join(ctx.data_dir, MOD_HOOKS_FILE)
        have = read_hook_list(path)
        known = have | read_hook_list(os.path.join(s.PKG, DEFAULT_HOOKS))
        found = probe_mod_hooks(s, ctx, exe)
        if found is not None and not found <= known:
            with open(path, "w", encoding="utf-8") as f:
                f.write("# the game functions installed code mods hook (setup's probe and the decomp version)\n")
                f.write("".join("%08X\n" % a for a in sorted(have | found)))
            s.say("  Code mods hook %d more game functions: building the decomp version again with them..."
                  % len(found - known))
            exe, rev, nunits, info = build_once(s, ctx, tc, jobs, mode, code_mods, keep_work=True, again=True)
    if not getattr(ctx.args, "keep_work", False):
        shutil.rmtree(getattr(s, "longpath", lambda p: p)(os.path.join(ctx.data_dir, "decomp-work")), ignore_errors=True)
    return exe, rev, nunits, info


def build_once(s, ctx, tc, jobs, mode="fast", code_mods=False, keep_work=False, again=False):
    """Builds the decomp version; `s` is the setup module (run_logged, Progress, compile_gamecode, link_game,
    say, LOG, SetupError, PKG). mode: "fast" (fast mode, cmake WWHD_DECOMP_FAST: the default) or "plain" (the plain
    decomp build, setup --decomp-plain). code_mods: the game code translated with code-mod support (recomp.py
    --mod-hooks), as the install's normal game. Returns (path of the built game, decomp revision, game units, info)."""
    manifest, data_dir = ctx.manifest, ctx.data_dir
    d = manifest["decomp"]
    if mode not in MODES or not mode_offered(manifest, mode):
        raise s.SetupError("this release has no decomp %s parts" % ("fast mode" if mode == "fast" else mode))
    fast = mode == "fast"
    parts = d["fast"] if fast else d
    t_all = time.time()
    rev = d.get("revision", "")
    rpx = os.path.join(ctx.game_dir, "code", "cking.rpx")
    build_id = s.game_builds.identify(rpx)
    if build_id is not None and not build_id.canonical:
        raise s.SetupError("the decomp version works with the USA game only (yours is the %s build)" % build_id.name)
    lp = getattr(s, "longpath", lambda p: p)
    need = SPACE_NEEDED[mode]
    if s.free_space(data_dir) < need:
        raise s.SetupError("not enough free disk space in %s for building the decomp version (%s needed)"
                           % (data_dir, s.human(need)))
    work = os.path.join(data_dir, "decomp-work")
    if not again:  # (again: the first pass's translation, declaration table and objects are reused)
        shutil.rmtree(lp(work), ignore_errors=True)
        os.makedirs(lp(work))
    out = os.path.join(work, "gen")
    times = {}
    lto, liblto, lto_why = whole_lto(tc) if fast else (False, None, "")
    gen_guest, rules_guest = guest_hook_args(s, data_dir, mode, code_mods)
    s.say("  Decomp version: %s%s" % ("fast mode" if fast else "plain build (--decomp-plain)",
                                      (", whole-image link (%s)" % lto_why if lto else ", per-unit link (%s)" % lto_why)
                                      if fast else ""))

    # 0. the decompilation's source (downloaded once per pin, checked against the release's SHA-256)
    t0 = time.time()
    src = fetch_source(s, data_dir, d)
    decomp_root = src
    capstone = fetch_capstone(s, data_dir) if fast else None
    times["source"] = time.time() - t0

    # 1. function list, glue and units: gen.py, reading the game for the model data and the source glue
    t0 = time.time()
    s.say("  Preparing the decompiled source (decomp %s) with your game..." % (rev[:7] or "?"))
    s.run_logged([sys.executable, os.path.join(s.PKG, "tools", "decomp_build", "gen.py"), src,
                  out, "--game", rpx, "--jobs", str(jobs), "--game-build", "--hook-shard", str(HOOK_SHARD)]
                 + (FAST_GEN_ARGS if fast else []) + gen_guest,  # (units: -DGABI_GAME_BUILD, as cmake/Decomp.cmake)
                 what="preparing the decompiled source (gen.py)")
    with open(os.path.join(out, "units.json")) as f:
        units = json.load(f)["units"]
    times["gen"] = time.time() - t0

    # 2. the game code, translated with the decomp build's function list (and code-mod support when the install has it)
    t0 = time.time()
    s.say("  Translating the game code for the decomp version...")
    gen_dir = os.path.join(work, "code")
    hooks_list = os.path.join(out, "decomp_hooks.txt")
    extra = (["--mod-hooks"] if code_mods else []) + ["--decomp-hooks", hooks_list]
    cmd = [sys.executable, os.path.join(s.PKG, "tools", "recomp", "recomp.py"), rpx, gen_dir] + extra
    env = dict(os.environ, WWHD_RECOMP_MOD_HOOKS="1" if code_mods else "0")
    what = "translating the game code (decomp version)"
    if hasattr(s, "translate_cached"):  # kept by its inputs (build_cache.py): an update with the same translator reuses it
        s.translate_cached(cmd, rpx, gen_dir, extra, [hooks_list], data_dir, env=env, what=what)
    else:
        s.run_logged(cmd, env=env, what=what)
    times["translate"] = time.time() - t0

    # 3. native-call rules (tools/decomp_build/native_rules.py, as cmake/Decomp.cmake); fast mode first makes the
    # declaration table from this translation (decl_table.py: the decompilation's decl_lint.py, port mode)
    t0 = time.time()
    rules = [sys.executable, os.path.join(s.PKG, "tools", "decomp_build", "native_rules.py"), src, out, gen_dir]
    if fast and again:  # (the first pass's table: the same translation)
        rules += ["--decl", os.path.join(data_dir, "decomp-cache", "decl-%d" % bool(code_mods), "handoff.tsv")]
    elif fast:
        s.say("  Checking the decompiled calls against your game (declaration table)...")
        # kept in DATA/decomp-cache (one per code-mod choice: the translation differs): decl_table.py reuses it while
        # the decompilation, the translation and the game are unchanged (a repair, an update with the same pin)
        decl = os.path.join(data_dir, "decomp-cache", "decl-%d" % bool(code_mods))
        cxx_dir = os.path.dirname(tool_path(tc, "clang++"))
        denv = dict(os.environ, PYTHONPATH=os.pathsep.join(
            [capstone] + ([os.environ["PYTHONPATH"]] if os.environ.get("PYTHONPATH") else [])),
            PATH=os.pathsep.join([cxx_dir, os.environ.get("PATH", "")]))
        s.run_logged([sys.executable, os.path.join(s.PKG, "tools", "decomp_build", "decl_table.py"), src, gen_dir,
                      os.path.join(out, "verify", "image.bin"), rpx, decl, "--jobs", str(jobs), "--no-cache"],
                     env=denv, what="making the declaration table (decl_table.py)")
        shutil.rmtree(lp(os.path.join(decl, "work")), ignore_errors=True)  # (its working files: the table is kept)
        times["decl"] = time.time() - t0
        t0 = time.time()
        rules += ["--decl", os.path.join(decl, "handoff.tsv")]
    rules += ["--no-cache"] + (FAST_RULES_ARGS if fast else []) + rules_guest
    s.run_logged(rules, what="the native-call rules (native_rules.py)")
    times["rules"] = time.time() - t0

    # 4. compile: the game code, the game units, hooks.cpp; objects whose preprocessed source, flags and compiler
    # are unchanged come from the install's object cache (object_cache.py), as for the normal game
    t0 = time.time()
    objs = s.compile_gamecode(tc, manifest, gen_dir, os.path.join(work, "obj"), jobs, cache_dir=data_dir)
    times["gamecode"] = time.time() - t0
    t0 = time.time()
    m = {"sdk": fwd(os.path.join(s.PKG, "sdk")), "decomp": fwd(src),
         "tools": fwd(os.path.join(s.PKG, "tools")), "out": fwd(out)}
    # debug information as for the game code (setup.debug_info_flags: off unless WWHD_SETUP_DEBUG_INFO)
    unit_flags = s.debug_info_flags([subst(a, m) for a in parts["unit_flags"]])
    hooks_flags = s.debug_info_flags([subst(a, m) for a in parts["hooks_flags"]])
    jobs_list = []  # (unit index, source, object, extra flags)
    for i, u in enumerate(units):
        udir = os.path.join(work, "units", str(i))
        os.makedirs(udir, exist_ok=True)
        # the unit's native-call table (native_rules.py; gabi.h GABI_NATIVE_TABLE), as cmake/Decomp.cmake
        table = os.path.join(out, "native", "%d.h" % i)
        native = ["-include", fwd(table)] if os.path.isfile(table) else []
        for k, src_file in enumerate([x["path"] for x in u["sources"]] + [u["glue"]]):
            quote = u["sources"][k]["quote"] if k < len(u["sources"]) else None
            jobs_list.append((i, src_file, os.path.join(udir, "%d_%s.o" % (k, os.path.basename(src_file))),
                              native + (["-iquote", quote] if quote else [])))
    hook_srcs = sorted(f for f in os.listdir(out) if re.match(r"hooks(_\d+)?\.cpp$", f))
    for f in hook_srcs:
        jobs_list.append((None, os.path.join(out, f), os.path.join(work, f[:-4] + ".o"), None))
    total = len(jobs_list)
    pr = s.Progress("compiling the decompiled game")
    done, failures, lock = [0], [], threading.Lock()

    ucache = None
    if hasattr(s, "object_cache"):
        ucache = s.object_cache(SimpleNamespace(cc=tc.cxx, env=tc.env), data_dir, unit_flags, work, {decomp_root: "<decomp>"})
    hits = [0]

    def one(job):
        _, src_file, obj, extra = job
        flags = hooks_flags if extra is None else unit_flags + extra
        key = ucache.key(tc.cxx, flags, fwd(src_file), tc.env) if ucache else None
        cmd = tc.cxx + flags + ["-c", fwd(src_file), "-o", fwd(obj)]
        if key and ucache.fetch(key, obj):
            p = SimpleNamespace(returncode=0, stdout=b"")
            hits[0] += 1
        else:
            p = subprocess.run(cmd, env=tc.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if p.returncode == 0 and key:
                ucache.store(key, obj)
        with lock:
            if p.returncode:
                failures.append((src_file, p.stdout.decode("utf-8", "replace")))
                s.LOG.write("$ " + " ".join(cmd) + "\n" + p.stdout.decode("utf-8", "replace"))
            done[0] += 1
            pr.update(done[0], total, "%d of %d files" % (done[0], total))

    s.LOG.write("decomp unit flags: " + " ".join(tc.cxx + unit_flags))
    pr.update(0, total, "0 of %d files (%d at a time)" % (total, jobs), force=True)
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        for f in as_completed([ex.submit(one, j) for j in jobs_list]):
            f.result()
    if failures:
        src_file, text = failures[0]
        raise s.SetupError("compiling %s failed:\n%s" % (os.path.basename(src_file), "\n".join(text.strip().splitlines()[-20:])))
    if ucache:
        ucache.trim()
        s.LOG.write("decomp object cache: %d of %d unit files from the cache" % (hits[0], total))
    pr.done("%d files, %d unchanged from an earlier build" % (total, hits[0]) if ucache else "%d files" % total)
    times["units"] = time.time() - t0

    # 5. each unit becomes one object whose hidden symbols are local, so units never see each other: `ld -r` (plain),
    # ld -r with ThinLTO inside the unit (fast, per-unit link), or the IR isolation for the whole-image link (unit_ir.py
    # through the compiler's libLTO), as cmake/Decomp.cmake
    t0 = time.time()
    by_unit = {}
    for i, _, obj, _ in jobs_list:
        if i is not None:
            by_unit.setdefault(i, []).append(obj)
    if lto:
        merge = lambda o, objs_: [sys.executable, os.path.join(s.PKG, "tools", "decomp_build", "unit_ir.py"), o,
                                  "--liblto", liblto, "--cc", tool_path(tc, "clang++")] + objs_
    elif fast:
        merge = lambda o, objs_: tc.cxx + ["-r", "-nostdlib", "-flto=thin", "-o", o] + objs_
    else:
        merge = lambda o, objs_: ["xcrun", "ld", "-r", "-o", o] + objs_
    glue = [None] * len(units)
    pr = s.Progress("isolating the game units")
    mfail, mdone = [], [0]

    def merge_one(i):
        u = units[i]
        o = os.path.join(work, "units", "%s.o" % u["name"])
        try:
            s.run_logged(merge(o, by_unit[i]), env=tc.env, what="merging the game unit %s" % u["name"])
        except s.SetupError as e:
            mfail.append(e)
        glue[i] = o
        with lock:
            mdone[0] += 1
            pr.update(mdone[0], len(units), "%d of %d units" % (mdone[0], len(units)))

    with ThreadPoolExecutor(max_workers=jobs) as ex:
        list(ex.map(merge_one, range(len(units))))
    if mfail:
        raise mfail[0]
    pr.done("%d units" % len(units))
    times["merge"] = time.time() - t0

    # 6. link: the release's decomp link line with this game code, hooks and units (fast mode, whole-image: ThinLTO
    # across the units at this link)
    t0 = time.time()
    exe = exe_path(data_dir, s.PORTABLE)
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    new = exe + ".new"
    hooks_objs = [os.path.join(work, f[:-4] + ".o") for f in hook_srcs]
    link_extra = ["-flto=thin"] if lto else []  # (no ThinLTO cache: the work folder goes after the build anyway)
    s.say("  Linking the decomp version%s..." % (" (whole-image optimisation, a few minutes)" if lto else ""))
    s.link_game(tc, manifest, objs, work, new, recipe=parts["link"], glue=hooks_objs + glue, extra=link_extra)
    if not s.PORTABLE:
        for rf in manifest.get("runtime_files", []):
            shutil.copyfile(os.path.join(s.PKG, "sdk", "runtime", rf), os.path.join(os.path.dirname(exe), rf))
    os.replace(new, exe)
    times["link"] = time.time() - t0
    times["total"] = time.time() - t_all
    work_bytes = dir_size(work)
    s.say("  Built the decomp version: %s (%d s: %s)" % (exe, times["total"], ", ".join(
        "%s %d s" % (k, v) for k, v in times.items() if k != "total")))
    s.LOG.write("decomp version times: " + json.dumps({k: round(v, 1) for k, v in times.items()}) +
                "; work folder %d MB, game %d MB" % (work_bytes >> 20, os.path.getsize(exe) >> 20))
    if not keep_work and not getattr(ctx.args, "keep_work", False):
        shutil.rmtree(lp(work), ignore_errors=True)
    return exe, rev, len(units), {"mode": mode, "code_mods": bool(code_mods), "whole_lto": lto,
                                  "seconds": round(times["total"]), "work_mb": work_bytes >> 20}


def dir_size(root):
    n = 0
    for dp, _, fns in os.walk(root):
        for fn in fns:
            try:
                n += os.lstat(os.path.join(dp, fn)).st_size
            except OSError:
                pass
    return n
