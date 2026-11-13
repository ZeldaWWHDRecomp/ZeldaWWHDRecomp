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


if __name__ == "__main__":
    unittest.main(verbosity=2)
