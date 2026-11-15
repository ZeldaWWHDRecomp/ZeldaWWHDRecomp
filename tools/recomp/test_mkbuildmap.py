#!/usr/bin/env python3
"""Unit tests for the parts of mkbuildmap.py that need no game files: python3 test_mkbuildmap.py

The derivation itself runs on the two executables (mkbuildmap.py usa.rpx other.rpx, --check for a map in
tools/recomp/builds/); these check the field-layout support the Japanese build needs (--layout).
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkbuildmap  # noqa: E402
from mkbuildmap import Layout  # noqa: E402

JP_GAME_INFO = "g_dComIfG_gameInfo:1046F0B0:62F8:0+12A0:5AB0=-4+5BEF=-5+5BF0=-4"


def lwz(rd, ra, d):
    return (32 << 26) | (rd << 21) | (ra << 16) | (d & 0xFFFF)


class LayoutSpec(unittest.TestCase):
    def setUp(self):
        self.lay = Layout.parse(JP_GAME_INFO)

    def test_parse(self):
        self.assertEqual((self.lay.name, self.lay.start, self.lay.size), ("g_dComIfG_gameInfo", 0x1046F0B0, 0x62F8))
        self.assertEqual(self.lay.bases, [0, 0x12A0])
        self.assertEqual(self.lay.steps, [(0x5AB0, -4), (0x5BEF, -5), (0x5BF0, -4)])

    def test_json_round_trip(self):
        again = Layout.from_json(self.lay.to_json())
        self.assertEqual((again.name, again.start, again.size, again.bases, again.steps),
                         (self.lay.name, self.lay.start, self.lay.size, self.lay.bases, self.lay.steps))

    def test_bad_specs_are_refused(self):
        for spec in ("x:1000:100:0", "x:1000:100:0:200=-4", "x:1000:100:0:40=-4+40=-8", "x:1000:zz:0:40=-4"):
            with self.assertRaises(SystemExit, msg=spec):
                Layout.parse(spec)

    def test_delta(self):
        self.assertEqual(self.lay.delta(0x5AAC), 0)
        self.assertEqual(self.lay.delta(0x5AB0), -4)
        self.assertEqual(self.lay.delta(0x5BEF), -5)
        self.assertEqual(self.lay.delta(0x5BF0), -4)
        self.assertEqual(self.lay.delta(0x62F4), -4)

    def test_explains_from_either_base(self):
        self.assertTrue(self.lay.explains(0x5B34, 0x5B30))            # from the gameInfo pointer
        self.assertTrue(self.lay.explains(0x5B34 - 0x12A0, 0x5B30 - 0x12A0))  # from the play pointer
        self.assertTrue(self.lay.explains(0x0100, 0x0100))            # an unmoved field
        self.assertFalse(self.lay.explains(0x5B34, 0x5B34))           # a moved field must move
        self.assertFalse(self.lay.explains(0x5B34, 0x5B2C))
        self.assertTrue(self.lay.sensitive(0x5B34))
        self.assertFalse(self.lay.sensitive(-0x2000))

    def test_word_and_body_equality(self):
        self.assertTrue(mkbuildmap.layout_word_equal(lwz(3, 4, 0x5B34), lwz(3, 4, 0x5B30), [self.lay]))
        self.assertFalse(mkbuildmap.layout_word_equal(lwz(3, 4, 0x5B34), lwz(3, 4, 0x5B30), []))
        self.assertFalse(mkbuildmap.layout_word_equal(lwz(3, 4, 0x5B34), lwz(3, 5, 0x5B30), [self.lay]))  # other base reg
        self.assertFalse(mkbuildmap.layout_word_equal(addi(3, 0, 0x5B34), addi(3, 0, 0x5B30), [self.lay]))  # li: rA == 0
        self.assertFalse(mkbuildmap.layout_word_equal(lwz(3, 0, 0x5B34), lwz(3, 0, 0x5B30), [self.lay]))
        self.assertEqual(mkbuildmap.masked([addi(3, 0, 0x5B34), lwz(3, 4, 0x5B34)], [self.lay]),
                         [addi(3, 0, 0x5B34), lwz(3, 4, 0)])
        x_form = (31 << 26) | (3 << 21) | (4 << 16) | (5 << 11) | (23 << 1)  # lwzx: no displacement
        self.assertFalse(mkbuildmap.layout_word_equal(x_form, x_form ^ 4, [self.lay]))
        self.assertTrue(mkbuildmap.layout_body_equal([0x60000000, lwz(3, 4, 0x5BEF)],
                                                     [0x60000000, lwz(3, 4, 0x5BEA)], [self.lay]))
        self.assertFalse(mkbuildmap.layout_body_equal([0x60000000], [0x60000000, 0x60000000], [self.lay]))


class DataSteps(unittest.TestCase):
    def test_field_steps_go_into_the_data_map(self):
        lay = Layout("obj", 0x2000, 0x100, [0], [(0x40, -4)])
        steps = [(0x1000, 0), (0x3000, -8)]
        self.assertEqual(mkbuildmap.with_layouts(steps, [lay]), [(0x1000, 0), (0x2040, -4), (0x2100, 0), (0x3000, -8)])
        self.assertEqual(mkbuildmap.with_layouts(steps, []), steps)

    def test_the_object_moves_as_a_whole_too(self):
        lay = Layout("obj", 0x2000, 0x100, [0], [(0x40, -4)])
        out = mkbuildmap.with_layouts([(0x1000, 8)], [lay])
        self.assertEqual(out, [(0x1000, 8), (0x2040, 4), (0x2100, 8)])
        f = mkbuildmap.piecewise(out)
        self.assertEqual(f(0x2000), 0x2008)
        self.assertEqual(f(0x2040), 0x2044)

    def test_a_map_already_changing_inside_the_object_is_refused(self):
        lay = Layout("obj", 0x2000, 0x100, [0], [(0x40, -4)])
        with self.assertRaises(mkbuildmap.Mismatch):
            mkbuildmap.with_layouts([(0x1000, 0), (0x2050, 4)], [lay])

    def test_the_japanese_map_has_its_layout(self):
        import json
        here = os.path.dirname(os.path.abspath(__file__))
        with open(os.path.join(here, "builds", "jp.json"), encoding="utf-8") as f:
            m = json.load(f)
        (lay,) = [Layout.from_json(d) for d in m["layouts"]]
        self.assertEqual(lay.to_json(), Layout.parse(JP_GAME_INFO).to_json())
        steps = [(int(a, 16), d) for a, d in m["data_steps"]]
        f = mkbuildmap.piecewise(steps)
        for off in (0, 0x5134, 0x5AB0, 0x5B34, 0x5BEF, 0x5BF0, 0x62F4):
            self.assertEqual(f(lay.start + off), lay.start + off + lay.delta(off), "+%#x" % off)


class CodePointers(unittest.TestCase):
    """The data-section pointer pass of derive_aligned (on stand-ins for the two executables)."""

    @staticmethod
    def binary(entries, pointers):
        from types import SimpleNamespace
        return SimpleNamespace(entries=entries, code_pointers=pointers)

    @staticmethod
    def result(maps, differing=()):
        return {"_data": lambda slot: slot + 0x10, "_maps": maps, "_called": set(), "stats": {},
                "differing_functions": [[a, 4, maps(a), 4] for a in differing]}

    def test_a_slot_pairs_functions_without_a_counterpart(self):
        canon = self.binary([0x100, 0x200, 0x300], {0x1000: 0x200, 0x1004: 0x300})
        other = self.binary([0x100, 0x280, 0x380], {0x1010: 0x280, 0x1014: 0x380})
        found = mkbuildmap.pair_through_pointers(canon, other, self.result(lambda t: t), [1, 2], [1, 2])
        self.assertEqual(found, {1: 1, 2: 2})

    def test_an_ambiguous_slot_pairs_nothing(self):
        canon = self.binary([0x100, 0x200, 0x300], {0x1000: 0x200, 0x1004: 0x300})
        other = self.binary([0x100, 0x280, 0x380], {0x1010: 0x280, 0x1014: 0x280})  # both slots hold one function
        found = mkbuildmap.pair_through_pointers(canon, other, self.result(lambda t: t), [1, 2], [1, 2])
        self.assertEqual(found, {})

    def test_slots_are_checked_and_a_pairing_without_evidence_reported(self):
        canon = self.binary([], {0x1000 + 4 * k: 0x200 for k in range(200)} | {0x2000: 0x300})
        other = self.binary([], {0x1010 + 4 * k: 0x240 for k in range(200)} | {0x2010: 0x999})
        r = self.result(lambda t: t + 0x40, differing=(0x200, 0x300, 0x400))
        mkbuildmap.check_pointers(canon, other, r)
        self.assertEqual((r["stats"]["pointers_checked"], r["stats"]["pointers_elsewhere"]), (201, 1))
        self.assertEqual(r["stats"]["differing_unconfirmed"], ["00000300", "00000400"])

    def test_many_slots_holding_other_functions_are_refused(self):
        canon = self.binary([], {0x1000 + 4 * k: 0x200 for k in range(10)})
        other = self.binary([], {0x1010 + 4 * k: 0x999 for k in range(10)})
        with self.assertRaisesRegex(mkbuildmap.Mismatch, "code pointers"):
            mkbuildmap.check_pointers(canon, other, self.result(lambda t: t + 0x40))


def addi(rd, ra, imm):
    return (14 << 26) | (rd << 21) | (ra << 16) | (imm & 0xFFFF)


def lis(rd, imm):
    return (15 << 26) | (rd << 21) | (imm & 0xFFFF)


def bl(pc, target):
    return (18 << 26) | ((target - pc) & 0x03FFFFFC) | 1


BLR = 0x4E800020
R_PPC_ADDR16_HA = 6
R_PPC_ADDR16_LO = 4


class SyntheticBinary(mkbuildmap.Binary):
    """In-memory stand-in for Binary so align() and derive_aligned() run without .rpx files."""

    def __init__(self, funcs, text_lo=0x02000020, data_lo=0x10000000, data_size=0x500000, code_pointers=()):
        from collections import defaultdict
        from types import SimpleNamespace

        self.entries = []
        self.ends = []
        self.reloc_type = {}
        self.reloc_target = defaultdict(list)
        words = {}

        pc = text_lo
        deferred = []
        for fn in funcs:
            self.entries.append(pc)
            for item in fn:
                if isinstance(item, tuple) and item[0] == "bl":
                    deferred.append((pc, item[1]))
                    words[pc] = 0
                elif isinstance(item, tuple):
                    word, typ, target = item
                    words[pc] = word
                    self.reloc_type[pc] = typ
                    self.reloc_target[pc].append((typ, target))
                else:
                    words[pc] = item
                pc += 4
            self.ends.append(pc)

        text_hi = pc
        for call_pc, target_idx in deferred:
            words[call_pc] = bl(call_pc, self.entries[target_idx])
        self.code_pointers = {slot: self.entries[idx] for slot, idx in code_pointers}

        self.p = SimpleNamespace(
            text_lo=text_lo,
            text_hi=text_hi,
            word=words.__getitem__,
            in_text=lambda a, lo=text_lo, hi=text_hi: lo <= a < hi,
            import_calls=set(),
            undef_calls=set(),
            rpx=SimpleNamespace(sections=[SimpleNamespace(name=".data", addr=data_lo, size=data_size)]),
        )


class AlignedDerivation(unittest.TestCase):
    """Independent synthetic test of align() and derive_aligned()."""

    def setUp(self):
        self.lay = Layout.parse(JP_GAME_INFO)

    def _build_pair(self):
        def anchor(imm):
            return [addi(3, 0, imm), addi(4, 0, imm + 1), BLR]

        def data_ref(addr):
            hi = ((addr + 0x8000) >> 16) & 0xFFFF
            lo = addr & 0xFFFF
            return [(lis(3, hi), R_PPC_ADDR16_HA, addr),
                    (addi(3, 3, lo), R_PPC_ADDR16_LO, addr),
                    BLR]

        moved_body = [addi(3, 0, 0x555), addi(3, 3, 7), BLR]
        canon_funcs = [
            [("bl", 5), ("bl", 9), BLR],                                                   # 0: fn0
            data_ref(0x1046F0B0),                                                          # 1: fn1
            [addi(3, 0, 0x111), addi(4, 0, 0x222), addi(5, 0, 0x333), BLR],                # 2: removed
            anchor(10),                                                                    # 3: fn2
            anchor(20),                                                                    # 4: fn3
            [lwz(3, 3, 0x5B34), BLR],                                                      # 5: get_link
            anchor(30),                                                                    # 6: fn4
            data_ref(0x10480000),                                                          # 7: fn5
            anchor(40),                                                                    # 8: fn6
            moved_body,                                                                    # 9: moved
            anchor(50),                                                                    # 10: fn7
        ]
        other_funcs = [
            [("bl", 6), ("bl", 2), BLR],                                                   # 0: fn0'
            data_ref(0x1046F0B0),                                                          # 1: fn1'
            moved_body,                                                                    # 2: moved'
            anchor(10),                                                                    # 3: fn2'
            [addi(3, 0, 0x444), addi(4, 0, 0x555), addi(5, 0, 0x666), BLR],                # 4: inserted
            anchor(20),                                                                    # 5: fn3'
            [lwz(3, 3, 0x5B30), BLR],                                                      # 6: get_link'
            anchor(30),                                                                    # 7: fn4'
            data_ref(0x10480020),                                                          # 8: fn5'
            anchor(40),                                                                    # 9: fn6'
            anchor(50),                                                                    # 10: fn7'
        ]

        canon = SyntheticBinary(canon_funcs, text_lo=0x02000020, code_pointers=[(0x10480100, 9)])
        other = SyntheticBinary(other_funcs, text_lo=0x02000040, code_pointers=[(0x10480120, 2)])
        return canon, other

    def test_align_and_derive_aligned_with_layout(self):
        canon, other = self._build_pair()
        pairs, removed, inserted = mkbuildmap.align(canon, other, [self.lay])
        self.assertEqual(removed, [2])
        self.assertEqual(inserted, [4])
        self.assertEqual(pairs, {0: 0, 1: 1, 3: 3, 4: 5, 5: 6, 6: 7, 7: 8, 8: 9, 9: 2, 10: 10})

        res = mkbuildmap.derive_aligned(canon, other, [self.lay])
        self.assertEqual(res["removed_functions"], [[canon.entries[2], 16]])
        self.assertEqual(res["inserted_functions"], [[other.entries[4], 16]])
        self.assertEqual(res["differing_functions"], [])
        s = res["stats"]
        self.assertEqual((s["same_code"], s["layout_only"], s["reordered"]), (9, 1, 1))
        self.assertEqual((s["calls_checked"], s["pointers_checked"], s["pointers_elsewhere"]), (2, 1, 0))
        self.assertEqual(s["layout_displacements"], {"+5B34 (-4)": 1})
        self.assertIn("checked", mkbuildmap.MAP_KEYS)
        import io, contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            mkbuildmap.report(res)
        self.assertIn("explained displacements: 1 word (1 distinct)", buf.getvalue())
        self.assertIn("+5B34 (-4): 1", buf.getvalue())
        code = mkbuildmap.piecewise(res["code_steps"])
        data = mkbuildmap.piecewise(res["data_steps"])
        self.assertEqual(code(canon.entries[5]), other.entries[6])
        self.assertEqual(code(canon.entries[9]), other.entries[2])
        self.assertEqual(data(0x1046F0B0 + 0x5B34), 0x1046F0B0 + 0x5B30)
        self.assertEqual(data(0x10480000), 0x10480020)

    def test_without_layout_the_getter_pair_is_differing_not_layout_only(self):
        canon, other = self._build_pair()
        res = mkbuildmap.derive_aligned(canon, other, [])
        self.assertEqual(res["stats"]["layout_only"], 0)
        self.assertEqual(res["stats"]["layout_displacements"], {})
        self.assertEqual(res["differing_functions"], [[canon.entries[5], 8, other.entries[6], 8]])


if __name__ == "__main__":
    unittest.main(verbosity=2)
