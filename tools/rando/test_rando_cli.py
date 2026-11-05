#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""wwhd-rando without game files: settings, presets, seed names, permalinks and the refusals.

usage: test_rando_cli.py BUILD_DIR   (cmake -S tools/rando -B BUILD_DIR && cmake --build BUILD_DIR)
The game-file checks (the output against the official TWWHD Randomizer 1.2.0, seed by seed) need a USA game and run
outside CI; see tools/rando/README.md.
"""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

BUILD = Path(sys.argv.pop(1) if len(sys.argv) > 1 else 'build-rando').resolve()
EXE = BUILD / ('wwhd-rando.exe' if sys.platform == 'win32' else 'wwhd-rando')
DATA = BUILD / 'data'


class Cli:
    def __init__(self, work):
        self.work = work

    def __call__(self, *args, ok=True):
        p = subprocess.run([str(EXE), '--data', str(DATA), '--work', str(self.work), *args],
                           capture_output=True, text=True, timeout=120)
        r = json.loads(p.stdout.strip().splitlines()[-1])
        if ok is not None:
            assert r['ok'] == ok, (args, r, p.stderr[-2000:])
            assert p.returncode == (0 if ok else 1), (args, p.returncode)
        return r


class RandoCli(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.cli = Cli(Path(self.tmp.name) / 'work')

    def tearDown(self):
        self.tmp.cleanup()

    def test_version(self):
        p = subprocess.run([str(EXE), 'version'], capture_output=True, text=True, timeout=60)
        self.assertEqual(json.loads(p.stdout), {'ok': True, 'version': '1.2.0'})

    def test_locations(self):
        locs = self.cli('locations')['locations']
        self.assertGreater(len(locs), 300)
        chest = [l for l in locs if l['paths']]
        self.assertTrue(chest)
        path, offsets = next(iter(chest[0]['paths'].items()))
        self.assertTrue(path.startswith('content/'))
        self.assertTrue(all(isinstance(o, int) for o in offsets))

    def test_data_has_no_game_art(self):
        files = [p.relative_to(DATA).as_posix() for p in DATA.rglob('*') if p.is_file()]
        self.assertTrue(files)
        self.assertFalse([f for f in files if f.lower().endswith(('.dds', '.tga', '.png', '.bftex', '.bflim', '.szs'))])
        self.assertFalse([f for f in files if f.startswith('assets/')])
        self.assertIn('LICENSE-TWWHD-Randomizer.txt', files)

    def test_settings_round_trip(self):
        s = self.cli('settings')['settings']
        self.assertIn('Progress Dungeons', s)
        self.assertNotIn('Starting Gear', s)
        self.assertEqual(s['Progress Mail']['kind'], 'bool')
        self.assertEqual((s['Path Hints']['kind'], s['Path Hints']['min'], s['Path Hints']['max']), ('count', 0, 7))
        self.assertEqual(s['Dungeon Small Keys']['choices'][0], 'Vanilla')
        self.assertEqual(s['Damage Multiplier']['min'], 2)
        self.cli('set', 'Path Hints', '8', ok=False)  # outside the PC randomizer's range
        self.cli('set', 'Damage Multiplier', '1', ok=False)
        self.cli('set', 'Progress Mail', '1')
        self.assertEqual(self.cli('get', 'Progress Mail')['value'], 1)
        self.cli('set', 'Progress Mail', '0')
        self.assertEqual(self.cli('get', 'Progress Mail')['value'], 0)
        self.cli('set', 'No such setting', '1', ok=False)
        self.cli('set', 'Starting Gear', '1', ok=False)
        self.cli('set', 'Progress Mail', 'x', ok=False)

    def test_seed_and_permalink(self):
        self.cli('set-seed', 'WWHDTest')
        link = self.cli('permalink')['permalink']
        self.cli('new-seed')
        self.assertNotEqual(self.cli('permalink')['permalink'], link)
        self.cli('load-permalink', link)
        self.assertEqual(self.cli('seed')['seed'], 'WWHDTest')
        self.assertEqual(self.cli('permalink')['permalink'], link)
        self.cli('load-permalink', 'not a permalink', ok=False)
        self.assertEqual(self.cli('permalink')['permalink'], link)  # (a refused one changes nothing)

    def test_presets(self):
        self.cli('set-seed', 'WWHDTest')
        links = {name: self.cli('preset', name)['permalink'] for name in ('standard', 'beginner', 'everything')}
        self.assertEqual(len(set(links.values())), 3)
        self.cli('preset', 'beginner')
        self.assertEqual(self.cli('get', 'Progress Puzzle Caves')['value'], 0)
        self.assertEqual(self.cli('seed')['seed'], 'WWHDTest')  # presets keep the seed name
        self.cli('preset', 'standard')
        self.cli('defaults')
        self.assertEqual(self.cli('permalink')['permalink'], links['standard'])
        self.cli('preset', 'nope', ok=False)

    def test_race_mode_refused(self):
        self.cli('set-seed', 'WWHDTest')
        self.cli('set', 'No Spoiler Log', '1')
        race = self.cli('permalink')['permalink']
        r = self.cli('generate', '--game', self.tmp.name, '--out', str(Path(self.tmp.name) / 'out'), ok=False)
        self.assertIn('Race-mode', r['error'])
        self.cli('set', 'No Spoiler Log', '0')
        normal = self.cli('permalink')['permalink']
        r = self.cli('load-permalink', race, ok=False)
        self.assertIn('Race-mode', r['error'])
        self.assertEqual(self.cli('permalink')['permalink'], normal)

    def test_releases(self):
        game = Path(self.tmp.name) / 'game'
        (game / 'meta').mkdir(parents=True)
        out = str(Path(self.tmp.name) / 'out')
        for tid, word in (('0005000010143400', 'Japanese'), ('000500001234ABCD', 'knows')):
            (game / 'meta/meta.xml').write_text('<menu><title_id type="hexBinary" length="8">%s</title_id></menu>' % tid)
            self.assertIn(word, self.cli('generate', '--game', str(game), '--out', out, ok=False)['error'])
        self.assertFalse(Path(out).exists())

    def test_output_must_be_empty(self):
        game = Path(self.tmp.name) / 'game'
        (game / 'meta').mkdir(parents=True)
        (game / 'meta/meta.xml').write_text('<menu><title_id>0005000010143500</title_id></menu>')
        out = Path(self.tmp.name) / 'out'
        out.mkdir()
        (out / 'x').write_text('x')
        self.assertIn('not empty', self.cli('generate', '--game', str(game), '--out', str(out), ok=False)['error'])


if __name__ == '__main__':
    unittest.main()
