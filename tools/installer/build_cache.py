# SPDX-License-Identifier: MPL-2.0
"""Incremental builds and updates: what a new release can keep from the build before it.

Translated game code (recomp.py's output) is kept by its inputs in <data>/code-translations/<16 hex>/:
the game's code (cking.rpx), every file of the shipped translator (tools/recomp, tools/rpx.py), the
translator's arguments (with the output folder replaced by a fixed name), the files those arguments name
(the decomp build's function list) and the WWHD_RECOMP_* settings it reads. The same key means the
translator gets the same input, so the kept output is the one it would write: an update whose translator
is unchanged translates nothing. A changed translator translates again (it works on the whole program),
and the compiled-object cache (object_cache.py) still reuses every object whose source came out the same.
A kept translation is used only after every file of it checks out against its record; at most KEEP are
kept, the least recently used go first, and only folders this module wrote (a complete.json naming their
own key) are ever removed. The files are kept compressed (zlib, fast level: the generated C is about
270 MB, about a tenth of that compressed), and checked against the SHA-256 of their text when used.

clone_tree copies a folder with copy-on-write clones where the file system has them (APFS clonefile,
Btrfs/XFS reflink): no extra space and no time, and the copy is independent of its source. Elsewhere it
reports that cloning is unavailable, so the caller can choose (the release update moves instead, after
telling the player).
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import zlib
from pathlib import Path

KEEP = 2
FORMAT = "wwhd translation cache 2"
LEVEL = 1


def _hash_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
    return h.hexdigest()


def tree_files(root):
    """Every file under root (relative posix path -> path), Python caches and file-browser files left out."""
    root = Path(root)
    out = {}
    for p in sorted(root.rglob("*")):
        if p.is_file() and "__pycache__" not in p.parts and p.suffix != ".pyc" and p.name not in (".DS_Store", "Thumbs.db"):
            out[p.relative_to(root).as_posix()] = p
    return out


def translation_key(pkg, rpx, args, gen_dir, extra_files=(), env=None):
    """The key of one translation: see the module text. args: recomp.py's arguments after the rpx and the
    output folder (as setup.recompile passes them); extra_files: files the arguments name."""
    env = os.environ if env is None else env
    h = hashlib.sha256((FORMAT + "\0").encode())

    def put(name, path):
        h.update(name.encode() + b"\0" + _hash_file(path).encode() + b"\0")

    put("game-code", rpx)
    pkg = Path(pkg)
    for rel, p in tree_files(pkg / "tools" / "recomp").items():
        if not rel.startswith("test_"):
            put("tools/recomp/" + rel, p)
    if (pkg / "tools" / "rpx.py").is_file():
        put("tools/rpx.py", pkg / "tools" / "rpx.py")
    norm = [str(a).replace(str(gen_dir), "<gen>") for a in args]
    for f in extra_files:
        norm = [a.replace(str(f), "<file:%s>" % _hash_file(f)) for a in norm]
    h.update(json.dumps(norm).encode() + b"\0")
    h.update(json.dumps(sorted((k, v) for k, v in env.items() if k.startswith("WWHD_RECOMP_"))).encode())
    return h.hexdigest()


def tree_hashes(root):
    return {rel: _hash_file(p) for rel, p in tree_files(root).items()}


class TranslationCache:
    def __init__(self, root, keep=KEEP):
        self.root = Path(root)
        self.keep = keep

    def entry(self, key):
        return self.root / key[:16]

    def fetch(self, key, gen_dir):
        """Writes the kept translation of key to gen_dir; True when there was a whole one (every file checks out)."""
        e = self.entry(key)
        tmp = Path(str(gen_dir) + ".fetch")
        try:
            record = json.loads((e / "complete.json").read_text(encoding="utf-8"))
            outputs = record.get("outputs")
            if record.get("key") != key or not outputs or record.get("format") != FORMAT:
                return False
            shutil.rmtree(tmp, ignore_errors=True)
            for rel, digest in outputs.items():
                data = zlib.decompress((e / "gen" / (rel + ".z")).read_bytes())
                if hashlib.sha256(data).hexdigest() != digest:
                    raise ValueError("damaged " + rel)
                out = tmp / rel
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(data)
        except (OSError, ValueError, TypeError, AttributeError, zlib.error):
            shutil.rmtree(tmp, ignore_errors=True)
            return False
        shutil.rmtree(gen_dir, ignore_errors=True)
        os.replace(tmp, gen_dir)
        try:
            os.utime(e / "complete.json")  # (used: the newest are kept)
        except OSError:
            pass
        return True

    def store(self, key, gen_dir):
        """Keeps a copy of gen_dir under key (written to a temporary folder, renamed into place)."""
        e = self.entry(key)
        tmp = self.root / (key[:16] + ".%d.tmp" % os.getpid())
        try:
            self.root.mkdir(parents=True, exist_ok=True)
            shutil.rmtree(tmp, ignore_errors=True)
            tmp.mkdir()
            outputs = {}
            for rel, path in tree_files(gen_dir).items():
                data = path.read_bytes()
                outputs[rel] = hashlib.sha256(data).hexdigest()
                out = tmp / "gen" / (rel + ".z")
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(zlib.compress(data, LEVEL))
            (tmp / "complete.json").write_text(json.dumps({"key": key, "format": FORMAT, "outputs": outputs}),
                                               encoding="utf-8")
            if e.exists():
                self._remove(e)
            os.replace(tmp, e)
        except OSError:
            shutil.rmtree(tmp, ignore_errors=True)  # a cache that can't be written only makes the next build slower
            return
        self.trim(protect=e)

    def _owned(self, e):
        try:
            record = json.loads((e / "complete.json").read_text(encoding="utf-8"))
            return isinstance(record.get("key"), str) and record["key"][:16] == e.name
        except (OSError, ValueError, AttributeError):
            return False

    def _remove(self, e):
        if self._owned(e) and not e.is_symlink():
            shutil.rmtree(e, ignore_errors=True)

    def trim(self, protect=None):
        """Keeps the `keep` most recently used translations; removes only folders this cache wrote."""
        entries = []
        for e in self.root.glob("*"):
            if e.is_dir() and not e.is_symlink() and len(e.name) == 16 and self._owned(e):
                entries.append(((e / "complete.json").stat().st_mtime, e))
        for _, e in sorted(entries, reverse=True)[self.keep:]:
            if e != protect:
                self._remove(e)
        for t in self.root.glob("*.tmp"):  # a crashed writer's leftovers (older than an hour)
            try:
                if t.is_dir() and t.stat().st_mtime < time.time() - 3600:
                    shutil.rmtree(t, ignore_errors=True)
            except OSError:
                pass


# ---------------------------------------------------------------------------------------------
# copy-on-write copies

def _clone_command(src, dst):
    if sys.platform == "darwin":
        return ["cp", "-c", "-R", "-p", str(src), str(dst)]
    if sys.platform.startswith("linux"):
        return ["cp", "--reflink=always", "-R", "-p", str(src), str(dst)]
    return None


def can_clone(src_dir, dst_dir):
    """True when a file in src_dir can be cloned (copy-on-write) into dst_dir: same volume, a file system with
    clones (APFS, Btrfs, XFS with reflink). Probes with a small file of its own, removed afterwards."""
    src_dir, dst_dir = Path(src_dir), Path(dst_dir)
    probe = src_dir / (".wwhd-clone-probe-%d" % os.getpid())
    out = dst_dir / probe.name
    try:
        dst_dir.mkdir(parents=True, exist_ok=True)
        probe.write_bytes(b"wwhd clone probe\n")
        cmd = _clone_command(probe, out)
        if not cmd:
            return False
        return subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0 and out.is_file()
    except OSError:
        return False
    finally:
        for p in (probe, out):
            try:
                p.unlink()
            except OSError:
                pass


def clone_tree(src, dst):
    """Copy-on-write copy of src (file or folder) to dst (must not exist); False when cloning is unavailable
    (nothing is left at dst then)."""
    cmd = _clone_command(src, dst)
    if not cmd or os.path.exists(dst):
        return False
    if subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
        return True
    if os.path.isdir(dst):
        shutil.rmtree(dst, ignore_errors=True)
    elif os.path.exists(dst):
        os.remove(dst)
    return False


def copy_tree(src, dst):
    """A clone where possible, else an ordinary copy."""
    if not clone_tree(src, dst):
        if os.path.isdir(src):
            shutil.copytree(src, dst, symlinks=True)
        else:
            shutil.copy2(src, dst)
