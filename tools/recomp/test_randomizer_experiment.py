# SPDX-License-Identifier: MPL-2.0
"""Synthetic section, identity and hook-conflict gates for the experimental randomizer tool."""
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest

import builds
from recomp import Recompiler
from randomizer_experiment import audit, game_rpx, outside_inputs, require_review, run


def program(changed=(), address=0x1000, data_address=0x10000000, size=32):
    data = bytearray(32)
    for index in changed:
        data[index] = 1
    sections = {'.text': SimpleNamespace(addr=address, size=size, data=bytes(data), flags=6),
                '.data': SimpleNamespace(addr=data_address, size=16, data=bytes(16), flags=3)}
    return SimpleNamespace(p=SimpleNamespace(rpx=SimpleNamespace(by_name=sections), entry=0x1000),
                           sorted_entries=[0x1000, 0x1010], func_end=lambda a: 0x1010 if a == 0x1000 else 0x1020)


class Audit(unittest.TestCase):
    def test_body_overlap_does_not_mean_hook_instruction_changed(self):
        hooks = [(True, 0x1004, 0x1004, 'test:1'), (False, 0x1010, 0x1010, 'test:2')]
        report = audit(program(), program([8, 20]), hooks)
        self.assertEqual(report['changed_original_functions'], ['00001000', '00001010'])
        self.assertEqual(report['changed_original_text_bytes'], 2)
        self.assertEqual(len(report['hooks_in_changed_functions']), 2)
        self.assertFalse(report['changed_hook_instructions'])
        with self.assertRaisesRegex(ValueError, 'review the hook overlaps'):
            require_review(report, set())
        require_review(report, {'00001000', '00001010'})
        with self.assertRaises(ValueError):
            require_review(report, {'00001000', '00001010', '00001020'})

    def test_changed_hook_instruction_is_not_waived_by_review_flag(self):
        for site in (False, True):
            with self.subTest(site=site):
                report = audit(program(), program([6]), [(site, 0x1004, 0x1004, 'test:1')])
                self.assertEqual(len(report['changed_hook_instructions']), 1)
                with self.assertRaisesRegex(ValueError, 'hook instruction changed'):
                    require_review(report, {'00001000'})

    def test_moved_or_shrunken_sections_are_refused(self):
        for modified in (program(address=0x2000), program(data_address=0x10000004), program(size=16)):
            with self.assertRaisesRegex(ValueError, 'moved, shrank or disappeared'):
                audit(program(), modified, [])

    def test_unmapped_original_text_is_not_ignored(self):
        original, modified = program(), program([0])
        original.sorted_entries = [0x1010]
        report = audit(original, modified, [])
        self.assertEqual(report['unmapped_changed_text_bytes'], 1)
        with self.assertRaisesRegex(ValueError, 'outside the reference function map'):
            require_review(report, set())

    def test_appended_helpers_are_distinguished_from_changed_original_bodies(self):
        report = audit(program(), program(size=48), [])
        self.assertEqual(report['appended_text_bytes'], 16)
        self.assertFalse(report['changed_original_functions'])
        require_review(report, set())


class Inputs(unittest.TestCase):
    def test_preservation_flag_does_not_accept_unknown_rpx(self):
        # A real unknown file, through the real identity gate, before any RPX parsing.
        for preserve in (False, True):
            with self.assertRaisesRegex(SystemExit, 'not a build of the game this port knows'):
                Recompiler(__file__, preserve_registers=preserve)
        self.assertIsNone(builds.by_title('0005000010143599'))

    def test_game_folder_title_and_contents(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp).resolve()
            for directory in ('code', 'content', 'meta'):
                (p/directory).mkdir()
            (p/'code/cking.rpx').write_bytes(b'synthetic test, not game data')
            (p/'meta/meta.xml').write_text('<menu><title_id>0005000010143599</title_id></menu>')
            self.assertEqual(game_rpx(p, '0005000010143599'), p/'code/cking.rpx')
            with self.assertRaisesRegex(ValueError, 'expected title'):
                game_rpx(p, builds.canonical_build().title_id)

    def test_run_protects_external_symlink_targets_before_writing_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp).resolve()
            game, external = p/'game', p/'external'
            game.mkdir(); external.mkdir()
            try:
                (game/'code').symlink_to(external, target_is_directory=True)
            except OSError:
                self.skipTest('symlink permission unavailable')
            args = SimpleNamespace(base_game=game, game=p/'randomized', report=external/'audit.json', outdir=None)
            with self.assertRaisesRegex(ValueError, 'outside both input'):
                run(args)
            self.assertFalse((external/'audit.json').exists())

    def test_output_cannot_overwrite_input_even_through_symlink(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp).resolve()
            game = p/'game'
            game.mkdir()
            for path in (game, game/'code/cking.rpx'):
                with self.assertRaisesRegex(ValueError, 'outside both input'):
                    outside_inputs(path, [game])
            try:
                (p/'link').symlink_to(game, target_is_directory=True)
            except OSError:
                pass  # Windows machines without symlink permission still check the real paths.
            else:
                with self.assertRaises(ValueError):
                    outside_inputs(p/'link/code/cking.rpx', [game])
            self.assertEqual(outside_inputs(p/'gen', [game]), p/'gen')


if __name__ == '__main__':
    unittest.main()
