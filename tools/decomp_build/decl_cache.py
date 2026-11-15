#!/usr/bin/env python3
"""Decomp build, fast mode: the content-addressed cache of decl_table.py (the declaration table), shared by build
directories.

The decompilation's decl_lint.py (port mode) makes the table in one pass over everything. Most of its work is a pure
function of a part of its inputs; those parts are cached here, each keyed by a hash of exactly what it reads, so a
change recomputes only what depends on it:

  parse    decl_lint.parse_source(rel): one source's definitions and ext lines
  tu       one source's compiled call sites (decl_lint.compile_all's clang -fsyntax-only run of its glued text)
  stub     Audit.stub_refs / stub_result_used of one source (which code calls its ext stubs; the expensive header scan)
           key of all three: the source's text, the texts of the sources that share a verify unit with it (autoglue
           reads them), and the shared context: every header and non-.cpp file of wwhd_src, tools/verify (its
           include/, src/ and top-level files), runtime/include, the gen headers any of them includes, the image, the
           RPX, the compiler and the flags (paths normalised). A header change recomputes every source.
  cfg      funcdb.Cfg of one generated function, keyed by the function's text (one bundle per translation; a new
           translation starts from the newest bundle, so a hook-list change rebuilds only the functions it changed)
  livein   decl_lint's live-in table (livein.tsv), keyed by the translation (code_*.c) and the tools
  targets  decl_lint.IndirectTargets' disassembly of the RPX (targets3-*.pkl), keyed by the RPX and the tools
  handoff  the finished table and log, keyed by decl_table.py's hash of all inputs (another build directory with the
           same decompilation and translation gets it without a run)

Not cached: the interprocedural dataflow (Flow.sites, Dataflow.maydef / livein, exit_reads, ResultReads, the call-site
classes). Their memos break call-graph cycles in the order the audit first asks for a function, which depends on the
sources, so a result kept from another run could differ from the full run's; they are recomputed every time.

Every key also holds the hash of the decompilation's tools/verify/*.py and of this repository's decl_lint_run.py,
decl_cache.py and rpx.py: a tool change recomputes everything. Entries are zlib-compressed pickles written atomically
(temporary file + rename); readers need no lock. A hit refreshes the entry's time; when the cache grows past its cap the
oldest entries go (least recently used first) under an exclusive lock, by whichever run finds it too large.

Root: $WWHD_DECOMP_BUILD_CACHE (cmake WWHD_DECOMP_BUILD_CACHE), default ~/Library/Caches/wwhd-decomp-build (macOS) or
$XDG_CACHE_HOME/wwhd-decomp-build; "off" disables it. Cap: $WWHD_DECOMP_BUILD_CACHE_MB (default 3072 MB). The cache
holds data made from the player's game (sites, the RPX's disassembly): it stays on this machine like the build
directory. Pickles are trusted data: the root must be writable by this user only."""
try:
    import fcntl
except ImportError:  # Windows
    fcntl = None
    import msvcrt
import hashlib
import os
import pickle
import re
import subprocess
import sys
import time
import zlib

VERSION = b"decl_cache-1\n"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
NAMESPACES = ("parse", "tu", "stub", "cfg", "livein", "targets", "handoff")


def default_root():
    v = os.environ.get("WWHD_DECOMP_BUILD_CACHE", "").strip()
    if v:
        return None if v.lower() in ("off", "0", "no", "none") else os.path.abspath(os.path.expanduser(v))
    if sys.platform == "darwin":
        return os.path.expanduser("~/Library/Caches/wwhd-decomp-build")
    return os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache"), "wwhd-decomp-build")


def default_cap():
    try:
        return int(os.environ.get("WWHD_DECOMP_BUILD_CACHE_MB", "3072")) << 20
    except ValueError:
        return 3072 << 20


_DIGESTS = {}


