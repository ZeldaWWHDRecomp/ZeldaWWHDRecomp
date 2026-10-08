#!/usr/bin/env python3
"""Statically recompile a Wii U RPX into C.

usage: recomp.py game/code/cking.rpx OUTDIR [--insns-per-file N] [--build NAME]

The rpx is identified by its SHA-256 (tools/recomp/builds.py). Functions are named by their
*canonical* (USA) address, so the runtime refers to the same f_XXXXXXXX whichever build of the game
it was translated from; the dispatch table maps this build's real addresses to them.

Output:
  OUTDIR/funcs.h         prototypes of every recompiled function and import
  OUTDIR/code_NNN.c      recompiled functions
  OUTDIR/table.c         guest address -> host function table
  OUTDIR/imports.c       weak default implementations of imported functions
  OUTDIR/imports.json    import slot addresses (for the runtime loader)
  OUTDIR/report.txt      statistics and unhandled instructions
"""
import argparse
import bisect
import collections
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import builds as game_builds
from analyze import Program, sext
from ppc2c import translate, Unhandled
from rpx import R_PPC_ADDR16_HA, R_PPC_ADDR16_LO, R_PPC_ADDR16_HI


def unknown_build_message(path):
    return "%s is not a build of the game this port knows (SHA-256 %s...); known: %s" % (
        path, game_builds.file_sha256(path)[:16],
        ", ".join("%s %s" % (b.name, b.title_id) for b in game_builds.all_builds()))


def c_ident(s):
    return re.sub(r"[^A-Za-z0-9_]", "_", s)


def branch_target(addr, w):
    """Static target of a non-linking b/bc, or None."""
    op = w >> 26
    if op == 18 and not (w & 1):
        return (sext(w & 0x03FFFFFC, 26) + (0 if w & 2 else addr)) & 0xFFFFFFFF
    if op == 16 and not (w & 1):
        return (sext(w & 0xFFFC, 16) + (0 if w & 2 else addr)) & 0xFFFFFFFF
    return None


# imported data objects get runtime-owned storage at fixed addresses
DATA_IMPORT_BASE = 0xC1000000
DATA_IMPORT_STRIDE = 0x1000


