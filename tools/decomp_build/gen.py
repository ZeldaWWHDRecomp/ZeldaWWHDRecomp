#!/usr/bin/env python3
"""Decomp build (CMake WWHD_DECOMP=ON, docs/decomp-build.md): which game functions run as compiled
decompiled source, and the generated glue for them.

usage: gen.py DECOMP_DIR OUT_DIR [--game RPX] [--jobs N]

DECOMP_DIR is the decompilation (the `decomp` submodule): DECOMP_DIR/wwhd_src (the source),
DECOMP_DIR/tools/verify/{include,src,units}. Every output goes to OUT_DIR (the CMake build directory,
never the source tree or the submodule) and is rewritten only when it changes:

  decomp_hooks.txt  the decompiled functions (canonical addresses). recomp.py --decomp-hooks routes
                    them through the runtime (hook_X); setup passes the same list to the recompiler.
  hooks.cpp         one slot per listed function and its hook_X entry point
                    (runtime/src/decomp/decomp.h: decompiled code or the recompiled f_X_orig)
  units.cmake       the game units and their source files, for CMakeLists.txt
  units.json        the same for setup (tools/installer/setup.py builds the decomp version)
  glue/N.cpp        per unit: tools/decomp_build/glue.cpp with the unit's name
  functions.tsv     address, unit, name of every listed function
  excluded.txt      decompiled functions that stay recompiled, with the reason
  skipped.txt       verify units that are not game code (a harness control/companion source, or a source
                    that is not in the tree), with the reason
  glued/PATH        GameCube-style sources (a `/* WWHD glue` block) as the decompilation's harness compiles
                    them: rewritten by its tools/verify/autoglue.py (function entry glue, VERIFY marks, guest
                    views of static tables, guest copies of string literals)
  include/assets/   the model data headers of tools/verify/assets.txt (tools/verify/assets.py; a decompilation
                    without assets.py has none)
  verify/image.bin  the loaded sections of the game's code (tools/verify/mkimage.py)

The last three are made from your own copy of the game (--game, cking.rpx of the USA game): the
decompilation does not contain the game's model data, and autoglue reads the content of the strings
and checks the tables against the game's code. They are game data: they stay in OUT_DIR. Sources
without a glue block need nothing from the game; a decompilation without any needs no --game.

Game units: verify units (tools/verify/units/*.txt; with the decompilation's tools/verify/specfile.py: the
specs its own gates run, sub-folders and `include` lines too) that share source files with VERIFY marks form one
game unit (connected components), named after its shortest verify-unit name. A shared source without
marks (support code: guest-call stubs, the translation unit's gc_tu) is compiled into each game unit
that lists it, as each verify unit is its own program in the decompilation's harness. Every
`VERIFY(0xADDR, fn)` in its sources is a candidate for ADDR. Excluded here: addresses the runtime already hooks (hooks*.txt, decomp_only_hooks_*.txt; the
decompiled function would bypass that hook) and addresses verified in more than one game unit (the
first unit by name keeps it). recomp.py excludes the rest, which needs the game's code: an address
that is not a function entry, or a function containing an instruction-level hook ("@ADDR").
"""
import argparse
import collections
import concurrent.futures
import glob
import json
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "recomp"))
import builds as game_builds  # noqa: E402


def unit_specs(src):
    """the verify-unit specs of the decompilation -> [(unit name, [spec lines])]. A decompilation with
    tools/verify/specfile.py (unit specs with `include` lines and specs in sub-folders) is read as its own
    gates read it: specfile.gated_specs (which leaves out units/shared/ and the units/_NAME/ self-tests and
    diagnosis specs) and specfile.expanded_lines. Without it: units/*.txt, line by line. A spec in a
    sub-folder is named with `__` for each `/` (game unit names become file names)."""
    vdir = os.path.join(src, "tools", "verify")
    if os.path.isfile(os.path.join(vdir, "specfile.py")):
        import importlib.util
        spec = importlib.util.spec_from_file_location("decomp_specfile", os.path.join(vdir, "specfile.py"))
        specfile = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(specfile)
        udir = os.path.join(vdir, "units")
        return [(name.replace("\\", "/").replace("/", "__"), specfile.expanded_lines(os.path.join(udir, name + ".txt")))
                for name in specfile.gated_specs(udir)]
    out = []
    for f in sorted(glob.glob(os.path.join(vdir, "units", "*.txt"))):
        with open(f, encoding="utf-8", errors="replace") as fh:
            out.append((os.path.basename(f)[:-4], fh.read().splitlines()))
    return out


