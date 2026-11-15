#!/usr/bin/env python3
"""Decomp build: the content-addressed cache of native_rules.py (the native-call rules), in the shared decomp build
cache of decl_cache.py (same root, size cap, least-recently-used pruning, atomic writes; a damaged entry is a miss).

native_rules.py reads every glued source and the headers it includes on every run. The parts of that work that are a
pure function of one file's text, or of a few whole inputs, are kept here by a hash of exactly what they read:

  nsrc    source_text_features(T): one source's bodies (features, argument counts, result kind, names called)
  nleaf   source_names(T): one source's functions and ext stubs by short name (leaf_rules)
  nscan   name_scan_text(T, NAMES, MULTILINE) of one file (NameScan, --bare-entry / --stackarg): kept per name, since
          a name's lines depend on the text and that name only; a run asking for other names adds them to the entry
          (the names an entry has answered: a shared entry under their digest)
  nhdr    header_refs(T): a file's gabi::hcall addresses and "..." includes (HeaderCalls, --hcall and NameScan)
  ncalled own_called_names(T): the names a source calls (under_declared)
  nlcall  lcall_addrs(T), nverify VERIFY addresses: a source's lean calls and VERIFY marks (unit_headers)
  ncode   scan_code: the original code's calls, tail branches and features, keyed by the image and the function /
          import-call tables
  nspec   spec_values(DIRECTIVE): keyed by the decompilation's tools/verify/*.py and its units/ tree (specfile.py
          reads nothing else)

Every key holds the tool key: this file, native_rules.py, gen.py (unit_specs), decl_cache.py (the store) and the
Python version (regular expressions). A text key is the file's content (as native_rules reads it), never its path, so
build directories and worktrees share entries; no entry holds a path.

Not cached (recomputed every run): everything that combines sources or depends on an order: the declaration table,
under_declared (it reads other sources' result kinds), result_stub_callers and a3_header_calls (the header trees),
may_park, the leaf fixed point, unbased_sources (its include-cycle cut depends on the order sources are visited),
the include-tree walks (they depend on which files exist), --bare / --bare-entry / --stackarg / --accessor, the unit
headers and every message."""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import decl_cache  # noqa: E402

VERSION = b"native_cache-1\n"
NAMESPACES = ("nsrc", "nleaf", "nscan", "nhdr", "ncalled", "nlcall", "nverify", "ncode", "nspec")


class Store(decl_cache.Store):
    """decl_cache.Store whose size (and so its pruning) counts these namespaces too: a native_rules.py run keeps the
    whole cache under the cap, least recently used entries first, whichever tool wrote them. (decl_cache.py itself is
    unchanged: it is part of every declaration-table key, and a change would recompute all of them once.)"""

    def size(self):
        total, files = super().size()
        for ns in NAMESPACES:
            for dp, _, fs in os.walk(os.path.join(self.root, ns)):
                for f in fs:
                    p = os.path.join(dp, f)
                    try:
                        st = os.stat(p)
                    except OSError:
                        continue
                    total += st.st_size
                    files.append((st.st_mtime, st.st_size, p, ".tmp" in f))
        return total, files


def tools_key():
    h = hashlib.sha256(VERSION + ("%d.%d\n" % sys.version_info[:2]).encode())
    decl_cache.hash_named(h, [("port/" + f, os.path.join(HERE, f))
                              for f in ("native_rules.py", "native_cache.py", "gen.py", "decl_cache.py")])
    return h.hexdigest()


def text_digest(text):
    return hashlib.sha256(text.encode("utf-8", "surrogatepass")).hexdigest()


