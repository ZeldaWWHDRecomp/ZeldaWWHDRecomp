# SPDX-License-Identifier: MPL-2.0
"""Run generated PPC callers whose assembly helpers return scratch registers, without game data."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import collections
import os
import shutil
import subprocess
import tempfile
import unittest

import builds
import recomp

ROOT = Path(__file__).resolve().parents[2]
CC = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")


def compiler(preserve, nested=False):
    # Own instructions: blr; [bl noop;] lis r12,0x1234; li r0,7;
    # fmr f0,f2; cmpwi r3,0; blr. The caller consumes r12 after bl helper.
    helper = ([0x4BFFFFFD] if nested else []) + [0x3D801234, 0x38000007, 0xFC001090, 0x2C030000, 0x4E800020]
    caller = 0x1004 + 4*len(helper)
    words = {0x1000: 0x4E800020}
    words.update({0x1004+4*i: w for i, w in enumerate(helper)})
    call = (18 << 26) | ((0x1004-caller) & 0x3FFFFFC) | 1
    words.update({caller: call, caller+4: 0x386C0009, caller+8: 0x4E800020})
    c = recomp.Recompiler.__new__(recomp.Recompiler)
    c.build = builds.canonical_build()
    c.mod_hooks = False
    c.preserve_registers = preserve
    c.p = SimpleNamespace(text_hi=caller+12, word=words.__getitem__, jump_tables={}, import_calls={}, undef_calls=set())
    c.entries = set((0x1000, 0x1004, caller))
    c.sorted_entries = sorted(c.entries)
    c.hooks, c.sites, c.imm_override, c.canon_of = set(), set(), {}, {}
    c.cr_stats, c.leaf_count, c.nonleaf_count = [0, 0], 0, 0
    c.unhandled, c.used_imports = collections.Counter(), set()
    return c, caller


class PreserveRegisters(unittest.TestCase):
    def test_default_keeps_abi_optimizations(self):
        c, _ = compiler(False)
        with patch.object(recomp, "CRLIVE", True), patch.object(recomp, "LEAF", True):
            c.emit_function(0x1004)
        self.assertEqual(c.leaf_count, 1)
        self.assertGreater(c.cr_stats[0], 0)

    @unittest.skipUnless(CC, "no C compiler")
    def test_scratch_returns_require_preservation_for_leaf_and_nonleaf_helpers(self):
        for preserve, nested in ((True, False), (True, True), (False, False), (False, True)):
            c, caller = compiler(preserve, nested)
            with patch.object(recomp, "CRLIVE", True), patch.object(recomp, "LEAF", True), patch.object(recomp, "NONLEAF", True):
                functions = [c.emit_function(a)[0] for a in c.sorted_entries]
            if preserve:
                self.assertEqual(c.leaf_count, 0)
                self.assertEqual(c.nonleaf_count, 0)
                self.assertEqual(c.cr_stats, [0, 0])
            self.assertFalse(c.unhandled)
            source = '#include "ppc.h"\n#include <stdlib.h>\n'
            source += '\n'.join('void f_%08X(Cpu*);' % a for a in c.sorted_entries)
            source += '''
int g_ppc_trace;
volatile int g_core_preempt[3];
void ppc_trace_enter(uint32_t a) { (void)a; }
void ppc_preempt(Cpu* c) { (void)c; }
void ppc_unimplemented(Cpu* c,uint32_t a,uint32_t w) { (void)c;(void)a;(void)w;abort(); }
'''
            source += '\n'.join(functions)
            source += '''
int main(void) {
    Cpu c={0}; c.f[2].ps0=2.5;
    f_%08X(&c);
    if(c.r[3]!=0x12340009u || c.r[12]!=0x12340000u || c.r[0]!=7u) return 1;
    if(c.f[0].ps0!=2.5 || !c.cr[2]) return 2;
    return 0;
}
''' % caller
            with tempfile.TemporaryDirectory() as tmp:
                src = Path(tmp)/'scratch.c'
                src.write_text(source)
                exe = Path(tmp)/('scratch.exe' if os.name == 'nt' else 'scratch')
                for opt in ('-O0', '-O2'):
                    with self.subTest(preserve=preserve, nested=nested, opt=opt):
                        subprocess.run([CC, '-std=c11', opt, '-w', '-I'+str(ROOT/'runtime/include'), str(src), '-lm', '-o', str(exe)], check=True, capture_output=True)
                        result = subprocess.run([str(exe)], capture_output=True)
                        # Identical assembly must demonstrate the original ABI-optimization bug
                        # when preservation is disabled, not merely pass with the fix enabled.
                        self.assertEqual(result.returncode, 0 if preserve else 1, result.stderr)


if __name__ == '__main__':
    unittest.main()
