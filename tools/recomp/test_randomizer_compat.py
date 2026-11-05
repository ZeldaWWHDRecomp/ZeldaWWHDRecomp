# SPDX-License-Identifier: MPL-2.0
"""Hook-clash checks on synthetic section bytes and the shipped hash-only profile."""
from copy import deepcopy
from pathlib import Path
from types import SimpleNamespace
import json
import tempfile
import unittest
from unittest.mock import patch
import struct
import mk_randomizer_profile as M

import builds
import randomizer_compat as C

START = 0x02000020
HOOKS = [(False, START, START, 'test:1'), (True, START+4, START+4, 'test:2')]


def fixture():
    original = bytes(range(64))
    reviewed = original[:-4]+b'1234'
    section = SimpleNamespace(name='.text', addr=START, type=1, flags=6, size=64, data=original)
    rpx = SimpleNamespace(by_name={'.text': section}, sections=[section], symbols=[])
    profile = dict(format=2, id='synthetic', base_sha256=builds.canonical_build().sha256,
                   hooks_sha256=C.hook_digest(HOOKS), entry_point=START,
                   imports_sha256=C.import_digest(rpx), sections=[dict(name='.text', address=START, size=64, type=1, flags=6)],
                   **C.range_profile(rpx, [START, START+4], [(START, START+64)]))
    profile['functions'][0]['sha256'].append(C.digest(reviewed))
    return rpx, profile, reviewed


