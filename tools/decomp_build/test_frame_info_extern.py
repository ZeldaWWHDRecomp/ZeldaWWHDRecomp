#!/usr/bin/env python3
"""gmem_frame_info in the game units (#678): frames_vis.h (pre-included into every unit) sets GABI_FRAME_INFO_EXTERN, so
the decompilation's gabi.h only declares gmem_frame_info and the runtime's definition (runtime/src/decomp/frames.cpp)
is the only one. Before, every unit had gabi.h's weak null default; on Windows (COFF) each weak definition also defines
`.weak.gmem_frame_info.default.<suffix>`, with the same suffix in every unit (`.refptr.g_decomp_call_tab`), and the
decomp exe's link failed with 29 duplicate symbols.

With the decompilation of the submodule (decomp/), on stub units with the game units' options and the host compiler
(c++): a unit references gmem_frame_info and defines none (no weak default, nothing a COFF link could duplicate); two
units linked with a program that defines it (as the runtime does) both reach that definition; without frames_vis.h
(the decompilation's own builds) gabi.h still has its weak default. The COFF link itself (llvm-mingw) is not run here."""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
INC = [os.path.join(ROOT, "runtime", "include"), os.path.join(ROOT, "decomp", "tools", "verify", "include")]
# the game units' options that matter here (cmake/Decomp.cmake wwhd_decomp_link; fast mode's table lookup)
UNIT_FLAGS = ["-std=c++20", "-O2", "-g0", "-w", "-fvisibility=hidden", "-fvisibility-inlines-hidden", "-ffp-contract=off",
              "-fno-fast-math", "-fwrapv", "-fno-strict-aliasing", "-DGABI_GAME_BUILD=1", "-DGABI_FAST=1", "-DGABI_FAST_NE=1",
              "-DGABI_SHARED_TLS=1", "-DGABI_SLIM_SCOPE=1", "-DGABI_LEAN_ENTRY=1", "-DGABI_NATIVE_ENTRIES=1"]
FRAMES_VIS = ["-include", os.path.join(HERE, "frames_vis.h")]

# a stand-in game unit: a guest call's table lookup (g_decomp_call_tab, as in the units of the report) and the
# automatic frame lookup
UNIT = r"""#include "gabi.h"
#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)
extern "C" __attribute__((visibility("default"))) unsigned CAT(unit_entry, UNIT)(unsigned a) {
    const GabiPtrEntry* e = gabi_ptr_find<true>(a);
    const GabiFrameInfo* fi = gmem_frame_info(a);
    return (e ? e->sig : 0u) + (fi ? fi->size : 0u) + UNIT;
}
"""
# the runtime's part: the tables and the one gmem_frame_info
MAIN = r"""#include <cstdint>
#include <cstdio>
#include "native_frames.h"
#include "ppc.h"
void ppc_trap(Cpu*, uint32_t) {}  // (gabi.h's gmem_trap default needs it; nothing here traps)
struct GabiPtrEntry { uint32_t addr, sig; void* fn; };
extern "C" const GabiPtrEntry* g_decomp_call_tab = nullptr;
extern "C" const GabiPtrEntry* g_decomp_ptr_tab = nullptr;
extern "C" uint32_t g_decomp_ptr_mask = 0;
static const GabiFrameInfo kFrame = {0x40, 0, nullptr, 0, 0};
extern "C" const GabiFrameInfo* gmem_frame_info(uint32_t addr) { return addr ? &kFrame : nullptr; }
extern "C" unsigned unit_entry0(unsigned);
extern "C" unsigned unit_entry1(unsigned);
int main() {
    unsigned a = unit_entry0(0x02000000u), b = unit_entry1(0x02000000u);
    std::printf("%u %u\n", a, b);
    return a == 0x40 && b == 0x41 ? 0 : 1;
}
"""
# a weak external, defined or not, of gmem_frame_info: nm's letter (Mach-O names have a leading underscore)
SYM = re.compile(r"^\s*(?:[0-9a-fA-F]+\s+)?([A-Za-z])\s+_?gmem_frame_info$", re.M)


@unittest.skipUnless(os.path.isfile(os.path.join(INC[1], "gabi.h")), "no decomp submodule")
@unittest.skipUnless(shutil.which("c++") and shutil.which("nm"), "no c++ / nm")
@unittest.skipIf(os.name == "nt", "the host compiler of the Mac and Linux builds")
class FrameInfoExtern(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        with open(os.path.join(self.dir, "unit.cpp"), "w") as f:
            f.write(UNIT)

    def tearDown(self):
        self.tmp.cleanup()

    def compile_unit(self, unit, extra):
        o = os.path.join(self.dir, "u%d%s.o" % (unit, "v" if extra else ""))
        r = subprocess.run(["c++"] + UNIT_FLAGS + extra + ["-I" + d for d in INC] + ["-DUNIT=%d" % unit, "-c",
                           os.path.join(self.dir, "unit.cpp"), "-o", o], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-3000:])
        return o

    def letters(self, obj):
        r = subprocess.run(["nm", obj], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return SYM.findall(r.stdout)

    def test_units_define_none(self):
        """with frames_vis.h a unit only references gmem_frame_info"""
        self.assertEqual(self.letters(self.compile_unit(0, FRAMES_VIS)), ["U"],
                         "a game unit must not define gmem_frame_info (decomp/tools/verify/include/gabi.h "
                         "GABI_FRAME_INFO_EXTERN; the decompilation pin may predate it)")

    def test_units_reach_the_runtime_definition(self):
        """two units linked with the runtime's stand-in both get its frame"""
        objs = [self.compile_unit(u, FRAMES_VIS) for u in (0, 1)]
        main = os.path.join(self.dir, "main.cpp")
        with open(main, "w") as f:
            f.write(MAIN)
        exe = os.path.join(self.dir, "stub")
        r = subprocess.run(["c++", "-std=c++20", "-O2"] + ["-I" + d for d in INC] + [main] + objs + ["-o", exe],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-3000:])
        p = subprocess.run([exe], capture_output=True, text=True)
        self.assertEqual((p.returncode, p.stdout.strip()), (0, "%d %d" % (0x40, 0x41)))

    def test_decompilation_default_kept(self):
        """without frames_vis.h (the decompilation's own game-test builds) gabi.h keeps its weak null default"""
        letters = self.letters(self.compile_unit(0, []))
        self.assertTrue(letters and all(c in "WwVvTt" for c in letters), letters)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]] + sys.argv[1:])