def spec_files(src):
    """every spec file (include fragments too): CMake configures again when one of them changes"""
    return [p for p in glob.glob(os.path.join(src, "tools", "verify", "units", "**", "*.txt"), recursive=True)]


VERIFY_ONLY = "tools/verify/units/"  # sources of the harness's control and companion variants


def read_units(src):
    """-> (units: name -> [sources], skipped: [(name, why)]). A spec is not game code when it compiles a source
    of the verification harness itself (tools/verify/units/...: control and companion variants that define a
    game function a second time) or when one of its sources is not in the tree (a spec of generated
    sources); both are skipped and listed in skipped.txt."""
    units, skipped = {}, []
    for u, lines in unit_specs(src):
        srcs = []
        for line in lines:
            t = line.split("#")[0].split()
            if t and t[0] == "src":
                srcs += [x for x in t[1:] if x not in srcs]
        if not srcs:
            continue
        own = [x for x in srcs if x.replace("\\", "/").startswith(VERIFY_ONLY)]
        missing = [x for x in srcs if not os.path.isfile(os.path.join(src, x))]
        if own:
            skipped.append((u, "verify-only source %s" % " ".join(own)))
        elif missing:
            skipped.append((u, "source not in the tree: %s" % " ".join(missing)))
        else:
            units[u] = srcs
    return units, skipped


def components(units, joins=None):
    """game units: verify units connected by shared sources; only sources in `joins` (when given) connect
    (a shared support source without VERIFY marks, e.g. a `_pending.cpp` of guest-call stubs or one that
    defines its translation unit's gc_tu, is compiled into each game unit that lists it instead)"""
    parent = {}

    def find(x):
        while parent.setdefault(x, x) != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for u, srcs in units.items():
        find("u:" + u)
        for s in srcs:
            if joins is None or s in joins:
                parent[find("u:" + u)] = find("s:" + s)
    groups = collections.defaultdict(lambda: ([], set()))
    for u, srcs in units.items():
        g = groups[find("u:" + u)]
        g[0].append(u)
        g[1].update(srcs)
    out = {}
    for us, srcs in groups.values():
        name = min(us, key=lambda x: (len(x), x))
        out[name] = (sorted(us), sorted(srcs))
    return out


GLUE_MARK = "/* WWHD glue"


def verify_marks_text(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)   # commented-out VERIFY lines do not count
    text = re.sub(r"//[^\n]*", "", text)
    marks = [(m.start(), int(m.group(1), 16), m.group(2).strip())
             for m in re.finditer(r"\bVERIFY\(\s*(0x[0-9A-Fa-f]+)\s*,\s*([^)]*)\)", text)]
    # autoglue names an overloaded method by a typed cast: VERIFY_NAMED(0xADDR, static_cast<...>(&C::f), "name")
    marks += [(m.start(), int(m.group(1), 16), m.group(2))
              for m in re.finditer(r'\bVERIFY_NAMED\(\s*(0x[0-9A-Fa-f]+)\s*,.*?,\s*"((?:[^"\\]|\\.)*)"\s*\)\s*;', text)]
    return [(a, n) for _, a, n in sorted(marks)]


FILE_SCOPE_VIEW = re.compile(r"^((?:const\s+)?[A-Za-z_][^\n=;]*?(?:&\s*(\w+)|\(&(\w+)\)(?:\[\d+\])+)\s*=\s*"
                             r"gabi::(?:object_at|local_static)<)", re.M)
EXT_STUB = re.compile(r"^(?!static\b|inline\b)(\S[^\n]*/\* other unit \(ext\) \*/)$", re.M)
EXTERN_NAME = re.compile(r"\bextern\s+[^;{]*?(\w+)\s*\)?\s*(?:\[[^\]]*\]\s*)*;")
STATIC_DECL = re.compile(r"^static\s+[^;(=\n]*?\b(\w+)\s*(?:\[[^\]\n]*\]\s*)*[;=({]", re.M)


