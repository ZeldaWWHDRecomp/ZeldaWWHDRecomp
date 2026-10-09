#!/usr/bin/env python3
"""Translate a guest mod (PowerPC relocatable ELF) to C for the WWHD guest mod runtime (Mod SDK v2).

usage: guestmod.py MOD.elf OUT.c --base 0x7F000000 [--report REPORT.json]

The mod is linked at the given guest base address (all relocations are applied here, so the generated
C holds final values), every function is translated with the game's own translator (tools/recomp/
ppc2c.py), and the result is one C file exporting wwhd_guest_module_v1 (runtime/include/
wwhd_guest_abi.h). Undefined symbols resolve as follows:

  __wwhd_game_0x<ADDR>   a game function: called through the runtime's dispatch (mods' hooks apply);
                         as data (address taken) it is the function's guest address
  __wwhd_orig_0x<ADDR>   the game's own code of a function, below every mod (call only)
  __wwhd_gdata_0x<ADDR>  game data at a fixed address
  anything else          a host service by name (wwhd_log, memcpy, ...), checked when the module loads

Errors (unsupported relocations or instructions, unknown symbols) are reported in plain words and
make the translation fail; nothing of the game is read or embedded.
"""
import argparse
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "recomp"))
from ppc2c import translate, Unhandled  # noqa: E402
from builds import canonical_build

TRANSLATOR_VERSION = "guestmod-0.2"

SHT_PROGBITS, SHT_SYMTAB, SHT_RELA, SHT_NOBITS = 1, 2, 4, 8
SHF_ALLOC, SHF_EXECINSTR = 2, 4
R_PPC_ADDR32, R_PPC_ADDR16_LO, R_PPC_ADDR16_HI, R_PPC_ADDR16_HA = 1, 4, 5, 6
R_PPC_REL24, R_PPC_REL14, R_PPC_REL32 = 10, 11, 26
REGION_START, REGION_END = 0x7F000000, 0x80000000


class ModError(Exception):
    pass


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


