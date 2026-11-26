#!/usr/bin/env python3
"""The capstone stand-in (capstone_shim/, used on Android for decl_lint.py's disassembly) against capstone 5:
known words always; the whole USA cking.rpx .text when WWHD_TEST_RPX names it and the real capstone is importable
(WWHD_TEST_CAPSTONE: the folder with the real capstone package)."""
import importlib.util
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))


def load_shim():
    spec = importlib.util.spec_from_file_location("capstone_shim", os.path.join(HERE, "capstone_shim", "capstone", "__init__.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


# (word, address, mnemonic, op_str) as capstone 5.0.7 prints them (from the USA game)
KNOWN = [("9421fff0", 0x2000024, "stwu", "r1, -0x10(r1)"), ("7c7f1b79", 0, "or.", "r31, r3, r3"),
         ("40820014", 0x2000038, "bne", "0x200004c"), ("40860024", 0x20345c4, "bne", "cr1, 0x20345e8"),
         ("3860005c", 0, "li", "r3, 0x5c"), ("3fe0ff00", 0, "lis", "r31, -0x100"), ("7fe3fb78", 0, "mr", "r3, r31"),
         ("4e800020", 0, "blr", ""), ("4d820020", 0, "beqlr", ""), ("57c007ff", 0, "clrlwi.", "r0, r30, 0x1f"),
         ("558a0fff", 0, "rlwinm.", "r10, r12, 1, 0x1f, 0x1f"), ("554c003e", 0, "slwi", "r12, r10, 0"),
         ("550a803e", 0, "rotlwi", "r10, r8, 0x10"), ("5403d97e", 0, "srwi", "r3, r0, 5"),
         ("4cc63182", 0, "crclr", "cr1eq"), ("7c0057ae", 0, "stfiwx", "f0, 0, r10"), ("7c1e60ac", 0, "dcbf", "r30, r12"),
         ("7c0037ec", 0, "dcbz", "0, r6"), ("e4e48008", 0, "psq_lu", "f7, 8(r4), 1, 0"),
         ("11200014", 0, "ps_sum0", "f9, f0, f0, f0"), ("13fffca0", 0, "ps_merge10", "f31, f31, f31"),
         ("10073040", 0, "ps_cmpo0", "cr0, f7, f6"), ("7d62e3a6", 0, "mtspr", "0x382, r11"),
         ("7ffff808", 0, "twu", "r31, r31"), ("00000000", 0, ".byte", "0x00, 0x00, 0x00, 0x00"),
         ("42000000", 0x2001498, "bdnz", "0x2001498"), ("60000000", 0, "nop", "")]


class ShimTests(unittest.TestCase):
    def test_known_words(self):
        cs = load_shim()
        md = cs.Cs(cs.CS_ARCH_PPC, cs.CS_MODE_32 | cs.CS_MODE_BIG_ENDIAN | cs.CS_MODE_PS)
        md.skipdata = True
        for word, addr, m, o in KNOWN:
            i = next(md.disasm(bytes.fromhex(word), addr))
            self.assertEqual((i.mnemonic, i.op_str), (m, o), word)

    @unittest.skipUnless(os.environ.get("WWHD_TEST_RPX") and os.environ.get("WWHD_TEST_CAPSTONE"),
                         "WWHD_TEST_RPX / WWHD_TEST_CAPSTONE not set")
    def test_whole_game_text(self):
        sys.path.insert(0, os.path.join(HERE, ".."))
        from rpx import Rpx
        t = Rpx(os.environ["WWHD_TEST_RPX"]).by_name[".text"]
        sys.path.insert(0, os.environ["WWHD_TEST_CAPSTONE"])
        import capstone as real
        self.assertNotIn("shim", real.__version__)
        cs = load_shim()
        a = real.Cs(real.CS_ARCH_PPC, real.CS_MODE_32 | real.CS_MODE_BIG_ENDIAN | real.CS_MODE_PS)
        b = cs.Cs(cs.CS_ARCH_PPC, cs.CS_MODE_32 | cs.CS_MODE_BIG_ENDIAN | cs.CS_MODE_PS)
        a.skipdata = b.skipdata = True
        n = 0
        for x, y in zip(a.disasm(t.data, t.addr), b.disasm(t.data, t.addr)):
            self.assertEqual((x.address, x.mnemonic, x.op_str), (y.address, y.mnemonic, y.op_str))
            n += 1
        self.assertEqual(n, len(t.data) // 4)


if __name__ == "__main__":
    unittest.main()
