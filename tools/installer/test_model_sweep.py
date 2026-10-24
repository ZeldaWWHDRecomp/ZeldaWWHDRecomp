import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

import model_sweep
import shader_prebuild as prebuild


def sarc(names):
    """A minimal little-endian SARC holding empty files with these names (name table only)."""
    table = b''
    offsets = []
    for name in names:
        offsets.append(len(table) // 4)
        raw = name.encode() + b'\0'
        table += raw + b'\0' * (-len(raw) % 4)
    header = 0x14
    sfat = b'SFAT' + struct.pack('<HHI', 0xC, len(names), 0x65)
    nodes = b''.join(struct.pack('<IIII', i, 0x01000000 | off, 0, 0) for i, off in enumerate(offsets))
    sfnt = b'SFNT' + struct.pack('<HH', 8, 0) + table
    body = sfat + nodes + sfnt
    data_offset = header + len(body)
    head = b'SARC' + struct.pack('<HH', header, 0xFEFF) + struct.pack('<II', data_offset + 0, data_offset) + struct.pack('<HH', 0x100, 0)
    return head + body


class FakeGame:
    """A game process: writes the sweep's progress lines like the runtime (names only)."""

    def __init__(self, test, script):
        self.test, self.script, self.calls = test, list(script), []

    def __call__(self, command, env, stdout, stderr, cwd):
        self.calls.append((command, env))
        state = Path(env['WWHD_DSWEEP_STATE'])
        archives = [l for l in Path(env['WWHD_DSWEEP_ARCHIVES']).read_text().splitlines() if l]
        action = self.script.pop(0)
        progress = state / 'progress.txt'
        done = {l[13:] for l in progress.read_text().splitlines() if l.startswith('archive-done ')} if progress.exists() else set()
        skip = set((state / 'skip.txt').read_text().splitlines()) if (state / 'skip.txt').exists() else set()
        with progress.open('a') as out:
            out.write('start\n')
            for name in archives:
                if name in done or name in skip:
                    continue
                out.write('load %s\nmodel %s:a.bdl\n' % (name, name))
                if action == ('crash', name):
                    return Proc(139)
                if action == 'cancel' and len(done) == 1:
                    self.test.cancelled = True
                    return Proc(None)
                out.write('archive-done %s\n' % name)
                done.add(name)
                stdout.write(b'WWHD-PROGRESS %d %d %s\n' % (len(done), len(archives), name.encode()))
                stdout.flush()
            out.write('sweep-done\n')
        return Proc(0)


class Proc:
    def __init__(self, code):
        self.returncode = code
        self.terminated = False

    def poll(self):
        return None if self.returncode is None and not self.terminated else (self.returncode or 0)

    def terminate(self):
        self.terminated = True

    def wait(self, timeout=None):
        return self.returncode

    def kill(self):
        self.terminated = True


class ModelSweepTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'game'
        common = self.game / 'content' / 'Common'
        (common / 'Object').mkdir(parents=True)
        (common / 'Pack').mkdir()
        for name in ('Bk', 'Kn', 'Always'):
            (common / 'Object' / (name + '.szs')).write_bytes(b'Yaz0')
        (common / 'Pack' / 'szs_permanent0.pack').write_bytes(sarc(['Link.szs', 'sea_Room1.szs', 'sea_LOD03.szs',
                                                                      'Name_Stage.szs', 'Kn.szs', 'notes.txt']))
        (common / 'Pack' / 'permanent_2d_UsEnglish.pack').write_bytes(sarc(['Btn_00.szs']))
        self.cache = self.root / 'cache'
        self.cancelled = False
        self.lines = []

    def run_sweep(self, game, identity=None, **kwargs):
        return model_sweep.run('/synthetic/exe', self.game, self.cache, 'vulkan', identity or {'game': 'g', 'preparation': 'p'},
                               {'WWHD_SHADER_CACHE': 'live/shaders.bin'}, line=self.lines.append, popen=game,
                               sleep=lambda s: None, **kwargs)

    def test_archive_names_come_from_the_players_files_without_rooms_or_layouts(self):
        self.assertEqual(model_sweep.object_archives(self.game), ['Always', 'Bk', 'Kn', 'Link'])

    def test_stage_resources_come_from_the_players_files_by_stage(self):
        stage = self.game / 'content' / 'Common' / 'Stage'
        stage.mkdir()
        for name in ('sea_Room10', 'sea_Stage', 'A_mori_Room0', 'A_mori_Stage', 'sea_LODALL', 'Room0'):
            (stage / (name + '.szs')).write_bytes(b'Yaz0')
        self.assertEqual(model_sweep.stage_archives(self.game), {
            'A_mori': ['Stage', 'Room0'], 'Name': ['Stage'],
            'sea': ['Stage', 'Room1', 'Room10', 'LOD03', 'LODALL']})
        self.assertEqual(model_sweep.object_archives(self.game), ['Always', 'Bk', 'Kn', 'Link'])
        game = FakeGame(self, ['ok'])
        self.assertEqual(self.run_sweep(game), 'done')
        listed = Path(game.calls[0][1]['WWHD_DSWEEP_STAGES']).read_text().splitlines()
        self.assertEqual(listed, ['A_mori Stage Room0', 'Name Stage', 'sea Stage Room1 Room10 LOD03 LODALL'])
        for key in ('WWHD_DSWEEP_ENV', 'WWHD_DSWEEP_ACTORS', 'WWHD_DSWEEP_FILE_SELECT'):
            self.assertEqual(game.calls[0][1][key], '1')
        # stage visits (on unless WWHD_SETUP_MODEL_SWEEP_VISITS=0): each stage with rooms at its first room, the sea at every island
        self.assertEqual(Path(game.calls[0][1]['WWHD_DSWEEP_VISIT']).read_text().splitlines(), ['A_mori 0', 'sea *'])
        self.assertIn('visit-ENDumi', game.calls[0][1]['WWHD_DSWEEP_SKIP'].split(','))
        with mock.patch.dict(os.environ, {'WWHD_SETUP_MODEL_SWEEP_VISITS': '0'}):
            game = FakeGame(self, ['ok'])
            model_sweep.clear(self.cache)
            self.assertEqual(self.run_sweep(game), 'done')
            self.assertNotIn('WWHD_DSWEEP_VISIT', game.calls[0][1])

    def test_sweep_runs_hidden_and_muted_with_its_own_settings(self):
        game = FakeGame(self, ['ok'])
        self.assertEqual(self.run_sweep(game), 'done')
        command, env = game.calls[0]
        self.assertEqual(command[1:3], ['--game', str(self.game)])
        for key, value in (('WWHD_NO_AUDIO', '1'), ('WWHD_HIDDEN_WINDOWS', '1'), ('WWHD_NO_HOST_INPUT', '1'),
                           ('WWHD_DSWEEP', 'title,1,2'), ('WWHD_DSWEEP_EXIT', '1'), ('WWHD_RENDERER_RUNTIME', 'vulkan'),
                           ('WWHD_SHADER_CACHE', 'live/shaders.bin'), ('WWHD_UNCAPPED', '1')):
            self.assertEqual(env[key], value)
        self.assertEqual(env['WWHD_HEADSTART'], '0')
        self.assertIn('door10', env['WWHD_DSWEEP_SKIP'].split(','))  # names that stop the game outside their room
        self.assertNotEqual(Path(env['WWHD_SETTINGS']).parent, self.cache)
        self.assertIn('WWHD-PROGRESS 4 4 Link', self.lines)
        self.assertEqual(model_sweep.status(self.cache, 'vulkan', {'game': 'g', 'preparation': 'p'}), 'done')
        # done for this identity: nothing runs again
        self.assertEqual(self.run_sweep(FakeGame(self, [])), 'done')

    def test_a_crash_skips_the_model_and_resumes_after_the_archives_done(self):
        game = FakeGame(self, [('crash', 'Kn'), 'ok'])
        self.assertEqual(self.run_sweep(game), 'done')
        state = model_sweep.state_dir(self.cache, 'vulkan')
        self.assertEqual((state / 'skip.txt').read_text(), 'Kn:a.bdl\n')
        progress = (state / 'progress.txt').read_text().splitlines()
        self.assertEqual(progress.count('archive-done Always'), 1)  # not swept twice
        self.assertEqual(len(game.calls), 2)

    def test_the_culprit_is_the_last_model_archive_or_actor_started(self):
        lines = ['start', 'load Bk', 'model Bk:a.bdl', 'archive-done Bk', 'actors', 'actor Tbox:0:0:0',
                 'actor-done Tbox:0:0:0', 'actor Saku:1:0:0']
        self.assertEqual(model_sweep.culprit(lines, set()), 'Saku:1:0:0')
        self.assertIsNone(model_sweep.culprit(lines, {'Saku:1:0:0'}))
        self.assertEqual(model_sweep.culprit(lines[:3], set()), 'Bk:a.bdl')

    def test_a_repeating_crash_gives_up(self):
        game = FakeGame(self, [('crash', 'Kn')] * 3)
        self.assertEqual(self.run_sweep(game, attempts=3), 'failed')

    def test_cancel_pauses_and_the_next_run_resumes(self):
        game = FakeGame(self, ['cancel'])
        with self.assertRaises(InterruptedError):
            self.run_sweep(game, cancel=lambda: self.cancelled)
        self.assertEqual(model_sweep.status(self.cache, 'vulkan', {'game': 'g', 'preparation': 'p'}), 'partial')
        self.cancelled = False
        self.assertEqual(self.run_sweep(FakeGame(self, ['ok'])), 'done')
        progress = (model_sweep.state_dir(self.cache, 'vulkan') / 'progress.txt').read_text().splitlines()
        self.assertEqual(progress.count('archive-done Always'), 1)

    def test_another_build_starts_over_and_another_game_forgets_the_skips(self):
        self.run_sweep(FakeGame(self, [('crash', 'Kn'), 'ok']))
        state = model_sweep.state_dir(self.cache, 'vulkan')
        self.run_sweep(FakeGame(self, ['ok']), identity={'game': 'g', 'preparation': 'new build'})
        self.assertEqual((state / 'skip.txt').read_text(), 'Kn:a.bdl\n')
        self.assertEqual((state / 'progress.txt').read_text().count('archive-done Always'), 1)
        self.run_sweep(FakeGame(self, ['ok']), identity={'game': 'other', 'preparation': 'p'})
        self.assertFalse((state / 'skip.txt').exists())


class PrepareWithModelsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'game'
        self.game.mkdir()
        (self.game / 'synthetic.sharcfb').write_text('synthetic archive identity only')
        self.package = self.root / 'package'
        (self.package / 'tools').mkdir(parents=True)
        (self.package / 'tools/shaderprep.py').write_text('synthetic translator identity')
        self.template = self.root / 'template'
        self.template.write_text('synthetic template identity')
        self.cache = self.root / 'cache'
        self.swept = []
        self.outcome = {}

    def runner(self, command, env, cancel, progress):
        if command[1] == str(self.package / 'tools/shaderprep.py'):
            Path(command[3]).write_text('synthetic assembled output')

    def sweeper(self, executable, game, root, renderer, identity, cache_env, line, cancel, log):
        self.swept.append(renderer)
        outcome = self.outcome.get(renderer, 'done')
        if outcome == 'cancel':
            raise InterruptedError('cancelled')
        if outcome == 'crash':
            raise RuntimeError('broken')
        line('WWHD-PROGRESS 2 2 Link')
        self.assertEqual(cache_env['WWHD_VK_PIPELINE_CACHE_DIR'], str(root / 'live'))
        return outcome

    def prepare(self, events=None, **kwargs):
        def progress(text, fraction=None):
            if events is not None:
                events.append((text, fraction))
        return prebuild.prepare(self.game, self.template, '/synthetic/exe', self.cache, self.package,
                                runner=self.runner, renderers=['metal', 'vulkan'], renderer='metal', models=True,
                                sweeper=self.sweeper, progress=progress, **kwargs)

    def test_models_follow_the_warm_for_each_renderer_on_one_bar(self):
        events = []
        result = self.prepare(events)
        self.assertEqual(result['status'], 'ready')
        self.assertEqual(result['models'], {'metal': 'done', 'vulkan': 'done'})
        self.assertEqual(sorted(self.swept), ['metal', 'vulkan'])
        fractions = [f for _, f in events if f is not None]
        self.assertEqual(fractions, sorted(fractions))
        self.assertAlmostEqual(fractions[-1], 1.0)
        self.assertIn(('Preparing shaders: models (experimental)', None), events)
        # one bar for both: progress shows once both games named their totals (2 + 2 of 2 + 2 archives)
        self.assertIn(('Preparing shaders: models (experimental): Link', 1.0), events)

    def test_a_paused_sweep_keeps_the_generation_and_resumes_at_the_next_setup(self):
        self.outcome['vulkan'] = 'cancel'
        result = self.prepare()
        self.assertEqual(result['status'], 'ready')
        self.assertEqual(result['models']['vulkan'], 'paused')  # metal: done or paused (they run together)
        self.assertEqual(json.loads((self.cache / 'active.json').read_text())['status'], 'ready')
        # the generation is reused; only the unfinished sweep runs again (the fake keeps no state: both run)
        self.outcome.clear()
        self.swept.clear()
        result = self.prepare()
        self.assertTrue(result['reused'])
        self.assertEqual(result['models'], {'metal': 'done', 'vulkan': 'done'})

    def test_a_failing_sweep_never_fails_the_preparation(self):
        self.outcome['metal'] = 'crash'
        result = self.prepare()
        self.assertEqual(result['status'], 'ready')
        self.assertEqual(result['models'], {'metal': 'failed', 'vulkan': 'done'})

    def test_without_models_nothing_is_swept(self):
        result = prebuild.prepare(self.game, self.template, '/synthetic/exe', self.cache, self.package,
                                  runner=self.runner, renderers=['vulkan'], sweeper=self.sweeper)
        self.assertEqual(result['status'], 'ready')
        self.assertNotIn('models', result)
        self.assertEqual(self.swept, [])


if __name__ == '__main__':
    unittest.main()