class Recompiler:
    def __init__(self, path, build=None):
        """`build` is which build of the game `path` is (tools/recomp/builds.py); by default it is
        identified by its SHA-256, and an rpx that is none of them is refused."""
        self.build = build or game_builds.identify(path)
        if self.build is None:
            raise SystemExit(unknown_build_message(path))
        self.p = Program(path)
        self.p.discover()
        self.entries = set(self.p.entries)
        self.imports = {}  # slot address -> (lib, name, kind)
        self.data_import_addr = {}  # slot address -> runtime storage address
        for sym in self.p.rpx.symbols:
            if sym.import_lib and sym.type != 3:  # skip section symbols
                self.imports[sym.value] = (sym.import_lib, sym.name, sym.import_kind)
        for i, slot in enumerate(sorted(s for s, v in self.imports.items() if v[2] == "d")):
            self.data_import_addr[slot] = DATA_IMPORT_BASE + i * DATA_IMPORT_STRIDE
        self._imm_overrides()
        # Game functions replaced by runtime hooks (tools/recomp/hooks.txt: one address per line,
        # canonical i.e. USA) plus the extra lists (hooks_*.txt). "@ADDR" is an instruction-level
        # hook: site_ADDR(c) runs just before the instruction at ADDR (also when ADDR is reached by
        # a branch), so it can adjust what that instruction uses.
        hook_entries, self.skipped_hooks = game_builds.read_hooks(game_builds.hook_files(), self.build)
        self.canon_of = {}              # this build's address -> canonical address (for symbol names)
        self.hooks, self.sites = set(), set()
        for site, canon, addr, _ in hook_entries:
            self.canon_of[addr] = canon
            (self.sites if site else self.hooks).add(addr)
        self._fixpoint()
        self._check_hooks(hook_entries)

    def _check_hooks(self, hook_entries):
        """A hook is written against the canonical code, so check that it can mean the same thing in
        this build (see tools/recomp/mkbuildmap.py). An instruction-level site inside a function the
        build compiled differently patches different code: that is an error. A hook on the entry of
        such a function wraps it whole, which usually still holds, but it is reported."""
        self.hooks_in_changed = []
        for site, canon, addr, where in hook_entries:
            if self.build.body_differs(canon):
                if site or addr not in self.entries:
                    raise SystemExit('%s: %08X is inside a function the %s build compiled differently; the '
                                     'hook is written for the canonical (USA) code. Mark its file with '
                                     '"# builds: USA" or write the hook for this build.' % (
                                         where, canon, self.build.name))
                self.hooks_in_changed.append((where, canon, addr))
                print("warning: %s: the %s build compiled %08X (there: %08X) differently; the hook wraps "
                      "the whole function, so check that it still means the same thing" % (
                          where, self.build.name, canon, addr), file=sys.stderr)
            if not self.p.in_text(addr):
                raise SystemExit("%s: %08X (%s build: %08X) is outside the game's code" % (
                    where, canon, self.build.name, addr))
            if not site and addr not in self.entries:
                raise SystemExit("%s: %08X (%s build: %08X) is not the start of a function there" % (
                    where, canon, self.build.name, addr))

    def _imm_overrides(self):
        """Resolve the immediates of instructions referencing imported symbols."""
        self.imm_override = {}
        for sec, addr, typ, sym, add in self.p.rpx.relocs:
            if not sym.import_lib or sec.name != ".text":
                continue
            s = (self.data_import_addr.get(sym.value, sym.value) + add) & 0xFFFFFFFF
            v = {R_PPC_ADDR16_HA: ((s + 0x8000) >> 16) & 0xFFFF,
                 R_PPC_ADDR16_LO: s & 0xFFFF,
                 R_PPC_ADDR16_HI: s >> 16}.get(typ)
            if v is not None:
                self.imm_override[addr & ~3] = v

    def _bounds(self):
        self.sorted_entries = sorted(self.entries)

    def func_of(self, a):
        i = bisect.bisect_right(self.sorted_entries, a) - 1
        return self.sorted_entries[i] if i >= 0 else None

    def func_end(self, start):
        i = bisect.bisect_right(self.sorted_entries, start)
        return self.sorted_entries[i] if i < len(self.sorted_entries) else self.p.text_hi

    def _fixpoint(self):
        """Branch targets that land inside another function become entries."""
        rounds = 0
        while True:
            self._bounds()
            new = set()
            for i, w in enumerate(self.p.words):
                a = self.p.text_lo + 4 * i
                t = branch_target(a, w)
                if t is None or not self.p.in_text(t) or t in self.entries:
                    continue
                if self.func_of(t) != self.func_of(a):
                    new.add(t)
            rounds += 1
            if not new:
                break
            self.entries |= new
        self.fixpoint_rounds = rounds
        self._bounds()

    # --- callbacks used by ppc2c.translate ---
    def branch(self, addr, tgt):
        if addr in self.p.import_calls:  # tail call into an imported function
            lib, name, slot = self.p.import_calls[addr]
            self.used_imports.add(slot)
            return "MUSTTAIL return %s(c);" % self.imp_name(slot)
        if self.cur_start <= tgt < self.cur_end:
            self.labels.add(tgt)
            if tgt <= addr:  # loop back-edge: re-read guest memory (spin waits, see PPC_LOOP in ppc.h)
                return "PPC_LOOP(); goto L_%08X;" % tgt
            return "goto L_%08X;" % tgt
        if tgt in self.entries:
            return "MUSTTAIL return f_%08X(c);" % self.sym(tgt)
        return "c->pc = 0x%08Xu; MUSTTAIL return ppc_dispatch(c);" % tgt

    def call(self, addr, tgt):
        if addr in self.p.import_calls:
            lib, name, slot = self.p.import_calls[addr]
            self.used_imports.add(slot)
            return "%s(c);" % self.imp_name(slot)
        if addr in self.p.undef_calls:
            return "ppc_unimplemented(c, 0x%08Xu, 0); /* call to undefined symbol */" % addr
        if tgt in self.entries:
            return "f_%08X(c);" % self.sym(tgt)
        return "c->pc = 0x%08Xu; ppc_dispatch(c);" % tgt

    def ret(self):
        return "return;"

    def indirect_jump(self, addr):
        jt = self.p.jump_tables.get(addr)
        if jt:
            base, count = jt
            cases = []
            for i in range(count):
                slot = base + 4 * i
                if self.cur_start <= slot < self.cur_end:
                    self.labels.add(slot)
                    cases.append("case 0x%08Xu: goto L_%08X;" % (slot, slot))
            back = any(self.cur_start <= base + 4 * i <= addr for i in range(count))  # may loop: see branch()
            return "%sswitch (c->ctr) { %s } c->pc = c->ctr; MUSTTAIL return ppc_dispatch(c);" % (
                "PPC_LOOP(); " if back else "", " ".join(cases))
        return "c->pc = c->ctr; MUSTTAIL return ppc_dispatch(c);"

    def sym(self, addr):
        """The canonical (USA) address `addr` is named by: the same number for the USA build, the
        function's canonical address for any other. Generated symbols use it so that the runtime's
        f_XXXXXXXX, hook_XXXXXXXX and site_XXXXXXXX never change with the build."""
        return self.canon_of.get(addr) or self.build.canon_code(addr)

    def imp_name(self, slot):
        lib, name, kind = self.imports[slot]
        return "imp_%s_%s" % (c_ident(lib.replace(".rpl", "")), c_ident(name))

    # --- emission ---
    def emit_function(self, start):
        self.cur_start, self.cur_end = start, self.func_end(start)
        self.labels = set()
        body = []
        for a in range(start, self.cur_end, 4):
            w = self.p.word(a)
            try:
                s = translate(a, w, self)
            except Unhandled as e:
                self.unhandled[str(e)] += 1
                s = "ppc_unimplemented(c, 0x%08Xu, 0x%08Xu);" % (a, w)
            body.append((a, w, s))
        # restrict: guest memory never aliases the register file, so the compiler may keep
        # registers in host registers across guest loads/stores
        hooked = start in self.hooks
        name = self.sym(start)
        fname = "f_%08X_orig" % name if hooked else "f_%08X" % name
        out = []
        if hooked:
            # runtime hook: callers reach hook_X, which may call the original code (f_X_orig)
            out.append("void f_%08X(Cpu* __restrict c) { hook_%08X(c); }\n" % (name, name))
        out += ["void %s(Cpu* __restrict c) {" % fname, "    PPC_ENTER(0x%08Xu);" % start]
        for a, w, s in body:
            if a in self.labels:
                out.append("L_%08X: ;" % a)
            if a in self.sites:
                out.append("    site_%08X(c);" % self.sym(a))
            out.append("    %s /* %08X: %08X */" % (s, a, w))
        # fall through into the next function
        if self.cur_end < self.p.text_hi:
            # code falling into a hooked function continues with its original code
            nxt = ("f_%08X_orig" if self.cur_end in self.hooks else "f_%08X") % self.sym(self.cur_end)
            out.append("    MUSTTAIL return %s(c);" % nxt)
        else:
            out.append("    ppc_unimplemented(c, 0x%08Xu, 0); /* fell off end of text */" % self.cur_end)
        out.append("}")
        return "\n".join(out), len(body)

    def run(self, outdir, per_file):
        os.makedirs(outdir, exist_ok=True)
        self.unhandled = collections.Counter()
        self.used_imports = set()
        self.imm_override = self.imm_override
        files, cur, n = [], [], 0
        for start in self.sorted_entries:
            src, count = self.emit_function(start)
            cur.append(src)
            n += count
            if n >= per_file:
                files.append(cur)
                cur, n = [], 0
        if cur:
            files.append(cur)
        for i, funcs in enumerate(files):
            with open(os.path.join(outdir, "code_%03d.c" % i), "w") as f:
                f.write('#include "funcs.h"\n\n')
                f.write("\n\n".join(funcs))
                f.write("\n")
        self.write_headers(outdir)
        self.write_report(outdir, len(files))

    def write_headers(self, outdir):
        func_slots = sorted(s for s, (lib, name, kind) in self.imports.items() if kind == "f")
        with open(os.path.join(outdir, "funcs.h"), "w") as f:
            f.write('#pragma once\n#include "ppc.h"\n\n')
            for e in self.sorted_entries:
                f.write("void f_%08X(Cpu* __restrict c);\n" % self.sym(e))
            f.write("\n/* hooked functions: hook_X is implemented in the runtime, f_X_orig is the game's code */\n")
            for e in sorted(self.sym(a) for a in self.hooks):
                f.write("void f_%08X_orig(Cpu* __restrict c);\nvoid hook_%08X(Cpu* c);\n" % (e, e))
            f.write("\n/* instruction-level hooks (\"@ADDR\" in hooks.txt), run before the instruction at ADDR */\n")
            for e in sorted(self.sym(a) for a in self.sites):
                f.write("void site_%08X(Cpu* c);\n" % e)
            f.write("\n/* imported functions */\n")
            for s in func_slots:
                f.write("void %s(Cpu* c);\n" % self.imp_name(s))
        with open(os.path.join(outdir, "table.c"), "w") as f:
            f.write('#include "funcs.h"\n#include "recomp_table.h"\n#include "guest_addr.h"\n\n')
            f.write("const RecompEntry g_recomp_funcs[] = {\n")
            for e in self.sorted_entries:
                f.write("    {0x%08Xu, f_%08X},\n" % (e, self.sym(e)))
            f.write("};\nconst unsigned g_recomp_func_count = %d;\n\n" % len(self.sorted_entries))
            f.write("const RecompImport g_recomp_imports[] = {\n")
            for s, (lib, name, kind) in sorted(self.imports.items()):
                fn = self.imp_name(s) if kind == "f" else "0"
                addr = self.data_import_addr.get(s, s)
                f.write('    {0x%08Xu, 0x%08Xu, "%s", "%s", %d, %s},\n' % (s, addr, lib, name, kind == "f", fn))
            f.write("};\nconst unsigned g_recomp_import_count = %d;\n" % len(self.imports))
            f.write("const uint32_t g_recomp_entry_point = 0x%08Xu;\n" % self.p.entry)
            self.write_build_map(f)
        with open(os.path.join(outdir, "imports.c"), "w") as f:
            f.write('#include "funcs.h"\n\nvoid hle_unimplemented(Cpu* c, const char* lib, const char* name);\n\n')
            for s in func_slots:
                lib, name, _ = self.imports[s]
                f.write('__attribute__((weak)) void %s(Cpu* c) { hle_unimplemented(c, "%s", "%s"); }\n' % (
                    self.imp_name(s), lib, name))
        with open(os.path.join(outdir, "imports.json"), "w") as f:
            json.dump([{"slot": s, "lib": l, "name": n, "kind": k} for s, (l, n, k) in sorted(self.imports.items())], f, indent=1)

    def write_build_map(self, f):
        """The address map of this build, for the runtime (runtime/include/guest_addr.h)."""
        b = self.build
        f.write('\n/* %s build: canonical (USA) address -> this build\'s (tools/recomp/builds.py) */\n' % b.name)
        f.write('const char g_guest_build_name[] = "%s";\n' % b.name)
        f.write('const char g_guest_build_title_id[] = "%s";\n' % b.title_id)
        for kind, steps in (("code", b.code_steps()), ("data", b.data_steps())):
            f.write("const GuestStep g_guest_%s_steps[] = {%s};\n" % (
                kind, ", ".join("{0x%08Xu, %d}" % (a, d) for a, d in steps)))
            f.write("const unsigned g_guest_%s_step_count = %d;\n" % (kind, len(steps)))

        for kind in ("code", "data"):
            lo, hi = b.bounds.get(kind, (0, 0))
            f.write("const uint32_t g_guest_%s_lo = 0x%08Xu, g_guest_%s_hi = 0x%08Xu;\n" % (kind, lo, kind, hi))
        changed = ", ".join("{0x%08Xu, 0x%08Xu}" % (a, a + size) for a, size, _, _ in b.differing)
        f.write("const GuestChanged g_guest_changed_code[] = {%s};\n" % (changed or "{0u, 0u}"))
        f.write("const unsigned g_guest_changed_code_count = %d;\n" % len(b.differing))

    def write_report(self, outdir, nfiles):
        with open(os.path.join(outdir, "report.txt"), "w") as f:
            f.write("build: %s (title %s)\n" % (self.build.name, self.build.title_id))
            for name, only in self.skipped_hooks:
                f.write("hooks skipped: %s (only for the %s build)\n" % (name, only))
            for where, canon, addr in self.hooks_in_changed:
                f.write("hook on a function this build compiled differently: %08X (here %08X, %s)\n" % (
                    canon, addr, where))
            f.write("functions: %d\nfiles: %d\nfixpoint rounds: %d\n" % (len(self.sorted_entries), nfiles, self.fixpoint_rounds))
            f.write("imports used: %d of %d\n" % (len(self.used_imports), len(self.imports)))
            f.write("unhandled instruction kinds:\n")
            for k, v in self.unhandled.most_common():
                f.write("  %6d  %s\n" % (v, k))
        print(open(os.path.join(outdir, "report.txt")).read())


if __name__ == "__main__":
    ap = argparse.ArgumentParser(usage=__doc__.strip().splitlines()[2].replace("usage: ", ""))
    ap.add_argument("rpx")
    ap.add_argument("outdir")
    ap.add_argument("--insns-per-file", type=int, default=30000)
    ap.add_argument("--build", help="which build the rpx is, when it should not be identified by "
                                    "its SHA-256 (%s)" % ", ".join(b.name for b in game_builds.all_builds()))
    a = ap.parse_args()
    build = None
    if a.build:
        build = game_builds.by_name(a.build)
        if build is None:
            sys.exit("unknown build %r; known: %s" % (a.build, ", ".join(b.name for b in game_builds.all_builds())))
    Recompiler(a.rpx, build).run(a.outdir, a.insns_per_file)