def file_digest(path):
    """sha256 of a file's content (memoised per process: inputs do not change during a run); b"-" for no file"""
    d = _DIGESTS.get(path)
    if d is None:
        h = hashlib.sha256()
        try:
            with open(path, "rb") as fh:
                for chunk in iter(lambda: fh.read(1 << 20), b""):
                    h.update(chunk)
            d = h.digest()
        except (FileNotFoundError, IsADirectoryError, NotADirectoryError):
            d = b"-"
        _DIGESTS[path] = d
    return d


def hidden(name):
    """names no tool reads that the system writes on its own (Finder's .DS_Store, ._ resource forks, __pycache__):
    left out of every hash, so browsing a folder does not invalidate the cache"""
    return name.startswith(".") or name == "__pycache__"


def tree_files(root, keep=None):
    """files under root (sorted walk), relative to root, hidden names left out; keep(rel) filters"""
    out = []
    for dp, ds, fs in os.walk(root):
        ds[:] = sorted(d for d in ds if not hidden(d))
        for f in sorted(x for x in fs if not hidden(x)):
            rel = os.path.relpath(os.path.join(dp, f), root)
            if keep is None or keep(rel):
                out.append(rel)
    return out


def hash_named(h, items):
    """items: (stable name, path) -> the name and the content's digest into h"""
    for name, path in items:
        h.update(name.encode() + b"\0" + file_digest(path) + b"\n")


def tool_hash(decomp):
    """the tools every entry depends on"""
    h = hashlib.sha256(VERSION)
    v = os.path.join(decomp, "tools", "verify")
    hash_named(h, [("verify/" + f, os.path.join(v, f)) for f in sorted(os.listdir(v)) if f.endswith(".py")])
    hash_named(h, [("port/" + f, os.path.join(HERE, f)) for f in ("decl_lint_run.py", "decl_cache.py")])
    hash_named(h, [("port/rpx.py", os.path.join(ROOT, "tools", "rpx.py"))])
    return h.hexdigest()


def gen_hash(gen):
    """the translation's code (code_*.c), path-independent"""
    h = hashlib.sha256(b"gen\n")
    hash_named(h, [(f, os.path.join(gen, f)) for f in sorted(os.listdir(gen)) if re.match(r"code_\d+\.c$", f)])
    return h.hexdigest()


