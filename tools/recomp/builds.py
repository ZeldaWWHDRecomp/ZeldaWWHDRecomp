"""The builds of the game this port knows, and how their addresses relate to the canonical ones.

Every game address written in this repository is an address of the **USA** build: the canonical id
of that function or that global. `tools/recomp/builds/*.json` holds, for every other build, the
piecewise constant shift from a canonical address to that build's address, derived and checked from
the two executables by `tools/recomp/mkbuildmap.py` (numbers only, never any of the game's code).

A hooks file (`hooks*.txt`) may start with

    # builds: USA

to say that its hooks belong to one build only. That is for hooks that patch code which another
build already has, like the European language code the USA build lacks (`hooks_language.txt`).
Everything else is checked automatically: a hook or site inside a function whose body differs
between the builds is refused, because the code it patches is not the same code there.
"""
import bisect
import glob
import hashlib
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
CANONICAL = "USA"


class Build:
    def __init__(self, d):
        self.name = d["name"]
        self.title_id = d["title_id"]
        self.sha256 = d["rpx_sha256"]
        self.bounds = {kind: tuple(int(a, 16) for a in d[kind + "_bounds"])
                       for kind in ("code", "data") if kind + "_bounds" in d}
        if self.name != CANONICAL and len(self.bounds) != 2:
            raise ValueError("noncanonical maps need verified code/data bounds")
        for lo, hi in self.bounds.values():
            if not 0 <= lo < hi <= 0xFFFFFFFF:
                raise ValueError("invalid address bounds")
        self._code = _Shift(d.get("code_steps", []))
        self._data = _Shift(d.get("data_steps", []))
        self.differing = [(int(a, 16), int(s1), int(b, 16), int(s2))
                          for a, s1, b, s2 in d.get("differing_functions", [])]
        self._differ_starts = [a for a, _, _, _ in self.differing]
        self._differ_ends = [a + s for a, s, _, _ in self.differing]

    @property
    def canonical(self):
        return self.name == CANONICAL

    def code(self, canon):
        """Canonical (USA) code address -> this build's address."""
        self.require_address("code", canon)
        if self.body_differs(canon) and canon not in self._differ_starts:
            raise ValueError("%08X is inside changed code; no instruction mapping" % canon)
        return self._code.apply(canon)

    def data(self, canon):
        """Canonical (USA) data address -> this build's address."""
        self.require_address("data", canon)
        return self._data.apply(canon)

    def require_address(self, kind, address):
        if self.canonical:
            return
        lo, hi = self.bounds[kind]
        if not lo <= address < hi:
            raise ValueError("%08X is outside verified %s bounds" % (address, kind))

    def canon_code(self, addr):
        """This build's code address -> the canonical one (the inverse of code()).

        Exact for every address the build really has. The other direction is not exact inside a
        function this build compiled shorter: the canonical bytes past its end have no address here,
        and code() would put them where the next function already is. body_differs() tells those
        apart; a function's entry always maps both ways."""
        return self._code.unapply(addr)

    def code_steps(self):
        """[(canonical start, shift)] of the code map, sorted; [(0, 0)] for the canonical build."""
        return list(zip(self._code.starts, self._code.deltas)) or [(0, 0)]

    def data_steps(self):
        """[(canonical start, shift)] of the data map, sorted; [(0, 0)] for the canonical build."""
        return list(zip(self._data.starts, self._data.deltas)) or [(0, 0)]

    def body_differs(self, canon):
        """Is `canon` inside a function this build compiled differently? Then code written against
        the canonical body (an instruction-level hook, an offset into it) does not apply here."""
        i = bisect.bisect_right(self._differ_starts, canon) - 1
        return i >= 0 and canon < self._differ_ends[i]

    def __repr__(self):
        return "<Build %s title %s>" % (self.name, self.title_id)


class _Shift:
    """A piecewise constant shift: [(start, delta), ...] sorted by start."""

    def __init__(self, steps):
        pairs = [(int(a, 16) if isinstance(a, str) else a, int(d)) for a, d in steps]
        if pairs != sorted(pairs) or len({a for a, _ in pairs}) != len(pairs):
            raise ValueError("address steps must be sorted and unique")
        if any(not 0 <= a <= 0xFFFFFFFF or not -0x80000000 <= d <= 0x7FFFFFFF
               or not 0 <= a + d <= 0xFFFFFFFF for a, d in pairs):
            raise ValueError("invalid address step")
        self.starts = [a for a, _ in pairs]
        self.deltas = [d for _, d in pairs]
        self.i_starts = sorted(a + d for a, d in pairs)
        self.i_deltas = [d for _, d in sorted(((a + d, d) for a, d in pairs))]

    def apply(self, a):
        i = bisect.bisect_right(self.starts, a) - 1
        return (a + self.deltas[i]) & 0xFFFFFFFF if i >= 0 else a

    def unapply(self, a):
        i = bisect.bisect_right(self.i_starts, a) - 1
        return (a - self.i_deltas[i]) & 0xFFFFFFFF if i >= 0 else a


def all_builds():
    builds = [canonical_build()]
    for path in sorted(glob.glob(os.path.join(HERE, "builds", "*.json"))):
        with open(path, encoding="utf-8") as source:
            builds.append(Build(json.load(source)))
    return builds


# The USA build is the canonical one: its addresses are the ids, so its map is empty. Its title and
# the SHA-256 of its cking.rpx live here, next to the other builds'.
_CANONICAL = {
    "name": CANONICAL,
    "title_id": "0005000010143500",
    "rpx_sha256": "c4f0ab300542e0bfc462696850534e71db2ad02288a7eb55e5a4cd4062f16153",
}


def canonical_build():
    return Build(_CANONICAL)


def by_name(name):
    for b in all_builds():
        if b.name.lower() == name.lower():
            return b
    return None


def by_sha256(digest):
    for b in all_builds():
        if b.sha256 == digest:
            return b
    return None


def by_title(title_id):
    for b in all_builds():
        if b.title_id == title_id:
            return b
    return None


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def identify(rpx_path):
    """Which build `rpx_path` is, or None if it is not one the port knows."""
    return by_sha256(file_sha256(rpx_path))


_BUILDS_RE = re.compile(r"^#\s*builds:\s*(.+)$", re.I)


def read_hooks(paths, build):
    """Read the hooks files that apply to `build`.

    Returns (entries, skipped): entries is a list of (is_site, canonical address, this build's
    address, "file:line"), skipped names the files that belong to another build. The caller checks
    that every entry applies to the build (the recompiler does, once it knows every function).
    """
    entries, skipped = [], []
    for path in paths:
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as source:
            lines = source.readlines()
        for lineno, line in enumerate(lines, 1):
            m = _BUILDS_RE.match(line.strip())
            if m:
                only = [s.strip().lower() for s in re.split(r"[,\s]+", m.group(1)) if s.strip()]
                if build.name.lower() not in only:
                    skipped.append((os.path.basename(path), m.group(1).strip()))
                    break
                continue
            text = line.split("#")[0].strip()
            if not text:
                continue
            where = "%s:%d" % (os.path.basename(path), lineno)
            site = text.startswith("@")
            canon = int(text[1:] if site else text, 16)
            entries.append((site, canon, build.code(canon), where))
    return entries, skipped


def hook_files(directory=None):
    d = directory or HERE
    return [os.path.join(d, "hooks.txt")] + sorted(glob.glob(os.path.join(d, "hooks_*.txt")))