class Compatibility(unittest.TestCase):
    def test_original_and_reviewed_variant(self):
        rpx, profile, reviewed = fixture()
        self.assertFalse(C.check(rpx, profile, HOOKS, START)['reviewed_changed_functions'])
        rpx.by_name['.text'].data = reviewed
        self.assertEqual(C.check(rpx, profile, HOOKS, START)['reviewed_changed_functions'], ['02000020'])

    def test_exact_hook_instruction_cannot_change(self):
        for offset in (0, 4, 7):
            with self.subTest(offset=offset):
                rpx, profile, _ = fixture()
                data = bytearray(rpx.by_name['.text'].data);data[offset] ^= 1
                rpx.by_name['.text'].data = bytes(data)
                with self.assertRaisesRegex(C.CompatibilityError, 'hook instruction'):
                    C.check(rpx, profile, HOOKS, START)

    def test_unchanged_hook_word_does_not_waive_body_review(self):
        rpx, profile, _ = fixture()
        rpx.by_name['.text'].data = rpx.by_name['.text'].data[:8]+b'unknown!'+rpx.by_name['.text'].data[16:]
        with self.assertRaisesRegex(C.CompatibilityError, 'unreviewed change'):
            C.check(rpx, profile, HOOKS, START)

    def test_new_port_hook_requires_updated_profile(self):
        rpx, profile, _ = fixture()
        with self.assertRaisesRegex(C.CompatibilityError, 'hook set'):
            C.check(rpx, profile, HOOKS+[(True, START+8, START+8, 'test:3')], START)
        # A comment/file move leaves the actual hook set unchanged.
        renamed = [(site, canon, addr, 'renamed:99') for site, canon, addr, _ in HOOKS]
        C.check(rpx, profile, renamed, START)

    def test_layout_imports_and_entry_are_checked(self):
        rpx, profile, _ = fixture()
        for field, value in (('addr', START+4), ('size', 12), ('flags', 2), ('type', 8)):
            modified = deepcopy(rpx);setattr(modified.by_name['.text'], field, value)
            with self.subTest(field=field), self.assertRaises(C.CompatibilityError):
                C.check(modified, profile, HOOKS, START)
        with self.assertRaisesRegex(C.CompatibilityError, 'entry point'):
            C.check(rpx, profile, HOOKS, START+4)
        rpx.symbols = [SimpleNamespace(import_lib='fake.rpl', name='test', value=0xC0000000, import_kind='f', type=2)]
        with self.assertRaisesRegex(C.CompatibilityError, 'import layout'):
            C.check(rpx, profile, HOOKS, START)

    def test_appended_helpers_do_not_change_the_original_span(self):
        rpx, profile, _ = fixture()
        rpx.by_name['.text'].data += b'own test helpers'
        rpx.by_name['.text'].size = len(rpx.by_name['.text'].data)
        C.check(rpx, profile, HOOKS, START)

    def test_extra_or_duplicate_loaded_sections_are_rejected(self):
        rpx, profile, _ = fixture()
        extra = deepcopy(rpx.by_name['.text']);extra.name = '.extra'
        rpx.sections = [rpx.by_name['.text'], extra]
        with self.assertRaisesRegex(C.CompatibilityError, 'loaded section set'):
            C.check(rpx, profile, HOOKS, START)
        extra.name = '.text'
        with self.assertRaisesRegex(C.CompatibilityError, 'loaded section set'):
            C.check(rpx, profile, HOOKS, START)

    def test_aggregate_covers_tiny_bodies_and_binds_ranges(self):
        rpx, profile, _ = fixture()
        rpx.by_name['.text'].data = bytes(range(168))
        rpx.by_name['.text'].size = 168
        ranges = [(START, START+4), (START+4, START+12), (START+12, START+44), (START+44, START+104), (START+104, START+168)]
        profile.update(C.range_profile(rpx, [START, START+4], ranges))
        self.assertEqual([f['end']-f['start'] for f in profile['tiny_functions']], [4, 8, 32, 60])
        self.assertEqual([f['end']-f['start'] for f in profile['functions']], [64])
        C.check(rpx, profile, HOOKS, START)
        for offset in (0, 7, 11, 43, 103):
            changed = deepcopy(rpx)
            data = bytearray(changed.by_name['.text'].data);data[offset] ^= 1
            changed.by_name['.text'].data = bytes(data)
            with self.subTest(offset=offset), self.assertRaisesRegex(C.CompatibilityError, 'tiny hooked function'):
                C.check(changed, profile, HOOKS, START)
        altered = deepcopy(profile);altered['tiny_functions'][1]['end'] += 4
        with self.assertRaises(C.CompatibilityError):C.check(rpx, altered, HOOKS, START)
        for field, value in (('minimum_function_hash_bytes', 4), ('points', [START])):
            altered = dict(profile);altered[field] = value
            with self.assertRaises(C.CompatibilityError):C.check(rpx, altered, HOOKS, START)

    def test_reviewed_body_cannot_waive_changed_hook_word(self):
        rpx, profile, reviewed = fixture()
        changed = bytearray(reviewed);changed[0] ^= 1
        rpx.by_name['.text'].data = bytes(changed)
        profile['functions'][0]['sha256'].append(C.digest(bytes(changed)))
        with self.assertRaisesRegex(C.CompatibilityError, 'hook instruction'):
            C.check(rpx, profile, HOOKS, START)

    def test_generator_allows_only_explicit_body_review_and_canonical_hook_words(self):
        rpx, _, reviewed = fixture()
        reference = SimpleNamespace(build=builds.canonical_build(), p=SimpleNamespace(rpx=rpx, entry=START),
                                    func_of=lambda address: START, func_end=lambda address: START+64)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'reviewed.rpx';header = bytearray(28);struct.pack_into('>I', header, 24, START);path.write_bytes(header)
            changed = deepcopy(rpx);changed.by_name['.text'].data = reviewed
            with patch.object(M, 'Recompiler', return_value=reference), patch.object(M.builds, 'read_hooks', return_value=(HOOKS, None)), patch.object(M, 'Rpx', return_value=changed):
                profile = M.make(path, [path], {START})
                self.assertEqual(len(profile['functions'][0]['sha256']), 2)
                with self.assertRaisesRegex(ValueError, 'unreviewed hook function'):M.make(path, [path], set())
                data = bytearray(reviewed);data[4] ^= 1;changed.by_name['.text'].data = bytes(data)
                with self.assertRaisesRegex(C.CompatibilityError, 'hook instruction'):M.make(path, [path], {START})

    def test_generator_does_not_publish_or_waive_tiny_body_hashes(self):
        rpx, _, _ = fixture()
        reference = SimpleNamespace(build=builds.canonical_build(), p=SimpleNamespace(rpx=rpx, entry=START),
                                    func_of=lambda address: START, func_end=lambda address: START+20)
        changed = deepcopy(rpx)
        data = bytearray(changed.by_name['.text'].data);data[12] ^= 1
        changed.by_name['.text'].data = bytes(data)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'reviewed.rpx';header = bytearray(28);struct.pack_into('>I', header, 24, START);path.write_bytes(header)
            with patch.object(M, 'Recompiler', return_value=reference), patch.object(M.builds, 'read_hooks', return_value=(HOOKS, None)), patch.object(M, 'Rpx', return_value=changed):
                profile = M.make(path, [], set())
                self.assertEqual(profile['functions'], [])
                self.assertEqual(profile['tiny_functions'], [dict(start=START, end=START+20)])
                with self.assertRaisesRegex(C.CompatibilityError, 'tiny hooked function'):
                    M.make(path, [path], {START})

    def test_unknown_file_and_hash_mismatch_are_rejected_before_parse(self):
        with self.assertRaisesRegex(C.CompatibilityError, 'PowerPC RPX'):
            C.identify(__file__)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'fake.rpx'
            header = bytearray(0x34);header[:6] = b'\x7fELF\x01\x02';header[18:20] = b'\x00\x14'
            path.write_bytes(header)
            with self.assertRaisesRegex(C.CompatibilityError, 'after the setup'):
                C.identify(path, expected_sha256='0'*64)
            with self.assertRaisesRegex(C.CompatibilityError, 'invalid RPX'):
                C.identify(path)

    def test_shipped_profile_matches_current_port_and_contains_hashes_only(self):
        profile = json.loads(C.PROFILE.read_text())
        hooks, _ = builds.read_hooks(builds.hook_files(), builds.canonical_build())
        self.assertEqual(profile['hooks_sha256'], C.hook_digest(hooks))
        self.assertEqual(profile['base_sha256'], builds.canonical_build().sha256)
        self.assertEqual(set(profile['points']), {a for _, _, a, _ in hooks})
        self.assertEqual(len({f['start'] for f in profile['functions']}), len(profile['functions']))
        self.assertEqual(profile['format'], 2)
        self.assertEqual(profile['minimum_function_hash_bytes'], C.MIN_FUNCTION_HASH_BYTES)
        self.assertRegex(profile['guard_sha256'], r'^[0-9a-f]{64}$')
        self.assertTrue(all(isinstance(p, int) for p in profile['points']))
        self.assertTrue(profile['tiny_functions'])
        for function in profile['tiny_functions']:
            self.assertEqual(set(function), {'start', 'end'})
            self.assertLess(function['end']-function['start'], C.MIN_FUNCTION_HASH_BYTES)
        for function in profile['functions']:
            self.assertEqual(set(function), {'start', 'end', 'sha256'})
            self.assertGreaterEqual(function['end']-function['start'], C.MIN_FUNCTION_HASH_BYTES)
            for value in function['sha256']:
                self.assertRegex(value, r'^[0-9a-f]{64}$')
        # Reviewed 1.2.0 variants (docs/randomizer-maintainers.md): every seed (grapple/block speed,
        # vanilla-bug fixes, misc features); Quiet Swift Sail (procSteerMove); any entrance shuffle
        # (setGameStartStage). One reviewed variant each: the patches do not depend on the seed.
        reviewed = {0x0216F7FC: 2, 0x023EFF20: 2, 0x025499A0: 2, 0x0247E854: 2, 0x025217F8: 2}
        self.assertEqual({f['start']: len(f['sha256']) for f in profile['functions'] if len(f['sha256']) > 1},
                         reviewed)
        self.assertIsNone(builds.by_title(C.TITLE_ID))  # not a normal immutable build hash

    def test_shipped_eu_profile_matches_the_eu_build_and_the_reviewed_usa_variants(self):
        """randomizer-eu.json (EU seeds from the in-app randomizer's port of the patches): the EU build's hooks, the
        same reviewed functions as the USA profile through the EU address map, chosen by the EU entry point"""
        profile = json.loads(C.PROFILES[1].read_text())
        eu = builds.by_name('EU')
        hooks, _ = builds.read_hooks(builds.hook_files(), eu)
        self.assertEqual(profile['base_sha256'], eu.sha256)
        self.assertEqual(profile['hooks_sha256'], C.hook_digest(hooks))
        self.assertEqual(set(profile['points']), {a for _, _, a, _ in hooks})
        self.assertNotEqual(profile['entry_point'], json.loads(C.PROFILE.read_text())['entry_point'])
        reviewed = {eu.code(a): 2 for a in (0x0216F7FC, 0x023EFF20, 0x025499A0, 0x0247E854, 0x025217F8)}
        self.assertEqual({f['start']: len(f['sha256']) for f in profile['functions'] if len(f['sha256']) > 1},
                         reviewed)
        for function in profile['functions']:
            for value in function['sha256']:
                self.assertRegex(value, r'^[0-9a-f]{64}$')
        build = C.randomized_build(eu, 'c' * 64)
        self.assertEqual((build.name, build.title_id, build.sha256), ('EU', C.TITLE_ID, 'c' * 64))
        self.assertEqual(build.code(0x0216F7FC), eu.code(0x0216F7FC))


if __name__ == '__main__':
    unittest.main()
