# SPDX-License-Identifier: MPL-2.0
"""Guard the shipped EUR map and region-aware runtime address uses.

This is a source audit, not a C++ parser or proof of matching field layouts.
Keep non-address exceptions narrow and documented; new game literals must use
GC/GD and have a shipped mapping.
"""
from pathlib import Path
import re
import unittest

import builds

ROOT = Path(__file__).resolve().parents[2]
NON_ADDRESS = {
    ("runtime.h", 0x02800000): "foreground bucket size",
    ("runtime.h", 0x02000000): "MEM1 size",
    ("core.cpp", 0x02000000): "executable memory lower bound",
    ("gfx/metal_main.mm", 0x02000000): "executable memory lower bound",
    ("hle/system_stubs.cpp", 0x02000000): "executable memory lower bound",
    ("mods/mod_archive.cpp", 0x02014B50): "ZIP central directory signature",
}
NON_ADDRESS.update({("espresso_fp.c", value): "frsqrte mantissa lookup table"
                    for value in (0x02FB7000, 0x02D26000, 0x02AC0000, 0x02881000,
                                  0x02665000, 0x02468000, 0x02287000, 0x020C1000)})
TRIVIA = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
LITERAL = re.compile(r'\b0[xX]([0-9a-fA-F]{1,8})(?![0-9a-fA-F])[uUlL]*\b')
WRAPPED = re.compile(r'\b(GC|GD)\s*\([^;]*?\)', re.S)
FIXED_GAME_ALLOCATIONS = {0x145AC92C, 0x3BB9DE00, 0x44BDF900, 0x44BDFD00}



def address_errors(source, name, mapping):
    text = TRIVIA.sub(lambda m: re.sub(r"[^\n]", " ", m[0]), source)
    spans = [(m.start(), m.end(), "code" if m[1] == "GC" else "data")
             for m in WRAPPED.finditer(text)]
    errors = []
    for m in LITERAL.finditer(text):
        a = int(m[1], 16)
        if a in FIXED_GAME_ALLOCATIONS:
            line = text.count("\n", 0, m.start()) + 1
            errors.append(f"{name}:{line}: fixed game allocation; use a mapped global or program identity")
            continue
        kind = "code" if 0x02000000 <= a < 0x03000000 else "data" if 0x10000000 <= a < 0x10500000 else None
        # MEM2's base is a VM boundary throughout the runtime, never a global.
        if kind is None or a == 0x10000000 or (name, a) in NON_ADDRESS:
            continue
        line = text.count("\n", 0, m.start()) + 1
        if not any(lo <= m.start() < hi and wrapper == kind for lo, hi, wrapper in spans):
            errors.append(f"{name}:{line}: {a:08X} bypasses GC/GD")
        if not valid_address(mapping, kind, a):
            errors.append(f"{name}:{line}: {a:08X} has no EUR mapping")
        # Check the full address for declarations such as kPlay + field offset.
        tail = re.match(r'\s*\+\s*(0x[0-9a-fA-F]+)', text[m.end():])
        if tail and not valid_address(mapping, kind, a + int(tail[1], 16)):
            errors.append(f"{name}:{line}: derived address has no EUR mapping")
    return errors


def valid_address(mapping, kind, address):
    try:
        getattr(mapping, kind)(address)
        return True
    except ValueError:
        return False


class CoverageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mapping = builds.by_name("EU")

    def test_every_active_hook_is_mapped(self):
        entries, _ = builds.read_hooks(builds.hook_files(), self.mapping)
        for site, canon, native, where in entries:
            self.assertTrue(valid_address(self.mapping, "code", canon), where)
            if site:
                self.assertFalse(self.mapping.body_differs(canon), where)
        for address in (0x02715310, 0x02574144):
            self.assertTrue(any(canon == address for _, canon, _, _ in entries))
            self.assertTrue(valid_address(self.mapping, "code", address))

    def test_runtime_game_literals_are_wrapped_and_mapped(self):
        errors = []
        root = ROOT / "runtime/src"
        for path in sorted(root.rglob("*")):
            if path.suffix not in (".cpp", ".h", ".c", ".mm"):
                continue
            name = path.relative_to(root).as_posix()
            errors.extend(address_errors(path.read_text(encoding="utf-8"), name, self.mapping))
        self.assertEqual(errors, [], "\n".join(errors))

    def test_portable_play_fields_keep_their_offsets(self):
        # The cutscene/rope gates and boat restoration derive addresses from kPlay.
        # Mapping only its base is valid only while all those fields share its shift.
        source = (ROOT / "runtime/src/savestate.cpp").read_text(encoding="utf-8")
        base = int(re.search(r'kPlay\s*=\s*GD\(0x([0-9a-fA-F]+)\)', source)[1], 16)
        offsets = {int(m[1], 16) for m in re.finditer(r'\bkPlay\s*\+\s*0x([0-9a-fA-F]+)', source)}
        self.assertGreaterEqual(len(offsets), 10)
        for offset in offsets:
            self.assertEqual(self.mapping.data(base + offset), self.mapping.data(base) + offset,
                             f"portable-state field +{offset:X} crosses a data shift")

    def test_guard_rejects_raw_and_unmapped_addresses(self):
        self.assertTrue(address_errors("ld32(0x101F84DC);", "new.cpp", self.mapping))
        self.assertTrue(address_errors("GD(0x027200A0)", "new.cpp", self.mapping))
        self.assertTrue(address_errors("GC(0x2593B18);", "new.cpp", self.mapping))
        self.assertTrue(address_errors("GC(0x02593B18);", "new.cpp", self.mapping))
        self.assertTrue(address_errors("GD(0x104FFFF0);", "new.cpp", self.mapping))
        self.assertTrue(address_errors("ld32(0x145AC92C);", "new.cpp", self.mapping))
        self.assertTrue(address_errors("shader == 0x44BDFD00;", "new.cpp", self.mapping))
        self.assertFalse(address_errors("const uint32_t p = GD(0x101F84DC);", "new.cpp", self.mapping))
        self.assertFalse(address_errors("GC(0x027200A0)", "new.cpp", self.mapping))


if __name__ == "__main__":
    unittest.main()