class Glue:
    """The decompilation's own source rewriter (tools/verify/autoglue.py) and model data headers
    (tools/verify/assets.py), with every output and the game image in OUT_DIR instead of its build/."""

    def __init__(self, src, out, rpx):
        self.src, self.out, self.rpx = src, out, rpx
        self.image = os.path.join(out, "verify", "image.bin")
        self.include = os.path.join(out, "include")
        self.mods = None

    def load(self):
        if self.mods:
            return self.mods
        if not self.rpx or not os.path.isfile(self.rpx):
            sys.exit("gen.py: the decompilation's GameCube-style sources need your own copy of the game (model "
                     "data, string and table glue): no game code at %s (CMake: -DWWHD_DECOMP_GAME=<game "
                     "folder with code/cking.rpx>)" % (self.rpx or "(none given; --game)"))
        if not os.path.exists(self.image) or os.path.getmtime(self.image) < os.path.getmtime(self.rpx):
            os.makedirs(os.path.dirname(self.image), exist_ok=True)
            # mkimage.py reads the RPX with this repository's tools/rpx.py
            env = dict(os.environ, PYTHONPATH=os.pathsep.join(
                [os.path.join(ROOT, "tools")] + ([os.environ["PYTHONPATH"]] if os.environ.get("PYTHONPATH") else [])))
            r = subprocess.run([sys.executable, os.path.join(self.src, "tools", "verify", "mkimage.py"),
                                self.rpx, self.image + ".tmp"], env=env, capture_output=True, text=True)
            if r.returncode:
                sys.exit("gen.py: mkimage.py failed:\n%s%s" % (r.stdout, r.stderr))
            os.replace(self.image + ".tmp", self.image)
        sys.path.insert(0, os.path.join(self.src, "tools", "verify"))
        # the public snapshot has tools/verify/assets.py (model data headers from the game); a decompilation
        # without it keeps that data in its sources and needs no headers
        assets = None
        if os.path.isfile(os.path.join(self.src, "tools", "verify", "assets.py")):
            import assets
        import autoglue
        with open(self.image, "rb") as fh:
            d = fh.read()
        regs, i = [], 0
        while i < len(d):
            a, n = struct.unpack_from("<II", d, i)
            i += 8
            regs.append((a, d[i:i + n]))
            i += n
        if assets:
            assets.IMAGE, assets.OUT, assets._image = self.image, os.path.join(self.include, "assets"), regs
        autoglue._image = regs
        self.mods = (assets, autoglue)
        return self.mods

    def assets(self):
        """every header of tools/verify/assets.txt (as verify.py prepare_shared does)"""
        assets, _ = self.load()
        if not assets:
            return 0
        try:
            man = assets.manifest()
            for h in man:
                assets.generate(h, man)
        except assets.AssetError as e:
            sys.exit("gen.py: assets: %s" % e)
        return len(man)

    def transform(self, rel):
        """source `rel` (relative to DECOMP_DIR) as autoglue rewrites it -> (text, statics, externs):
        `statics` the names of its file-scope statics, `externs` the names it and its headers declare
        extern (both for link_statics)"""
        _, autoglue = self.load()
        with open(os.path.join(self.src, rel), encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        try:
            res = autoglue.transform(text, rel)
        except autoglue.GlueError as e:
            sys.exit("gen.py: autoglue: %s" % e)
        headers = [text] + [t for _, t in autoglue.include_closure(text, rel)]
        externs = set()
        for t in headers:
            externs.update(EXTERN_NAME.findall(t))

        # an `ext` glue line defines a guest-call stub for a function of another verify unit. A game unit can
        # join both, or two sources with the same stub: the stub is made weak, so the real definition wins
        # and stubs merge. Inside decompiled code both reach the function through the guest anyway
        # (WWHD_GLUE_FUNC calls nested decompiled code by its address). A stub of a function the source or
        # one of its headers declares `static` is its own (and cannot be weak).
        def weak_stub(m):
            name = re.match(r"[^(]*?((?:\w+::)*~?\w+)\s*\(", m.group(1))
            if name and "::" not in name.group(1):
                decl = re.compile(r"\bstatic\b[^;{}]*\b%s\s*\(" % re.escape(name.group(1)))
                if any(decl.search(t) for t in headers):
                    return m.group(1)
            return "__attribute__((weak)) " + m.group(1)
        res = EXT_STUB.sub(weak_stub, res)
        return res, set(STATIC_DECL.findall(text)), externs


def link_statics(text, statics):
    """autoglue writes a file-scope `static T x;` as `T& x = gabi::object_at<T>(ADDR);` without the
    `static`. In the harness every verify unit is its own program, but a game unit here can join several
    verify units' sources (d_a_npc_ji1_*.cpp, each with its own `static ... l_hio`): the views of
    `statics` get the internal linkage of the statics they stand for back."""
    return FILE_SCOPE_VIEW.sub(lambda m: ("static " if (m.group(2) or m.group(3)) in statics else "") + m.group(1), text)


TU_DATA = re.compile(r"^const gc_tu_data gc_tu\s*=\s*\{", re.M)


def tu_data_fields(text):
    """the designated initialisers of a source's `const gc_tu_data gc_tu = {...};` as {field: value}"""
    m = TU_DATA.search(text)
    depth, i = 1, m.end()
    while depth:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    body = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text[m.end():i - 1], flags=re.S)
    fields, depth, cur = {}, 0, ""
    for ch in body + ",":
        if ch == "," and depth == 0:
            if cur.strip():
                k, _, v = cur.strip().partition("=")
                fields[k.strip()] = " ".join(v.split())
            cur = ""
            continue
        depth += {"{": 1, "}": -1}.get(ch, 0)
        cur += ch
    return fields


