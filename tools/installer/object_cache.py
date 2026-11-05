# SPDX-License-Identifier: MPL-2.0
"""Compiled game-code objects kept by their inputs, so a build that differs in a few files compiles only those.

A randomized game's code differs from another seed's in a few hundred functions; the translated sources
(code_NNN.c) that hold none of them are the same, and so are their objects. The key of an object is the
SHA-256 of the compiler's identity (its command and `--version`), the compile flags with the build folder's
own paths replaced by fixed names, and the preprocessed source (`-E -P`: every header it includes, without
line markers). The same key means the compiler gets the same input, so the cached object is the one it would
make. Objects are stored by key in <data>/code-objects/<2 hex>/<key>.o, written to a temporary name first
and renamed into place (a reader sees a whole object or none); the oldest are removed once the folder holds
more than LIMIT bytes.

Nothing in the cache holds game bytes beyond what the build's own objects hold; it stays on the player's
machine, beside the builds it speeds up (code-builds/).
"""
import hashlib
import os
import subprocess
import time
from pathlib import Path

LIMIT = 1 << 30  # a full set of the game's objects is about 50 MB


class Cache:
    def __init__(self, root, compiler_id, flags, replacements):
        """compiler_id: the compiler's command and version text; flags: the compile flags as given to the compiler;
        replacements: {path: name} folder paths in the flags that differ between builds (the generated code's)"""
        self.root = Path(root)
        norm = []
        for f in flags:
            for path, name in replacements.items():
                f = f.replace(path, name)
            norm.append(f)
        self.base = hashlib.sha256(("wwhd object cache 1\0" + compiler_id + "\0" + "\0".join(norm)).encode()).digest()
        self.hits = self.misses = 0

    def key(self, cc, flags, src, env=None):
        """the key of one source: its preprocessed text with this cache's base; None when preprocessing fails"""
        p = subprocess.run(cc + flags + ["-E", "-P", src], env=env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        if p.returncode != 0:
            return None
        h = hashlib.sha256(self.base)
        h.update(p.stdout)
        return h.hexdigest()

    def path(self, key):
        return self.root / key[:2] / (key + ".o")

    def fetch(self, key, obj):
        """copies the cached object to obj; True when there was one"""
        if not key:
            return False
        p = self.path(key)
        try:
            data = p.read_bytes()
        except OSError:
            self.misses += 1
            return False
        tmp = Path(str(obj) + ".tmp")
        tmp.write_bytes(data)
        os.replace(tmp, obj)
        try:
            os.utime(p)  # (used: the newest are kept)
        except OSError:
            pass
        self.hits += 1
        return True

    def store(self, key, obj):
        if not key:
            return
        p = self.path(key)
        try:
            p.parent.mkdir(parents=True, exist_ok=True)
            tmp = p.with_name("%s.%d.tmp" % (p.name, os.getpid()))
            tmp.write_bytes(Path(obj).read_bytes())
            os.replace(tmp, p)
        except OSError:
            pass  # a cache that can't be written only makes the next build slower

    def trim(self, limit=LIMIT):
        """removes the least recently used objects beyond `limit` bytes (cache entries only, never anything else)"""
        files = []
        for p in self.root.glob("??/*.o"):
            try:
                st = p.stat()
            except OSError:
                continue
            files.append((st.st_mtime, st.st_size, p))
        total = sum(f[1] for f in files)
        for _, size, p in sorted(files):
            if total <= limit:
                break
            try:
                p.unlink()
                total -= size
            except OSError:
                pass
        for p in self.root.glob("??/*.tmp"):  # a crashed writer's leftovers (older than an hour)
            try:
                if p.stat().st_mtime < time.time() - 3600:
                    p.unlink()
            except OSError:
                pass
