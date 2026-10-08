#!/usr/bin/env python3
"""Unit tests for the build registry and the address maps: python3 test_builds.py

No game files: the maps (tools/recomp/builds/*.json) are plain numbers, derived and checked from two
executables by tools/recomp/mkbuildmap.py (run that with both rpx files to check them against the
real binaries).
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import builds  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


class Registry(unittest.TestCase):
    def test_canonical_is_usa_and_identity(self):
        usa = builds.canonical_build()
        self.assertEqual(usa.name, "USA")
        self.assertTrue(usa.canonical)
        for a in (0x02000020, 0x025F172C, 0x028F8250, 0x101F4BAC, 0x1046F0B0):
            self.assertEqual(usa.code(a), a)
            self.assertEqual(usa.data(a), a)
            self.assertFalse(usa.body_differs(a))

    def test_known_builds(self):
        names = {b.name for b in builds.all_builds()}
        self.assertIn("USA", names)
        self.assertIn("EU", names)
        self.assertEqual(builds.by_title("0005000010143600").name, "EU")
        self.assertIsNone(builds.by_title("0005000010143400"))  # Japan: no map yet
        self.assertEqual(builds.by_name("eu").title_id, "0005000010143600")
        self.assertIsNone(builds.by_name("nonesuch"))

    def test_titles_and_hashes_are_distinct(self):
        all_builds = builds.all_builds()
        self.assertEqual(len({b.title_id for b in all_builds}), len(all_builds))
        self.assertEqual(len({b.sha256 for b in all_builds}), len(all_builds))
        for b in all_builds:
            self.assertRegex(b.sha256, r"^[0-9a-f]{64}$")
            self.assertRegex(b.title_id, r"^00050000[0-9a-f]{8}$")


class Identify(unittest.TestCase):
    def test_a_file_that_is_no_build(self):
        with open(__file__, "rb"):
            self.assertIsNone(builds.identify(__file__))

    def test_by_sha256(self):
        usa = builds.canonical_build()
        self.assertEqual(builds.by_sha256(usa.sha256).name, "USA")
        self.assertIsNone(builds.by_sha256("0" * 64))


class Maps(unittest.TestCase):
    def setUp(self):
        self.eu = builds.by_name("EU")

    def test_bounds_refuse_unverified_addresses(self):
        for kind in ("code", "data"):
            lo, hi = self.eu.bounds[kind]
            translate = getattr(self.eu, kind)
            for address in (lo - 1, hi, 0xFFFFFFFF):
                with self.assertRaises(ValueError):
                    translate(address)
        with self.assertRaises(ValueError):
            self.eu.data(0x145AC92C)  # an allocation is not a mapped global

    def test_noncanonical_map_requires_bounds(self):
        with self.assertRaisesRegex(ValueError, "bounds"):
            builds.Build({"name": "EU", "title_id": "0005000010143600", "rpx_sha256": "0" * 64})

    def test_invalid_shift_tables_are_refused(self):
        for steps in (([8, 0], [4, 0]), ([4, 0], [4, 1]), ([4, -8],), ([4, 0x80000000],)):
            with self.assertRaises(ValueError):
                builds._Shift(steps)

    def test_steps_are_sorted_and_start_at_the_section(self):
        for shift in (self.eu._code, self.eu._data):
            self.assertEqual(shift.starts, sorted(shift.starts))
            self.assertEqual(len(set(shift.starts)), len(shift.starts))
        self.assertEqual(self.eu._code.starts[0], 0x02000020)   # .text of the USA build
        self.assertEqual(self.eu._data.starts[0], 0x10000000)   # .rodata

    def test_inverse_round_trip(self):
        probe = list(self.eu._code.starts) + [a + 4 for a in self.eu._code.starts] + \
                [a - 4 for a in self.eu._code.starts[1:]] + [0x02000020, 0x025F172C, 0x028F8250] + \
                [a for a, _, _, _ in self.eu.differing] + [b for _, _, b, _ in self.eu.differing]
        for a in probe:
            if self.eu.body_differs(a):
                continue   # see test_inside_a_differing_function_has_no_inverse
            self.assertEqual(self.eu.canon_code(self.eu.code(a)), a, "%08X" % a)

    def test_differing_function_entries_round_trip(self):
        """Their entry is still a one-to-one correspondence: only what is inside them is not."""
        for canon, _, addr, _ in self.eu.differing:
            self.assertEqual(self.eu.code(canon), addr, "%08X" % canon)
            self.assertEqual(self.eu.canon_code(addr), canon, "%08X" % addr)

    def test_inside_a_differing_function_has_no_inverse(self):
        """A function the build compiled shorter leaves canonical addresses past its end with no
        address of their own there (the next function's run already starts), so code() is only
        meaningful for a differing function's entry. Nothing may use those: body_differs() is what
        the recompiler and the hooks checks go by."""
        canon, canon_size, addr, size = next(d for d in self.eu.differing if d[3] < d[1])
        tail = canon + size + 4   # inside the canonical body, past the end of the build's
        self.assertTrue(self.eu.body_differs(tail))
        with self.assertRaises(ValueError):
            self.eu.code(tail)

    def test_known_addresses(self):
        # from the derivation (docs/builds.md): the first run is unshifted, the last is +0x8C0
        self.assertEqual(self.eu.code(0x02000020), 0x02000020)
        self.assertEqual(self.eu.code(0x028F8250), 0x028F8B10)
        self.assertEqual(self.eu.data(0x101F4BAC), 0x101F4BAC)
        self.assertEqual(self.eu.data(0x1048DBF8), 0x1048DC10)

    def test_body_differs_covers_the_regional_code(self):
        # the European language code the USA build does not have, and its neighbours
        self.assertTrue(self.eu.body_differs(0x025F8618))      # the player name for a message tag
        self.assertTrue(self.eu.body_differs(0x025F9448))      # SysSetting::update (the language reader)
        self.assertTrue(self.eu.body_differs(0x025FC3D0 + 4))  # inside putPlayerName
        self.assertFalse(self.eu.body_differs(0x025F9448 - 4))
        self.assertFalse(self.eu.body_differs(0x024FFC40))     # camera_draw: the same code in both
        self.assertEqual(len(self.eu.differing), 18)


class Hooks(unittest.TestCase):
    """What the recompiler checks when it reads tools/recomp/hooks*.txt for a build."""

    def test_every_file_is_read_for_the_canonical_build(self):
        entries, skipped = builds.read_hooks(builds.hook_files(), builds.canonical_build())
        self.assertEqual(skipped, [])
        self.assertGreater(len(entries), 200)
        for site, canon, addr, where in entries:
            self.assertEqual(canon, addr)  # the canonical build is the identity
            self.assertTrue(0x02000000 <= canon < 0x02900000, where)

    def test_build_directive_skips_a_file(self):
        entries, skipped = builds.read_hooks(builds.hook_files(), builds.by_name("EU"))
        self.assertIn(("hooks_language.txt", "USA"), skipped)
        self.assertFalse(any(where.startswith("hooks_language.txt") for _, _, _, where in entries))

    def test_no_instruction_site_patches_code_a_build_compiled_differently(self):
        """The invariant the recompiler enforces (recomp.py _check_hooks): an "@ADDR" hook patches
        one exact instruction, so its function must be the same code in that build. A new hook that
        breaks this for a build fails here, without needing the game."""
        for build in builds.all_builds():
            entries, _ = builds.read_hooks(builds.hook_files(), build)
            for site, canon, addr, where in entries:
                if site:
                    self.assertFalse(build.body_differs(canon),
                                     "%s: %08X is inside a function the %s build compiled differently"
                                     % (where, canon, build.name))


if __name__ == "__main__":
    unittest.main(verbosity=2)