def shared_tu_data(comps, texts):
    """gc_api's per-translation-unit data (`gc_tu`, gc_api/tu.h): the sources of one original translation
    unit (d_a_player_main_06/07/08.cpp, joined in one game unit through d_a_player_main.cpp) each define
    it with what they use. In a game unit the definition that holds all the others' fields is kept and
    the others become weak (texts[s] rewritten); sources that disagree stop the build."""
    weak = {}
    for c, (_, srcs) in comps.items():
        defs = [(s, tu_data_fields(texts[s])) for s in srcs if TU_DATA.search(texts[s])]
        if len(defs) < 2:
            continue
        best = max(defs, key=lambda d: len(d[1]))
        for s, f in defs:
            bad = [k for k, v in f.items() if best[1].get(k) != v]
            if bad:
                sys.exit("gen.py: game unit %s: gc_tu of %s and %s differ in %s" % (c, s, best[0], ", ".join(bad)))
            if s != best[0]:
                weak.setdefault(s, set()).add(c)
            elif weak.get(s):
                sys.exit("gen.py: %s: gc_tu is needed strong in %s and weak in %s" % (s, c, ", ".join(weak[s])))
    for s in weak:
        texts[s] = TU_DATA.sub(lambda m: "__attribute__((weak)) " + m.group(0), texts[s], count=1)
    return set(weak)


_worker_glue = None


def _glue_worker_init(src, out, rpx):
    global _worker_glue
    _worker_glue = Glue(src, out, rpx)


def _glue_worker(rel):
    return _worker_glue.transform(rel)


def runtime_hooks():
    """The functions the runtime hooks itself (canonical addresses; instruction sites are left to recomp.py)."""
    d = os.path.join(ROOT, "tools", "recomp")
    entries, _ = game_builds.read_hooks(game_builds.hook_files(d) + game_builds.decomp_only_hook_files(d),
                                        game_builds.canonical_build())
    return {canon for site, canon, _, _ in entries if not site}


def write_if_changed(path, text):
    try:
        with open(path, encoding="utf-8") as fh:
            if fh.read() == text:
                return
    except OSError:
        pass
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    os.replace(tmp, path)


def c_string(s):
    return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"')


def cmake_path(p):
    return p.replace("\\", "/")


def decomp_revision(src):
    try:
        r = subprocess.run(["git", "-C", src, "rev-parse", "HEAD"], capture_output=True, text=True, timeout=30)
        return r.stdout.strip() if r.returncode == 0 else ""
    except (OSError, subprocess.SubprocessError):
        return ""