class Cache:
    """the native_rules.py entries of one run (see the module text)"""

    def __init__(self, store):
        self.store, self.tools = store, tools_key()
        self._digests, self._sets = {}, {}

    def key(self, ns, *parts):
        h = hashlib.sha256(("%s\n%s\n" % (ns, self.tools)).encode())
        for x in parts:
            h.update(x.encode() if isinstance(x, str) else x)
            h.update(b"\0")
        return h.hexdigest()

    def text(self, ns, text, compute):
        """compute(text), a pure function of the text, by the text's content"""
        k = self.key(ns, text_digest(text))
        v = self.store.get(ns, k)
        if v is None:
            v = compute(text)
            self.store.put(ns, k, v)
        return v

    def names(self, text, names, multiline, compute):
        """NameScan's scan of one file: {name: [(line, allowed)]} for NAMES. The file's entry holds the lines of every
        name asked so far and the digest of that set of names (the set itself is one shared entry, "nscan" under its
        digest): a run with the same names is a hit; one with others computes only those, compute(text, missing
        names, multiline), and stores the union"""
        names = frozenset(names)
        want = self.names_digest(names)
        k = self.key("nscan", text_digest(text), "m" if multiline else "s")
        e = self.store.get("nscan", k)
        ok = isinstance(e, tuple) and len(e) == 2 and isinstance(e[0], str) and isinstance(e[1], dict)
        if ok and e[0] == want:
            got = e[1]
        else:
            asked = self.names_set(e[0]) if ok else None
            got = e[1] if asked is not None else {}
            missing = names - (asked or frozenset())
            if missing:  # (else every name asked is answered: the entry stays as it is)
                got = dict(got)
                got.update(compute(text, missing, multiline))
                self.store.put("nscan", k, (self.names_digest((asked or frozenset()) | names), got))
        return {n: got[n] for n in names if n in got}

    def names_digest(self, names):
        d = self._digests.get(names)
        if d is None:
            d = self._digests[names] = hashlib.sha256("\0".join(sorted(names)).encode()).hexdigest()
            self._sets[d] = names
            if not self.store.get_bytes("nscan", self.key("nscan-names", d)):
                self.store.put("nscan", self.key("nscan-names", d), names)
        return d

    def names_set(self, digest):
        """the names behind a digest (None: unknown, the entry is recomputed)"""
        if digest not in self._sets:
            v = self.store.get("nscan", self.key("nscan-names", digest))
            self._sets[digest] = v if isinstance(v, frozenset) else None
        return self._sets[digest]

    def code(self, image, secs, funcs, icalls, compute):
        """scan_code(secs, funcs, icalls) by the image's content and the tables"""
        import collections
        import pickle
        k = self.key("ncode", decl_cache.file_digest(image),
                     hashlib.sha256(pickle.dumps((list(funcs), sorted(icalls.items())), protocol=4)).hexdigest())
        v = self.store.get("ncode", k)
        if v is None:
            calls, tails, feat = compute(secs, funcs, icalls)
            v = (calls, tails, dict(feat))
            self.store.put("ncode", k, v)
        calls, tails, feat = v
        return calls, tails, collections.defaultdict(set, feat)

    def specs(self, src, directive, compute):
        """compute(src, directive) (spec_values) by tools/verify/*.py and the units/ tree"""
        if not hasattr(self, "spec_key"):
            v = os.path.join(src, "tools", "verify")
            items = [("verify/" + f, os.path.join(v, f)) for f in sorted(os.listdir(v))
                     if f.endswith(".py") and os.path.isfile(os.path.join(v, f)) and not decl_cache.hidden(f)]
            u = os.path.join(v, "units")
            if os.path.isdir(u):
                items += [("units/" + r, os.path.join(u, r)) for r in decl_cache.tree_files(u)]
            h = hashlib.sha256()
            decl_cache.hash_named(h, items)
            self.spec_key = h.hexdigest()
        k = self.key("nspec", self.spec_key, directive)
        r = self.store.get("nspec", k)
        if r is None:
            r = compute(src, directive)
            self.store.put("nspec", k, r)
        return r

    def summary(self):
        return self.store.summary()


def open_cache(arg):
    """--cache DIR / off (None: decl_cache.default_root, i.e. $WWHD_DECOMP_BUILD_CACHE or the user cache) -> Cache, or
    None when the cache is off"""
    if arg is None:
        root = decl_cache.default_root()
    else:
        root = None if arg.lower() in ("off", "0", "no", "none", "") else os.path.abspath(os.path.expanduser(arg))
    return Cache(Store(root)) if root else None