class Elf:
    def __init__(self, data):
        if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
            raise ModError("not a 32-bit big-endian ELF file (build the mod for powerpc-unknown-eabi)")
        (e_type, e_machine, _, _, _, e_shoff, _, _, _, _, e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from(
            ">HHIIIIIHHHHHH", data, 16)
        if e_machine != 20:
            raise ModError("not a PowerPC ELF file (machine %d)" % e_machine)
        if e_type != 1:
            raise ModError("the mod must be a relocatable ELF (link with ld.lld -r)")
        self.data = data
        self.sections = []
        for i in range(e_shnum):
            name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from(
                ">IIIIIIIIII", data, e_shoff + i * e_shentsize)
            self.sections.append(dict(idx=i, name_off=name, type=typ, flags=flags, off=off, size=size,
                                      link=link, info=info, align=max(align, 1), entsize=entsize))
        strtab = self.sections[e_shstrndx]
        for s in self.sections:
            s["name"] = self.cstr(strtab["off"] + s["name_off"])
        self.symbols = []
        for s in self.sections:
            if s["type"] == SHT_SYMTAB:
                names = self.sections[s["link"]]
                for k in range(s["size"] // 16):
                    nm, value, size, info, other, shndx = struct.unpack_from(">IIIBBH", data, s["off"] + 16 * k)
                    self.symbols.append(dict(name=self.cstr(names["off"] + nm), value=value, size=size,
                                             type=info & 15, bind=info >> 4, shndx=shndx))

    def cstr(self, off):
        end = self.data.index(b"\0", off)
        return self.data[off:end].decode("utf-8", "replace")

    def bytes_of(self, s):
        return self.data[s["off"]:s["off"] + s["size"]]


class Translator:
    def __init__(self, elf, base, build=None):
        self.elf = elf
        self.base = base
        self.build = build or canonical_build()
        if not self.build.canonical and not any(
                s["name"] == ".wwhd_addresses" and s["size"] > 0 and s["size"] % 4 == 0 and
                self.elf.bytes_of(s) == b"\0\0\0\1" * (s["size"] // 4)
                for s in elf.sections):
            raise ModError("Mod uses an older SDK with unrelocatable USA data addresses; "
                           "ask its author to rebuild with the European-compatible SDK")
        self.imm_override = {}
        self.imports = {}   # call/branch site -> ("game"|"orig"|"svc", value)
        self.services = []  # host service names, in order of first use
        self.layout()
        self.relocate()
        self.discover()

    # ---- layout: code first, then read-only data, data, bss
    def layout(self):
        alloc = [s for s in self.elf.sections if s["flags"] & SHF_ALLOC and s["size"]]
        rank = lambda s: (0 if s["flags"] & SHF_EXECINSTR else 2 if s["type"] == SHT_NOBITS else 1)
        addr = self.base
        self.sec_addr = {}
        for s in sorted(alloc, key=lambda s: (rank(s), s["idx"])):
            if s["align"] > 65536 or s["align"] & (s["align"] - 1):
                raise ModError("mod sections require power-of-two alignment no larger than 64 KiB")
            addr = (addr + s["align"] - 1) & ~(s["align"] - 1)
            self.sec_addr[s["idx"]] = addr
            addr += s["size"]
        self.end = (addr + 15) & ~15
        if self.end > REGION_END:
            raise ModError("the mod is too large for the guest mod region")
        self.image = bytearray(self.end - self.base)
        self.image_end = self.base
        for s in alloc:
            if s["type"] != SHT_NOBITS:
                a = self.sec_addr[s["idx"]] - self.base
                self.image[a:a + s["size"]] = self.elf.bytes_of(s)
                self.image_end = max(self.image_end, self.sec_addr[s["idx"]] + s["size"])
        self.text = [(self.sec_addr[s["idx"]], self.sec_addr[s["idx"]] + s["size"]) for s in alloc
                     if s["flags"] & SHF_EXECINSTR]

    def in_text(self, a):
        return any(lo <= a < hi for lo, hi in self.text)

    def word(self, a):
        return struct.unpack_from(">I", self.image, a - self.base)[0]

    def put32(self, a, v):
        struct.pack_into(">I", self.image, a - self.base, v & 0xFFFFFFFF)

    def put16(self, a, v):
        struct.pack_into(">H", self.image, a - self.base, v & 0xFFFF)

    # ---- symbols and relocations
    def game_address(self, kind, address):
        try:
            if kind == "gdata":
                return self.build.data(address)
            if not self.build.canonical and self.build.body_differs(address):
                raise ValueError("function differs between game builds")
            return self.build.code(address)
        except ValueError as exc:
            # Public declaration names are available in source checkouts and release SDKs.
            from pathlib import Path
            header = Path(HERE).parents[1] / "runtime/guest/include/game/functions.h"
            if not header.is_file():
                header = Path(HERE).parents[1] / "sdk/guest/include/game/functions.h"
            name = ""
            if header.is_file():
                match = re.search(r"^#define WWHD_ADDR_(\w+) 0x%08X$" % address,
                                  header.read_text(), re.M)
                if match:
                    name = match[1] + " "
            raise ModError("%s cannot use %s0x%08X on %s: %s" %
                           ("Mod", name, address, self.build.name, exc)) from exc

    def symbol_value(self, sym):
        """(kind, value): kind 'addr' for a guest address, else an import kind."""
        if sym["shndx"] == 0:  # undefined
            m = re.match(r"__wwhd_(game|orig|gdata)_(?:0x)?([0-9A-Fa-f]{1,8})$", sym["name"])
            if m:
                return m.group(1), int(m.group(2), 16)
            if not re.match(r"[A-Za-z_][A-Za-z0-9_]*$", sym["name"]):
                raise ModError("unknown symbol %r" % sym["name"])
            return "svc", sym["name"]
        if sym["shndx"] == 0xFFF1:  # absolute
            return "addr", sym["value"]
        if sym["shndx"] not in self.sec_addr:
            raise ModError("symbol %r is in a section that is not loaded" % sym["name"])
        return "addr", self.sec_addr[sym["shndx"]] + sym["value"]

    def relocate(self):
        self.code_refs = set()
        for rs in self.elf.sections:
            if rs["type"] != SHT_RELA or rs["info"] not in self.sec_addr:
                continue
            target = self.elf.sections[rs["info"]]
            tbase = self.sec_addr[rs["info"]]
            for k in range(rs["size"] // 12):
                off, info, addend = struct.unpack_from(">IIi", self.elf.data, rs["off"] + 12 * k)
                typ, sym = info & 0xFF, self.elf.symbols[info >> 8]
                p = tbase + off
                kind, val = self.symbol_value(sym)
                where = "%s+0x%X" % (target["name"], off)
                if kind in ("game", "orig", "gdata"):
                    val = self.game_address(kind, (val + addend) & 0xFFFFFFFF)
                    addend = 0
                if kind in ("orig", "svc") or (kind == "game" and typ in (R_PPC_REL24, R_PPC_REL14)):
                    if typ != R_PPC_REL24:
                        raise ModError("%s: %s can only be called, not used as a value (%s)" % (where, sym["name"], typ))
                    if kind == "svc" and val not in self.services:
                        self.services.append(val)
                    self.imports[p & ~3] = (kind, val)
                    continue
                s = (val + addend) & 0xFFFFFFFF
                if typ in (R_PPC_ADDR32, R_PPC_ADDR16_LO) and kind == "addr":
                    self.code_refs.add(s)
                if typ == R_PPC_ADDR32:
                    self.put32(p, s)
                elif typ == R_PPC_ADDR16_LO:
                    self.put16(p, s)
                elif typ == R_PPC_ADDR16_HI:
                    self.put16(p, s >> 16)
                elif typ == R_PPC_ADDR16_HA:
                    self.put16(p, (s + 0x8000) >> 16)
                elif typ == R_PPC_REL24:
                    d = (s - p) & 0xFFFFFFFF
                    if sext(d, 32) != sext(d & 0x03FFFFFC, 26):
                        raise ModError("%s: branch out of range" % where)
                    self.put32(p, (self.word(p) & ~0x03FFFFFC) | (d & 0x03FFFFFC))
                elif typ == R_PPC_REL14:
                    d = (s - p) & 0xFFFFFFFF
                    self.put32(p, (self.word(p) & ~0xFFFC) | (d & 0xFFFC))
                elif typ == R_PPC_REL32:
                    self.put32(p, s - p)
                else:
                    raise ModError("%s: unsupported relocation type %d against %s (small data? build with "
                                   "the SDK's flags)" % (where, typ, sym["name"]))
        hs = [s for s in self.elf.sections if s["name"] == ".wwhd_hooks"]
        self.hooks = []
        for s in hs:
            a = self.sec_addr.get(s["idx"])
            for k in range(s["size"] // 16):
                kind, target, func, flags = struct.unpack_from(">IIII", self.image, a - self.base + 16 * k)
                if kind not in (1, 2, 3):
                    raise ModError("bad hook descriptor %d (kind %d)" % (k, kind))
                if not self.in_text(func):
                    raise ModError("hook descriptor %d does not point to mod code" % k)
                self.hooks.append((kind, self.game_address("game", target), func, flags))

    # ---- functions: symbols, call targets, address-taken code, tail-call targets
    def discover(self):
        entries = set()
        for sym in self.elf.symbols:
            if sym["type"] == 2 and sym["shndx"] in self.sec_addr:
                entries.add(self.sec_addr[sym["shndx"]] + sym["value"])
        entries |= {lo for lo, hi in self.text}
        entries |= {h[2] for h in self.hooks}
        for lo, hi in self.text:
            for a in range(lo, hi, 4):
                w = self.word(a)
                if a in self.imports:
                    continue
                if (w >> 26) == 18 and (w & 1):
                    t = (sext(w & 0x03FFFFFC, 26) + (0 if w & 2 else a)) & 0xFFFFFFFF
                    if self.in_text(t):
                        entries.add(t)
        # address-taken code (function pointers in data or built with lis/addi)
        entries |= {v for v in self.code_refs if self.in_text(v)}
        while True:
            se = sorted(entries)
            new = set()
            for lo, hi in self.text:
                for a in range(lo, hi, 4):
                    if a in self.imports:
                        continue
                    t = self.branch_target(a, self.word(a))
                    if t is not None and self.in_text(t) and t not in entries and self.func_of(se, t) != self.func_of(se, a):
                        new.add(t)
            if not new:
                break
            entries |= new
        self.entries = sorted(entries)

    @staticmethod
    def branch_target(addr, w):
        op = w >> 26
        if op == 18 and not (w & 1):
            return (sext(w & 0x03FFFFFC, 26) + (0 if w & 2 else addr)) & 0xFFFFFFFF
        if op == 16 and not (w & 1):
            return (sext(w & 0xFFFC, 16) + (0 if w & 2 else addr)) & 0xFFFFFFFF
        return None

    @staticmethod
    def func_of(se, a):
        import bisect
        i = bisect.bisect_right(se, a) - 1
        return se[i] if i >= 0 else None

    def func_end(self, start):
        hi = next(h for l, h in self.text if l <= start < h)
        later = [e for e in self.entries if start < e < hi]
        return min(later) if later else hi

    # ---- ppc2c callbacks
    def import_code(self, addr, tail):
        kind, val = self.imports[addr]
        if kind == "game":
            return ("c->pc = 0x%08Xu; MUSTTAIL return ppc_dispatch(c);" if tail else "c->pc = 0x%08Xu; ppc_dispatch(c);") % val
        if kind == "orig":
            return ("g_host->call_original(c, 0x%08Xu); return;" if tail else "g_host->call_original(c, 0x%08Xu);") % val
        fn = "svc_%d" % self.services.index(val)
        return "c->pc = 0x%08Xu; " % addr + (("MUSTTAIL return %s(c);" if tail else "%s(c);") % fn)

    def branch(self, addr, tgt):
        if addr in self.imports:
            return self.import_code(addr, True)
        if self.cur_start <= tgt < self.cur_end:
            self.labels.add(tgt)
            if tgt <= addr:
                return "PPC_LOOP(); goto L_%08X;" % tgt
            return "goto L_%08X;" % tgt
        if tgt in self.entries:
            return "MUSTTAIL return mf_%08X(c);" % tgt
        return "c->pc = 0x%08Xu; MUSTTAIL return ppc_dispatch(c);" % tgt

    def call(self, addr, tgt):
        if addr in self.imports:
            return self.import_code(addr, False)
        if tgt in self.entries:
            return "mf_%08X(c);" % tgt
        return "c->pc = 0x%08Xu; ppc_dispatch(c);" % tgt

    def ret(self):
        return "return;"

    def indirect_jump(self, addr):
        return "c->pc = c->ctr; MUSTTAIL return ppc_dispatch(c);"

    # ---- output
    def emit(self, mod_id):
        # only numbers and checked identifiers reach the C source; the id is reduced to safe characters
        out = [PRELUDE % {"version": TRANSLATOR_VERSION, "id": re.sub(r"[^A-Za-z0-9._-]", "_", mod_id)[:64]}]
        for i, name in enumerate(self.services):
            out.append("static PpcFunc svc_%d; /* %s */" % (i, name))
        for e in self.entries:
            out.append("static void mf_%08X(Cpu* __restrict c);" % e)
        problems = []
        for e in self.entries:
            self.cur_start, self.cur_end = e, self.func_end(e)
            self.labels = set()
            body = []
            for a in range(e, self.cur_end, 4):
                w = self.word(a)
                try:
                    s = translate(a, w, self)
                except Unhandled as ex:
                    problems.append("%08X: %08X (%s)" % (a, w, ex))
                    s = "ppc_unimplemented(c, 0x%08Xu, 0x%08Xu);" % (a, w)
                body.append((a, w, s))
            out.append("static void mf_%08X(Cpu* __restrict c) {" % e)
            out.append("    PPC_ENTER(0x%08Xu);" % e)
            for a, w, s in body:
                if a in self.labels:
                    out.append("L_%08X: ;" % a)
                out.append("    %s /* %08X: %08X */" % (s, a, w))
            nxt = self.cur_end
            if nxt in self.entries:
                out.append("    MUSTTAIL return mf_%08X(c);" % nxt)
            else:
                out.append("    ppc_unimplemented(c, 0x%08Xu, 0); /* fell off the end of the code */" % nxt)
            out.append("}")
        if problems:
            raise ModError("instructions the translator does not support:\n  " + "\n  ".join(problems[:20]))
        img = bytes(self.image[:self.image_end - self.base])
        out.append("static const uint8_t k_image[%d] = {" % max(1, len(img)))
        for i in range(0, len(img), 24):
            out.append("    " + ",".join("%d" % b for b in img[i:i + 24]) + ",")
        out.append("};")
        out.append("static const WWHDGuestFunc k_funcs[] = {")
        for e in self.entries:
            out.append("    {0x%08Xu, mf_%08X}," % (e, e))
        out.append("};")
        out.append("static const WWHDGuestHook k_hooks[%d] = {" % max(1, len(self.hooks)))
        for kind, target, func, flags in self.hooks:
            out.append("    {%d, 0x%08Xu, 0x%08Xu, %d, mf_%08X}," % (kind, target, func, flags, func))
        out.append("};")
        out.append("static const char* const k_services[] = {%s};" % ", ".join(
            ['"%s"' % s for s in self.services] + ["0"]))
        out.append(EPILOGUE % {"version": TRANSLATOR_VERSION, "base": self.base, "size": self.end - self.base,
                                "image_size": len(img), "nfuncs": len(self.entries), "nhooks": len(self.hooks),
                                "nsvc": len(self.services),
                                "svc_assign": "\n".join("    svc_%d = svcs[%d];" % (i, i) for i in range(len(self.services)))})
        return "\n".join(out) + "\n"


PRELUDE = r"""/* Generated by tools/guestmod/guestmod.py (%(version)s) from guest mod "%(id)s". Do not edit. */
#include "wwhd_guest_abi.h"

#if defined(_WIN32)
#define WWHD_MODULE_EXPORT __declspec(dllexport)
#define WWHD_MODULE_LOCAL
#else
#define WWHD_MODULE_EXPORT __attribute__((visibility("default")))
#define WWHD_MODULE_LOCAL __attribute__((visibility("hidden")))
#endif

/* the module imports nothing from the executable: runtime entry points come from the host table */
static const WWHDGuestHostV1* g_host;
WWHD_MODULE_LOCAL void ppc_dispatch(Cpu* c) { MUSTTAIL return g_host->dispatch(c); }
WWHD_MODULE_LOCAL void ppc_unimplemented(Cpu* c, uint32_t a, uint32_t i) { g_host->unimplemented(c, a, i); }
WWHD_MODULE_LOCAL void ppc_trap(Cpu* c, uint32_t a) { g_host->trap(c, a); }
WWHD_MODULE_LOCAL uint64_t ppc_timebase(void) { return g_host->timebase(); }
WWHD_MODULE_LOCAL double ppc_fres(double x) { return g_host->fres(x); }
WWHD_MODULE_LOCAL double ppc_frsqrte(double x) { return g_host->frsqrte(x); }
#undef PPC_ENTER
#define PPC_ENTER(a) do { if (__builtin_expect(g_host->core_preempt[c->core], 0)) g_host->preempt(c); } while (0)
"""

EPILOGUE = r"""
static const WWHDGuestModuleV1 k_module = {
    sizeof(WWHDGuestModuleV1), WWHD_GUEST_ABI_VERSION, "%(version)s",
    0x%(base)08Xu, 0x%(size)Xu, k_image, %(image_size)du,
    k_funcs, %(nfuncs)du, k_hooks, %(nhooks)du,
};

WWHD_MODULE_EXPORT const WWHDGuestModuleV1* wwhd_guest_module_v1(const WWHDGuestHostV1* host) {
    if (!host || host->size < sizeof(WWHDGuestHostV1) || host->abi_version != WWHD_GUEST_ABI_VERSION) return 0;
    PpcFunc svcs[%(nsvc)d + 1];
    for (int i = 0; i < %(nsvc)d; i++) {
        svcs[i] = host->service(k_services[i]);
        if (!svcs[i]) return 0; /* a host service this runtime does not provide */
    }
    g_host = host;
%(svc_assign)s
    return &k_module;
}
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("out")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=REGION_START)
    ap.add_argument("--id", default="mod")
    ap.add_argument("--report")
    a = ap.parse_args()
    if not (REGION_START <= a.base < REGION_END) or a.base % 0x1000:
        sys.exit("error: --base must be a 4 KiB aligned address in %08X-%08X" % (REGION_START, REGION_END))
    try:
        t = Translator(Elf(open(a.elf, "rb").read()), a.base)
        src = t.emit(a.id)
    except ModError as e:
        sys.exit("error: %s" % e)
    with open(a.out, "w") as f:
        f.write(src)
    rep = {"translator": TRANSLATOR_VERSION, "base": a.base, "size": t.end - a.base, "functions": len(t.entries),
           "hooks": [{"kind": {1: "replace", 2: "entry", 3: "return"}[k], "target": "%08X" % tg} for k, tg, _, _ in t.hooks],
           "services": t.services,
           "game_calls": sorted({"%08X" % v for k, v in t.imports.values() if k in ("game", "orig")})}
    if a.report:
        with open(a.report, "w") as f:
            json.dump(rep, f, indent=1)
    print(json.dumps(rep))


if __name__ == "__main__":
    main()
