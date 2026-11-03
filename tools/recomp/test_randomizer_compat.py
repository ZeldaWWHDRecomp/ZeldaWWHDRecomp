# SPDX-License-Identifier: MPL-2.0
"""Hook-clash checks on synthetic section bytes and the shipped hash-only profile."""
from copy import deepcopy
from pathlib import Path
from types import SimpleNamespace
import json
import tempfile
import unittest

import builds
import randomizer_compat as C

START = 0x02000020
HOOKS = [(False, START, START, 'test:1'), (True, START+4, START+4, 'test:2')]


def fixture():
    original = b'abcdefghijklmnop'
    reviewed = original[:12]+b'1234'
    section = SimpleNamespace(name='.text', addr=START, type=1, flags=6, size=16, data=original)
    rpx = SimpleNamespace(by_name={'.text': section}, symbols=[])
    profile = dict(format=1, id='synthetic', base_sha256=builds.canonical_build().sha256,
                   hooks_sha256=C.hook_digest(HOOKS), entry_point=START,
                   imports_sha256=C.import_digest(rpx), sections=[dict(name='.text', address=START, size=16, type=1, flags=6)],
                   points=[dict(address=a, sha256=C.digest(original[a-START:a-START+4])) for a in (START, START+4)],
                   functions=[dict(start=START, end=START+16, sha256=[C.digest(original), C.digest(reviewed)])])
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
        rpx.by_name['.text'].data = rpx.by_name['.text'].data[:8]+b'unknown!'
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
        self.assertEqual({p['address'] for p in profile['points']}, {a for _, _, a, _ in hooks})
        self.assertEqual(len({f['start'] for f in profile['functions']}), len(profile['functions']))
        for point in profile['points']:
            self.assertEqual(set(point), {'address', 'sha256'})
            self.assertRegex(point['sha256'], r'^[0-9a-f]{64}$')
        for function in profile['functions']:
            self.assertEqual(set(function), {'start', 'end', 'sha256'})
            self.assertLess(function['start'], function['end'])
            for value in function['sha256']:
                self.assertRegex(value, r'^[0-9a-f]{64}$')
        self.assertIsNone(builds.by_title(C.TITLE_ID))  # not a normal immutable build hash


if __name__ == '__main__':
    unittest.main()
