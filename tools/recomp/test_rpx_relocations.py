# SPDX-License-Identifier: MPL-2.0
"""The loader's view of an RPX: relocations to the module's own symbols applied (issue #383).

TWWHD Randomizer 1.2.0 redirects function-table entries by editing only relocation addends; the
section bytes keep the vanilla pointers. A synthetic module (no game data) has the same shape: a
table whose bytes point at a stub while its relocation points at an appended function.
"""
from pathlib import Path
import struct, tempfile, unittest
import sys

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent)]
from rpx import Rpx, R_PPC_ADDR32, R_PPC_ADDR16_LO, R_PPC_ADDR16_HA, R_PPC_ADDR16_HI, R_PPC_REL24
import analyze

TEXT, DATA, IMPORT = 0x02000000, 0x10000000, 0xC0000000
BLR = 0x4E800020


def build_rpx(path, table, relocs, text_words):
    """sections: null, .text, .data, .fimport_x, .symtab, .strtab, .rela.data, .rela.text, .shstrtab.
    Symbols: 0 null, 1 $TEXT (section 1, TEXT), 2 $DATA (section 2, DATA), 3 import (section 3), 4 undefined."""
    names = [b'', b'.text', b'.data', b'.fimport_x', b'.symtab', b'.strtab', b'.rela.data', b'.rela.text', b'.shstrtab']
    shstr = b'\0'.join(names) + b'\0'
    name_off = [shstr.index(n + b'\0') if n else 0 for n in names]
    strtab = b'\0$TEXT\0$DATA\0imp\0undef\0'
    sym = lambda name, value, info, shndx: struct.pack('>IIIBBH', strtab.index(name + b'\0') if name else 0, value, 0, info, 0, shndx)
    symtab = sym(b'', 0, 0, 0) + sym(b'$TEXT', TEXT, 3, 1) + sym(b'$DATA', DATA, 3, 2) + \
        sym(b'imp', IMPORT, 0x12, 3) + sym(b'undef', 0, 0x12, 0)
    rela = {'data': b'', 'text': b''}
    for where, off, typ, symbol, addend in relocs:
        rela[where] += struct.pack('>IIi', off, symbol << 8 | typ, addend)
    text = struct.pack('>%dI' % len(text_words), *text_words)
    data = struct.pack('>%dI' % len(table), *table)
    # (type, flags, addr, payload, link, info)
    sections = [(0, 0, 0, b'', 0, 0), (1, 6, TEXT, text, 0, 0), (1, 3, DATA, data, 0, 0),
                (0x80000002, 2, IMPORT, b'\0' * 8, 0, 0), (2, 0, 0, symtab, 5, 0), (3, 0, 0, strtab, 0, 0),
                (4, 0, 0, rela['data'], 4, 2), (4, 0, 0, rela['text'], 4, 1), (3, 0, 0, shstr, 0, 0)]
    header = bytearray(0x34)
    header[:6] = b'\x7fELF\x01\x02'; header[18:20] = b'\x00\x14'
    struct.pack_into('>I', header, 0x18, TEXT)
    struct.pack_into('>I', header, 0x20, 0x34)
    struct.pack_into('>HHH', header, 0x2E, 40, len(sections), len(sections) - 1)
    offset = 0x34 + 40 * len(sections); entries = b''; body = b''
    for i, (typ, flags, addr, payload, link, info) in enumerate(sections):
        entries += struct.pack('>10I', name_off[i], typ, flags, addr, offset, len(payload), link, info, 4, 0)
        offset += len(payload); body += payload
    Path(path).write_bytes(bytes(header) + entries + body)


class RpxRelocations(unittest.TestCase):
    # text: 0 entry (blr), 4 stub (blr, referenced only by the table), 8 appended (blr),
    # 12/16 lis/addi pair whose LO relocation was redirected, 20 a word with an import relocation
    STUB, NEW = TEXT + 4, TEXT + 8
    WORDS = [BLR, BLR, BLR, 0x3C601000, 0x38630000, 0x3C600000]

    def make(self, tmp, redirected):
        target = self.NEW if redirected else self.STUB
        lo = 0x0040 if redirected else 0x0000
        relocs = [('data', DATA, R_PPC_ADDR32, 1, target - TEXT),          # table[0]: the item function
                  ('data', DATA + 4, R_PPC_ADDR32, 1, self.STUB - TEXT),   # table[1]: unchanged
                  ('data', DATA + 8, R_PPC_ADDR32, 3, 0),                  # import: left alone
                  ('data', DATA + 12, R_PPC_ADDR32, 4, 0x1234),            # undefined: left alone
                  ('text', TEXT + 14, R_PPC_ADDR16_HA, 2, lo),
                  ('text', TEXT + 18, R_PPC_ADDR16_LO, 2, lo),
                  ('text', TEXT + 22, R_PPC_ADDR16_HI, 3, 0),
                  ('text', TEXT + 20, R_PPC_REL24, 1, 0)]
        path = Path(tmp) / ('seed.rpx' if redirected else 'base.rpx')
        build_rpx(path, [self.STUB, self.STUB, 0, 0], relocs, self.WORDS)
        return str(path)

    def test_unmodified_module_is_unchanged(self):
        with tempfile.TemporaryDirectory() as tmp:
            r = Rpx(self.make(tmp, False))
            before = {s.name: s.data for s in r.sections}
            self.assertEqual(r.apply_internal_relocations(), {})
            self.assertEqual({s.name: s.data for s in r.sections}, before)

    def test_redirected_relocations_are_applied(self):
        with tempfile.TemporaryDirectory() as tmp:
            r = Rpx(self.make(tmp, True))
            changed = r.apply_internal_relocations()
            self.assertEqual(sorted(changed), [TEXT + 18, DATA])
            table = struct.unpack('>4I', r.by_name['.data'].data)
            self.assertEqual(table, (self.NEW, self.STUB, 0, 0))  # import and undefined slots as linked
            words = struct.unpack('>6I', r.by_name['.text'].data)
            self.assertEqual(words[4], 0x38630040)
            self.assertEqual(words[5], self.WORDS[5])  # import HI: the recompiler resolves it

    def test_translation_sees_the_loaded_pointers(self):
        # Before #383 the stub's only reference was the redirected relocation's raw bytes: discovery
        # found the new function only through the relocation, and the loader kept the stub pointer.
        with tempfile.TemporaryDirectory() as tmp:
            p = analyze.Program(self.make(tmp, True))
            entries = set(p.discover())
            self.assertIn(self.NEW, entries)
            self.assertIn(self.STUB, entries)  # still referenced by table[1]
            self.assertEqual(p.word(TEXT + 16), 0x38630040)
            self.assertEqual(sorted(p.relocated), [TEXT + 18, DATA])

    def test_relocation_outside_its_section_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'bad.rpx'
            build_rpx(path, [0], [('data', DATA + 4, R_PPC_ADDR32, 1, 0)], [BLR])
            with self.assertRaisesRegex(ValueError, 'outside section'):
                Rpx(str(path)).apply_internal_relocations()


if __name__ == '__main__':
    unittest.main()
