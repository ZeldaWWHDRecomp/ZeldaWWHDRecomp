# SPDX-License-Identifier: MPL-2.0
"""Separate extraction policy accepts only reviewed large-function variants."""
from copy import deepcopy
import hashlib
import json
import unittest
from unittest.mock import patch, Mock

import randomizer_compat as compat
import source_core_compat as core
from test_randomizer_compat import fixture, HOOKS, START


def data():
    image, base, reviewed = fixture()
    base['functions'][0]['sha256'] = base['functions'][0]['sha256'][:1]
    raw = json.dumps(base).encode()
    policy = {'format': 1, 'id': 'source-core-test', 'base_profile_sha256': hashlib.sha256(raw).hexdigest(),
              'functions': [{'start': START, 'end': START + 64, 'sha256': [compat.digest(reviewed)],
                             'review': 'synthetic reviewed full function'}]}
    return image, base, reviewed, raw, policy


class SourceCoreCompatibility(unittest.TestCase):
    def test_known_variant_core_accepts_normal_execution_policy_still_refuses(self):
        image, base, reviewed, raw, policy = data()
        image.by_name['.text'].data = reviewed
        with self.assertRaisesRegex(compat.CompatibilityError, 'unreviewed change'):
            compat.check(image, base, HOOKS, START)
        adjusted = core.reviewed_profile(raw, policy)
        self.assertEqual(compat.check(image, adjusted, HOOKS, START)['profile'], 'source-core-test')
        self.assertEqual(base['functions'][0]['sha256'], json.loads(raw)['functions'][0]['sha256'])

    def test_unknown_variant_and_guard_word_remain_rejected(self):
        image, _, reviewed, raw, policy = data()
        adjusted = core.reviewed_profile(raw, policy)
        for offset in (0, 4, 20, 63):
            value = bytearray(reviewed);value[offset] ^= 1
            image.by_name['.text'].data = bytes(value)
            with self.subTest(offset=offset), self.assertRaises(compat.CompatibilityError):
                compat.check(image, adjusted, HOOKS, START)

    def test_extension_does_not_relax_import_layout_or_entry(self):
        image, _, reviewed, raw, policy = data()
        adjusted = core.reviewed_profile(raw, policy)
        image.by_name['.text'].data = reviewed
        image.by_name['.text'].addr += 4
        with self.assertRaisesRegex(compat.CompatibilityError, 'layout'):
            compat.check(image, adjusted, HOOKS, START)
        image.by_name['.text'].addr -= 4
        with self.assertRaisesRegex(compat.CompatibilityError, 'entry point'):
            compat.check(image, adjusted, HOOKS, START + 4)

    def test_tiny_partial_duplicate_or_changed_base_policy_cannot_extend(self):
        _, _, _, raw, policy = data()
        for change in ('base', 'partial', 'tiny', 'duplicate', 'unknown-field'):
            altered = deepcopy(policy)
            if change == 'base': altered['base_profile_sha256'] = '0' * 64
            if change == 'partial': altered['functions'][0]['start'] += 4
            if change == 'tiny': altered['functions'][0]['end'] -= 4
            if change == 'duplicate': altered['functions'] *= 2
            if change == 'unknown-field': altered['waive_guards'] = True
            with self.subTest(change=change), self.assertRaises(compat.CompatibilityError):
                core.reviewed_profile(raw, altered)

    def test_wrapper_is_explicit_and_labels_data_only(self):
        from types import SimpleNamespace
        _, _, _, raw, policy = data()
        selected = SimpleNamespace(sha256='a' * 64, randomizer_report={})
        with patch.object(core.compat, 'PROFILE', Mock(read_bytes=Mock(return_value=raw))), \
             patch.object(core, 'POLICY', Mock(read_text=Mock(return_value=json.dumps(policy)))), \
             patch.object(compat, 'identify', return_value=selected) as identify:
            result = core.identify_source_core('private-seed.rpx', expected_sha256='a' * 64)
        self.assertTrue(result.source_core_data)
        self.assertIsNot(result, selected)
        self.assertFalse(hasattr(result, 'code'))
        self.assertEqual(selected.randomizer_report, {})
        self.assertEqual(result.randomizer_report['validation_policy'], 'source-core-data-only-v1')
        self.assertEqual(identify.call_args.kwargs['expected_sha256'], 'a' * 64)
        self.assertEqual(identify.call_args.kwargs['profile']['id'], policy['id'])


if __name__ == '__main__':
    unittest.main()