def main():
    ap = argparse.ArgumentParser(usage=__doc__.strip().splitlines()[3][7:])
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--game", help="cking.rpx of your own copy of the game (USA)")
    ap.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1), help="parallel autoglue processes")
    a = ap.parse_args()
    src = os.path.abspath(a.src)
    out = os.path.abspath(a.out)
    for need in ("wwhd_src", "tools/verify/include/gabi.h", "tools/verify/units"):
        if not os.path.exists(os.path.join(src, need)):
            sys.exit("gen.py: %s has no %s (run: git submodule update --init decomp)" % (src, need))
    os.makedirs(os.path.join(out, "glue"), exist_ok=True)

    units, skipped = read_units(src)
    write_if_changed(os.path.join(out, "skipped.txt"), "".join("%-40s %s\n" % x for x in skipped))
    hooks = runtime_hooks()
    gluer = Glue(src, out, os.path.abspath(a.game) if a.game else None)
    texts = {}          # source (relative) -> its text as compiled
    glued = {}          # glued source -> (file-scope statics, extern names of it and its headers)
    n_assets = 0
    to_glue = []
    for s in sorted({s for srcs in units.values() for s in srcs}):
        with open(os.path.join(src, s), encoding="utf-8", errors="replace") as fh:
            texts[s] = fh.read()
        if GLUE_MARK in texts[s]:
            to_glue.append(s)
    if to_glue:
        n_assets = gluer.assets()  # also the game image, before the workers read it
        # autoglue takes most of the time: the sources in parallel (each worker loads the image once)
        with concurrent.futures.ProcessPoolExecutor(max_workers=max(1, a.jobs), initializer=_glue_worker_init,
                                                    initargs=(src, out, gluer.rpx)) as ex:
            for s, (text, statics, externs) in zip(to_glue, ex.map(_glue_worker, to_glue, chunksize=8)):
                texts[s] = text
                glued[s] = (statics, externs)
    marked = {s for s, t in texts.items() if verify_marks_text(t)}
    comps = components(units, marked)
    alias = {u: c for c, (us, _) in comps.items() for u in us}
    # file-scope statics keep internal linkage, except names another source of the same game unit (or its
    # headers) declares extern: d_a_player_main.cpp's `static ... mAnmDataTable[]` is used by the other
    # parts through d_a_player_main_01.h, JAIZelBasic.cpp's statics through JAIZelBasic.h
    unit_externs = collections.defaultdict(set)
    for c, (_, srcs) in comps.items():
        for t in srcs:
            if t in glued:
                unit_externs[c] |= glued[t][1]
    in_units = collections.defaultdict(list)
    for c, (_, srcs) in comps.items():
        for t in srcs:
            in_units[t].append(c)
    for s, (statics, _) in glued.items():
        keep = statics - set().union(*(unit_externs[c] for c in in_units[s]))
        if keep:
            texts[s] = link_statics(texts[s], keep)
    rewritten = set(glued) | shared_tu_data(comps, texts)
    compiled = {}       # source (relative) -> the file that is compiled: itself or its rewritten copy
    for s in texts:
        if s in rewritten:
            compiled[s] = os.path.join(out, "glued", s)
            os.makedirs(os.path.dirname(compiled[s]), exist_ok=True)
            write_if_changed(compiled[s], texts[s])
        else:
            compiled[s] = os.path.join(src, s)
    n_glued = len(glued)

    owner = {}          # addr -> (unit, name, used)
    excluded = []       # (addr, unit, name, why)
    for c in sorted(comps):
        for s in comps[c][1]:
            for addr, name in verify_marks_text(texts[s]):
                if addr in owner:
                    if owner[addr][0] != c:
                        excluded.append((addr, c, name, "also verified in %s (kept there)" % owner[addr][0]))
                    continue
                if addr in hooks:
                    excluded.append((addr, c, name, "already a runtime hook"))
                    owner[addr] = (c, name, False)
                else:
                    owner[addr] = (c, name, True)
    listed = sorted(x for x, v in owner.items() if v[2])
    units_used = sorted({owner[x][0] for x in listed})
    uidx = {u: i for i, u in enumerate(units_used)}
    rev = decomp_revision(src)

    lines = ["# generated by tools/decomp_build/gen.py: the decompiled functions of the decomp build",
             "# (canonical addresses; recomp.py --decomp-hooks)%s" % (" from decomp %s" % rev if rev else "")]
    lines += ["%08X  # %s %s" % (x, owner[x][0], owner[x][1]) for x in listed]
    write_if_changed(os.path.join(out, "decomp_hooks.txt"), "\n".join(lines) + "\n")
    write_if_changed(os.path.join(out, "excluded.txt"), "".join(
        "%08X %-28s %-40s %s\n" % e for e in sorted(excluded)))
    write_if_changed(os.path.join(out, "functions.tsv"), "".join(
        "%08X\t%s\t%s\n" % (x, owner[x][0], owner[x][1]) for x in listed))

    h = ["// generated by tools/decomp_build/gen.py", '#include "decomp/decomp.h"', "", 'extern "C" {']
    h += ["void f_%08X_orig(Cpu*);" % x for x in listed]
    h += ["}", "", "namespace decomp {", "const char* const kUnits[] = {"]
    h += ["    %s," % c_string(u) for u in units_used]
    h += ["};", "const unsigned kNumUnits = %d;" % len(units_used), "const UnitAlias kAliases[] = {"]
    al = sorted((u, c) for u, c in alias.items() if c in uidx)
    h += ["    {%s, %d}," % (c_string(u), uidx[c]) for u, c in al]
    h += ["};", "const unsigned kNumAliases = %d;" % len(al), "Slot g_slots[] = {"]
    h += ["    {0x%08Xu, %d, %s, f_%08X_orig}," % (x, uidx[owner[x][0]], c_string(owner[x][1]), x) for x in listed]
    h += ["};", "const unsigned kNumSlots = %d;" % len(listed), "}  // namespace decomp", "", 'extern "C" {']
    h += ["void hook_%08X(Cpu* c) { decomp::call(c, %d); }" % (x, i) for i, x in enumerate(listed)]
    h += ["}", ""]
    write_if_changed(os.path.join(out, "hooks.cpp"), "\n".join(h))

    glue = cmake_path(os.path.join(HERE, "glue.cpp"))
    cm = ["# generated by tools/decomp_build/gen.py", "set(WWHD_DECOMP_UNIT_COUNT %d)" % len(units_used)]
    deps, quoted = set(), set()
    uj = []
    for i, u in enumerate(units_used):
        uj.append({"name": u, "glue": os.path.join(out, "glue", "%d.cpp" % i), "sources": [
            {"path": compiled[s], "quote": os.path.dirname(os.path.join(src, s))
             if compiled[s] != os.path.join(src, s) else None} for s in comps[u][1]]})
        gp = os.path.join(out, "glue", "%d.cpp" % i)
        write_if_changed(gp, "// generated by tools/decomp_build/gen.py\n#define DECOMP_UNIT %s\n#include \"%s\"\n"
                         % (c_string(u), glue))
        srcs = [cmake_path(compiled[s]) for s in comps[u][1]]
        deps.update(cmake_path(os.path.join(src, s)) for s in comps[u][1])
        cm.append('set(WWHD_DECOMP_UNIT_%d_NAME "%s")' % (i, u))
        cm.append("set(WWHD_DECOMP_UNIT_%d_SOURCES\n  %s)" % (i, "\n  ".join('"%s"' % s for s in srcs)))
        # a glued copy includes the headers next to its original ("..." includes)
        for s in comps[u][1]:
            if compiled[s] != os.path.join(src, s) and s not in quoted:
                quoted.add(s)
                cm.append('set_property(SOURCE "%s" APPEND PROPERTY COMPILE_OPTIONS -iquote "%s")' % (
                    cmake_path(compiled[s]), cmake_path(os.path.dirname(os.path.join(src, s)))))
    # what the lists above depend on: CMake configures again when one of these changes
    deps.update(cmake_path(p) for p in spec_files(src))
    if os.path.isfile(os.path.join(src, "tools", "verify", "specfile.py")):
        deps.add(cmake_path(os.path.join(src, "tools", "verify", "specfile.py")))
    deps.update(cmake_path(p) for p in game_builds.hook_files(os.path.join(ROOT, "tools", "recomp")))
    deps.update(cmake_path(p) for p in game_builds.decomp_only_hook_files(os.path.join(ROOT, "tools", "recomp")))
    deps.add(cmake_path(os.path.abspath(__file__)))
    if n_glued:
        deps.update(cmake_path(os.path.join(src, "tools", "verify", f)) for f in ("autoglue.py", "assets.py", "assets.txt")
                    if os.path.isfile(os.path.join(src, "tools", "verify", f)))
        deps.add(cmake_path(gluer.rpx))
    cm.append("set(WWHD_DECOMP_GEN_DEPENDS\n  %s)" % "\n  ".join('"%s"' % d for d in sorted(deps)))
    write_if_changed(os.path.join(out, "units.cmake"), "\n".join(cm) + "\n")
    write_if_changed(os.path.join(out, "units.json"), json.dumps(
        {"revision": rev, "functions": len(listed), "units": uj}, indent=1) + "\n")
    print("decomp: %d game units, %d functions decompiled, %d excluded (%s); %d of %d sources glued, %d model "
          "data headers" % (len(units_used), len(listed), len(excluded), os.path.join(out, "excluded.txt"),
                            n_glued, len(compiled), n_assets))
    if skipped:
        print("decomp: %d verify units skipped (not game code: %s)" % (len(skipped), os.path.join(out, "skipped.txt")))


if __name__ == "__main__":
    main()