class Store:
    """content-addressed entries under root/<namespace>/<2 hex>/<key>; atomic writes, LRU by mtime, size cap"""

    def __init__(self, root, cap=None):
        self.root, self.cap = root, default_cap() if cap is None else cap
        self.stats = {}
        os.makedirs(root, mode=0o700, exist_ok=True)

    def path(self, ns, key):
        return os.path.join(self.root, ns, key[:2], key)

    def _count(self, ns, what):
        s = self.stats.setdefault(ns, [0, 0, 0])
        s[what] += 1

    def get_bytes(self, ns, key):
        p = self.path(ns, key)
        try:
            with open(p, "rb") as fh:
                data = fh.read()
        except OSError:
            self._count(ns, 1)
            return None
        try:
            os.utime(p)  # least recently used goes first
        except OSError:
            pass
        self._count(ns, 0)
        return data

    def put_bytes(self, ns, key, data):
        p = self.path(ns, key)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        tmp = "%s.tmp%d-%d" % (p, os.getpid(), time.monotonic_ns())
        try:
            with open(tmp, "wb") as fh:
                fh.write(data)
            os.replace(tmp, p)
        except OSError:
            try:
                os.remove(tmp)  # (this call's own temporary file)
            except OSError:
                pass
            return False
        self._count(ns, 2)
        return True

    def get(self, ns, key):
        data = self.get_bytes(ns, key)
        if data is None:
            return None
        try:
            return pickle.loads(zlib.decompress(data))
        except Exception:  # noqa: BLE001 (a damaged entry is a miss; the next put replaces it)
            self.stats[ns][0] -= 1
            self.stats[ns][1] += 1
            return None

    def put(self, ns, key, obj):
        return self.put_bytes(ns, key, zlib.compress(pickle.dumps(obj, protocol=pickle.HIGHEST_PROTOCOL), 1))

    def get_file(self, ns, key, dest):
        """copies an entry's bytes to dest (atomically); True on a hit"""
        data = self.get_bytes(ns, key)
        if data is None:
            return False
        tmp = dest + ".tmp%d" % os.getpid()
        with open(tmp, "wb") as fh:
            fh.write(data)
        os.replace(tmp, dest)
        return True

    def put_file(self, ns, key, src):
        try:
            with open(src, "rb") as fh:
                return self.put_bytes(ns, key, fh.read())
        except OSError:
            return False

    def latest(self, ns, kind):
        """the key last recorded with set_latest (a pointer file, for the cfg bundle a new translation starts from)"""
        try:
            with open(os.path.join(self.root, ns, "LATEST-" + kind)) as fh:
                return fh.read().strip() or None
        except OSError:
            return None

    def set_latest(self, ns, kind, key):
        p = os.path.join(self.root, ns, "LATEST-" + kind)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        tmp = "%s.tmp%d" % (p, os.getpid())
        with open(tmp, "w") as fh:
            fh.write(key + "\n")
        os.replace(tmp, p)

    def size(self):
        total, files = 0, []
        for ns in NAMESPACES:
            d = os.path.join(self.root, ns)
            for dp, _, fs in os.walk(d):
                for f in fs:
                    if f.startswith("LATEST-"):
                        continue
                    p = os.path.join(dp, f)
                    try:
                        st = os.stat(p)
                    except OSError:
                        continue
                    total += st.st_size
                    files.append((st.st_mtime, st.st_size, p, ".tmp" in f))
        return total, files

    def prune(self, now=None):
        """least recently used entries out until the cache is under 80 % of its cap (only when it is over the cap);
        abandoned temporary files older than an hour too. One run at a time (an exclusive lock; a busy lock skips)."""
        now = time.time() if now is None else now
        with open(os.path.join(self.root, ".lock"), "a") as lk:
            try:
                if fcntl:
                    fcntl.flock(lk, fcntl.LOCK_EX | fcntl.LOCK_NB)
                else:
                    lk.seek(0)
                    msvcrt.locking(lk.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError:
                return 0
            total, files = self.size()
            removed = 0
            for mt, sz, p, tmp in files:
                if tmp and now - mt > 3600:
                    try:
                        os.remove(p)
                        total -= sz
                        removed += 1
                    except OSError:
                        pass
            if total <= self.cap:
                return removed
            for mt, sz, p, tmp in sorted(f for f in files if not f[3]):
                if total <= self.cap * 0.8:
                    break
                try:
                    os.remove(p)
                    total -= sz
                    removed += 1
                except OSError:
                    pass
            return removed

    def summary(self):
        return ", ".join("%s %d/%d" % (ns, s[0], s[0] + s[1]) for ns, s in sorted(self.stats.items()) if s[0] + s[1])


class SourceKeys:
    """per-source keys of the parse / tu / stub entries (see the module text)"""

    INCLUDE = re.compile(rb'^\s*#\s*(?:include|import)\s*[<"]([^>"]+)[>"]', re.M)

    def __init__(self, decomp, gen, image, rpx, runtime, flags, tools_key, siblings):
        self.decomp, self.siblings = decomp, siblings
        h = hashlib.sha256(b"sources\n" + tools_key.encode() + b"\n")
        v = os.path.join(decomp, "tools", "verify")
        items = [("verify/" + f, os.path.join(v, f)) for f in sorted(os.listdir(v))
                 if os.path.isfile(os.path.join(v, f)) and not hidden(f)]
        for name, d, keep in (("verify/include", os.path.join(v, "include"), None),
                              ("verify/src", os.path.join(v, "src"), None),
                              ("wwhd_src", os.path.join(decomp, "wwhd_src"), lambda r: not r.endswith(".cpp")),
                              ("runtime/include", os.path.join(runtime, "include"), None)):
            items += [(name + "/" + r, os.path.join(d, r)) for r in tree_files(d, keep)]
        items += [("image", image), ("rpx", rpx)]
        hash_named(h, items)
        # gen headers a source could include (-I GEN): those whose name some header, harness file or source includes
        names = set()
        for _, p in items[:-2]:
            if p.endswith((".h", ".hpp", ".inc", ".inl", ".hh", ".def", ".cpp", ".c")):
                names |= {os.path.basename(m.decode("utf-8", "replace")) for m in self.INCLUDE.findall(open(p, "rb").read())}
        for r in tree_files(os.path.join(decomp, "wwhd_src"), lambda r: r.endswith(".cpp")):
            with open(os.path.join(decomp, "wwhd_src", r), "rb") as fh:
                names |= {os.path.basename(m.decode("utf-8", "replace")) for m in self.INCLUDE.findall(fh.read())}
        hash_named(h, [("gen/" + f, os.path.join(gen, f)) for f in sorted(os.listdir(gen))
                       if f in names and not re.match(r"code_\d+\.c$", f)])
        try:
            cc = subprocess.run(["clang++", "--version"], capture_output=True, text=True).stdout
        except OSError:
            cc = "no clang++"
        h.update(cc.encode() + b"\n")
        norm = [x.replace(os.path.abspath(gen), "<gen>").replace(os.path.abspath(runtime), "<runtime>")
                .replace(os.path.abspath(decomp), "<decomp>") for x in flags]
        h.update("\0".join(norm).encode() + b"\n")
        self.context = h.hexdigest()
        self.memo = {}

    def key(self, rel):
        k = self.memo.get(rel)
        if k is None:
            h = hashlib.sha256(self.context.encode() + b"\n" + rel.encode() + b"\0")
            h.update(file_digest(os.path.join(self.decomp, rel)))
            for s in sorted(self.siblings(rel)):
                h.update(b"\n" + s.encode() + b"\0" + file_digest(os.path.join(self.decomp, s)))
            k = self.memo[rel] = h.hexdigest()
        return k


class Hooks:
    """the per-unit cache hooked into decl_lint (see decl_lint_run.py): install_cfg at start (write_livein builds
    Cfgs too), install_sources once decl_lint.main has set its paths (run_compile_audit's entry); save() at the end"""

    def __init__(self, store, decomp, gen, tools_key, log=None):
        self.store, self.decomp, self.gen, self.tools_key, self.log = store, decomp, gen, tools_key, log
        self.stub_old, self.stub_new = {}, {}
        self.cfg_hooked = self.src_hooked = False
        self.cfg_built = self.cfg_reused = 0

    def note(self, msg):
        if self.log:
            self.log(msg)

    def install_cfg(self):
        """funcdb.Cfg by the function's text (False: funcdb has another shape, Cfgs are built as before)"""
        try:
            import funcdb
        except ImportError:
            return False
        if not (hasattr(funcdb, "Cfg") and hasattr(funcdb, "Dataflow") and hasattr(funcdb.Dataflow, "cfg")):
            self.note("decl_cache: funcdb has no Dataflow.cfg / Cfg: Cfgs uncached")
            return False
        store, gk = self.store, gen_hash(self.gen)
        self.bundle_key = hashlib.sha256((self.tools_key + "\n" + gk).encode()).hexdigest()
        cfgs = store.get("cfg", self.bundle_key)
        self.cfg_fresh = cfgs is None
        if self.cfg_fresh:  # a new translation: start from the newest bundle (most functions are unchanged)
            prev = store.latest("cfg", self.tools_key)
            cfgs = (store.get("cfg", prev) if prev else None) or {}
        used, Cfg, hooks = {}, funcdb.Cfg, self
        self.cfg_used = used

        def cfg(df, addr):
            c = df.cfgs.get(addr)
            if c is None:
                text = df.gen.text(addr, keep=False)
                h = hashlib.sha1(text.encode("utf-8", "surrogatepass")).digest()
                c = used.get(h)
                if c is None:
                    c = cfgs.get(h)
                    if c is None:
                        c = Cfg(text)
                        hooks.cfg_built += 1
                    else:
                        hooks.cfg_reused += 1
                    used[h] = c
                df.cfgs[addr] = c
            return c
        funcdb.Dataflow.cfg = cfg
        self.cfg_hooked = True
        return True

    def install_sources(self, D):
        """parse / tu / stub entries (False: decl_lint has another shape, those run as before)"""
        import autoglue
        need = [(D, "parse_source"), (D, "compile_all"), (D, "compile_flags"), (D, "PATHS"), (D, "Audit"),
                (autoglue, "_spec_siblings")]
        if hasattr(D, "Audit"):
            need += [(D.Audit, "stub_refs"), (D.Audit, "stub_result_used")]
        missing = [n for o, n in need if not hasattr(o, n)]
        if missing:
            self.note("decl_cache: decl_lint has no %s: sources uncached" % ", ".join(missing))
            return False
        P, store = D.PATHS, self.store
        keys = SourceKeys(self.decomp, self.gen, P.get("image") or "", P["rpx"], P["runtime"], D.compile_flags(),
                          self.tools_key, lambda rel: autoglue._spec_siblings(rel) if not os.path.isabs(rel) else ())
        self.keys = keys

        orig_parse = D.parse_source

        def parse_source(rel):
            k = keys.key(rel)
            v = store.get("parse", k)
            if v is None:
                v = orig_parse(rel)
                store.put("parse", k, v)
            return v
        D.parse_source = parse_source

        orig_compile = D.compile_all

        def compile_all(sources, jobs, scratch, cache=None):
            # (decl_lint's own cache, by file times, is left out: cache=None)
            res, todo = {}, []
            for s in sources:
                v = store.get("tu", keys.key(s))
                if v is None:
                    todo.append(s)
                else:
                    res[s] = tuple(v)
            if todo:
                got = orig_compile(todo, jobs, scratch, None)
                for s in todo:
                    sites, errs = got[s]
                    res[s] = (sites, errs)
                    if not errs.startswith("decl_lint: "):  # (a crash of the tool itself is not kept, as decl_lint does)
                        store.put("tu", keys.key(s), (sites, errs))
            return {s: res[s] for s in sources}
        D.compile_all = compile_all

        stub_old, stub_new = self.stub_old, self.stub_new

        def memo_method(name, tag):
            orig = getattr(D.Audit, name)

            def method(au, rel, a, regs):
                e = stub_old.get(rel)
                if e is None:
                    e = stub_old[rel] = store.get("stub", keys.key(rel)) or {}
                k = (tag, a, regs)
                if k in e:
                    return e[k]
                v = orig(au, rel, a, regs)
                e[k] = v
                stub_new.setdefault(rel, set()).add(k)
                return v
            setattr(D.Audit, name, method)
        memo_method("stub_refs", "refs")
        memo_method("stub_result_used", "used")
        self.src_hooked = True
        return True

    def save(self):
        for rel in self.stub_new:
            self.store.put("stub", self.keys.key(rel), self.stub_old[rel])
        if self.cfg_hooked and (self.cfg_fresh or self.cfg_built):
            # this translation's bundle: every function this run used
            self.store.put("cfg", self.bundle_key, self.cfg_used)
            self.store.set_latest("cfg", self.tools_key, self.bundle_key)
        self.note("decl_cache: %s (hits/lookups); cfg %d built, %d reused" % (
            self.store.summary(), self.cfg_built, self.cfg_reused))
