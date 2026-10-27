"""android_models.py: setup's models step on Android, driven by the desktop's model_sweep.run with the
setup screen starting each attempt (here a thread plays ModelPrepareActivity and the game)."""
import itertools
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import android_models  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
model_sweep = android_models.load_installer(ROOT)[0]

PIDS = itertools.count(4000)


class FakeActivity(threading.Thread):
    """ModelPrepareActivity and its game: starts each requested attempt (as the setup screen does), writes the
    sweep's progress like the runtime, quits when asked (model-attempt-stop.json)."""

    def __init__(self, job, script):
        super().__init__(daemon=True)
        self.job, self.script = Path(job), list(script)
        self.alive, self.seen, self.requests = set(), set(), []
        self.quit = threading.Event()

    def run(self):
        while not self.quit.is_set():
            request = android_models.read_json(self.job / 'model-attempt.json')
            if request and request['id'] not in self.seen:
                self.seen.add(request['id'])
                self.requests.append(request)
                self.attempt(request, self.script.pop(0) if self.script else 'done')
            time.sleep(.005)

    def stop_requested(self, attempt_id):
        stop = android_models.read_json(self.job / 'model-attempt-stop.json')
        return bool(stop and stop.get('id') == attempt_id)

    def attempt(self, request, action):
        if action == 'absent':  # the setup screen is not in front: nothing starts
            return
        pid = next(PIDS)
        status = {'schema': 1, 'id': request['id'], 'pid': pid, 'state': 'running'}
        android_models.write_json(self.job / 'model-attempt-status.json', status)
        self.alive.add(pid)
        env = request['env']
        state = Path(env['WWHD_DSWEEP_STATE'])
        archives = [l for l in Path(env['WWHD_DSWEEP_ARCHIVES']).read_text().splitlines() if l]
        progress = state / 'progress.txt'
        lines = progress.read_text().splitlines() if progress.exists() else []
        done = {l[13:] for l in lines if l.startswith('archive-done ')}
        skip = set((state / 'skip.txt').read_text().splitlines()) if (state / 'skip.txt').exists() else set()
        with progress.open('a') as out, open(request['stdout'], 'ab') as stdout:
            out.write('start\nphase objects 0.0\n')
            for name in archives:
                if name in done or name in skip:
                    continue
                out.write('load %s\nmodel %s:a.bdl\n' % (name, name))
                out.flush()
                if action == ('crash', name):
                    self.alive.discard(pid)  # the game stopped at this model
                    return
                if action in ('left', 'hold') and done:
                    if action == 'left':  # the player left the progress screen
                        android_models.write_json(self.job / 'model-attempt-status.json', dict(status, state='left'))
                    while not self.stop_requested(request['id']) and not self.quit.is_set():
                        time.sleep(.005)
                    self.alive.discard(pid)  # an orderly quit (Settings > Quit Game)
                    return
                out.write('archive-done %s\n' % name)
                done.add(name)
                stdout.write(b'WWHD-PROGRESS %d %d %s\n' % (len(done), len(archives), name.encode()))
                stdout.flush()
            out.write('sweep-done\n')
        self.alive.discard(pid)


class AndroidModelsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        base = Path(self.temp.name)
        self.job, self.game, self.root = base / 'job', base / 'game', base / 'shader-prebuild'
        self.job.mkdir()
        objects = self.game / 'content/Common/Object'
        objects.mkdir(parents=True)
        for name in ('Aa', 'Bb', 'Cc'):
            (objects / (name + '.szs')).write_bytes(b'')
        (self.root / 'generation-test').mkdir(parents=True)
        (self.root / 'generation-test/headstart.bin').write_bytes(b'head start')
        self.identity = {'schema': 1, 'renderer': 'vulkan', 'game': {'code/cking.rpx': [1, 2]}}
        (self.root / 'active.json').write_text(json.dumps(
            {'schema': 1, 'status': 'ready', 'generation': 'generation-test', 'identity': self.identity}))

    def tearDown(self):
        self.temp.cleanup()

    def sweep(self, script, launch_seconds=5.0, before=None):
        activity = FakeActivity(self.job, script)
        launcher = android_models.Launcher(self.job, alive=lambda pid: pid in activity.alive,
                                           launch_seconds=launch_seconds, quit_seconds=5)
        activity.start()
        if before:
            before(activity)
        logs = []

        def fast(*args, **kwargs):
            return model_sweep.run(*args, sleep=lambda s: time.sleep(.005), **kwargs)
        try:
            result = android_models.run(self.job, self.game, self.root, launcher=launcher, sweeper=fast,
                                        log=logs.append)
        finally:
            activity.quit.set()
            activity.join(5)
        self.assertEqual(android_models.read_json(self.job / 'models-result.json'), result)
        return result, activity, logs

    def state(self, name):
        path = model_sweep.state_dir(self.root, 'vulkan') / name
        return path.read_text().splitlines() if path.exists() else []

    def test_attempts_resume_skip_what_stopped_the_game_and_finish(self):
        result, activity, logs = self.sweep([('crash', 'Bb'), 'done'])
        self.assertEqual(result, {'schema': 1, 'status': 'done'})
        self.assertEqual(self.state('skip.txt'), ['Bb:a.bdl'])
        self.assertEqual(len(activity.requests), 2)
        request = activity.requests[0]
        # the game's options only, hidden and muted, the generation's live caches, its own folder and output
        self.assertTrue(all(k.startswith('WWHD_') for k in request['env']))
        for key, value in (('WWHD_HIDDEN_WINDOWS', '1'), ('WWHD_NO_AUDIO', '1'), ('WWHD_DSWEEP_EXIT', '1'),
                           ('WWHD_RENDERER_RUNTIME', 'vulkan'), ('WWHD_HEADSTART', '0')):
            self.assertEqual(request['env'][key], value)
        self.assertEqual(request['env']['WWHD_VK_PIPELINE_CACHE_DIR'], str(self.root / 'live'))
        self.assertEqual(request['args'][:2], ['--game', str(self.game)])
        self.assertTrue(request['cwd'].startswith(str(self.job / 'model-scratch')))
        self.assertTrue(request['stdout'].startswith(request['cwd']))
        progress = android_models.read_json(self.job / 'models-progress.json')
        self.assertEqual((progress['done'], progress['total'], progress['fraction']), (3, 3, 1.0))  # Bb without its model
        self.assertFalse((self.job / 'model-attempt.json').exists())
        self.assertFalse((self.job / 'model-scratch').exists())
        # done: a later run has nothing to do
        again, activity, _ = self.sweep([])
        self.assertEqual(again['status'], 'done')
        self.assertEqual(activity.requests, [])

    def test_unfinished_cache_writes_go_before_each_attempt(self):
        live = self.root / 'live'
        (live / 'vulkan-shaders').mkdir(parents=True)
        kept = ['vk-3fddc0632cf6.bin', 'vk-3fddc0632cf6.bin.recipes', 'vulkan-shaders/spirv.bin', 'notes.t1234567',
                'vk-3fddc0632cf6.bin.tzzzzzzzz']
        staged = ['vk-3fddc0632cf6.bin.recipes.t99fd27d0', 'vulkan-shaders/spirv.bin.t0a1b2c3d',
                  'vk-3fddc0632cf6.bin.tmp-123-456-7']
        for name in kept + staged:
            (live / name).write_bytes(b'x')

        def written_mid_attempt(activity):  # an attempt that stops mid-write leaves one for the next attempt
            (live / 'shaders.bin.tdeadbeef').write_bytes(b'x')
        result, activity, logs = self.sweep([('crash', 'Bb'), 'done'], before=written_mid_attempt)
        self.assertEqual(result['status'], 'done')
        self.assertEqual(sorted(str(p.relative_to(live)) for p in live.rglob('*') if p.is_file()), sorted(kept))
        self.assertEqual(sum('a write the game did not finish' in l for l in logs), 4)

    def test_no_setup_screen_pauses_without_skipping(self):
        result, activity, _ = self.sweep(['absent'], launch_seconds=.3)
        self.assertEqual(result, {'schema': 1, 'status': 'paused', 'reason': 'screen'})
        self.assertEqual(self.state('skip.txt'), [])
        self.assertFalse((self.job / 'model-attempt.json').exists())  # never started later
        # shown again: it goes on where it was
        result, _, _ = self.sweep(['done'])
        self.assertEqual(result['status'], 'done')

    def test_leaving_the_progress_screen_stops_in_order_and_keeps_the_progress(self):
        result, activity, _ = self.sweep(['left'])
        self.assertEqual(result, {'schema': 1, 'status': 'paused', 'reason': 'left'})
        self.assertEqual(android_models.read_json(self.job / 'model-attempt-stop.json')['id'],
                         activity.requests[0]['id'])
        self.assertEqual(self.state('skip.txt'), [])
        self.assertIn('archive-done Aa', self.state('progress.txt'))
        result, activity, _ = self.sweep(['done'])
        self.assertEqual(result['status'], 'done')
        self.assertEqual(self.state('progress.txt').count('load Aa'), 1)  # not drawn again

    def test_cancel_stops_in_order(self):
        def cancel_later(activity):
            def press():
                while not (self.job / 'model-attempt-status.json').exists():
                    time.sleep(.005)
                time.sleep(.05)
                android_models.write_json(self.job / 'models-cancel.json', {'schema': 1})
            threading.Thread(target=press, daemon=True).start()
        result, activity, _ = self.sweep(['hold'], before=cancel_later)
        self.assertEqual(result, {'schema': 1, 'status': 'paused', 'reason': 'cancel'})
        self.assertTrue((self.job / 'model-attempt-stop.json').exists())
        self.assertEqual(self.state('skip.txt'), [])

    def test_heat_pause_stops_in_order(self):
        def heat_later(activity):
            def heat():
                while not (self.job / 'model-attempt-status.json').exists():
                    time.sleep(.005)
                time.sleep(.05)
                android_models.write_json(self.job / 'pause.json', {'schema': 1, 'reason': 'heat'})
            threading.Thread(target=heat, daemon=True).start()
        result, _, _ = self.sweep(['hold'], before=heat_later)
        self.assertEqual(result, {'schema': 1, 'status': 'paused', 'reason': 'pause'})
        self.assertEqual(self.state('skip.txt'), [])

    def test_without_prepared_shaders_the_step_fails(self):
        (self.root / 'active.json').write_text(json.dumps({'schema': 1, 'status': 'warm_pending',
                                                           'generation': 'generation-test', 'identity': {}}))
        result, activity, _ = self.sweep([])
        self.assertEqual(result['status'], 'failed')
        self.assertEqual(activity.requests, [])


class LogTest(unittest.TestCase):
    def test_the_step_log_stays_in_the_job_and_rotates(self):
        with tempfile.TemporaryDirectory() as temp:
            log = android_models.job_log(temp)
            log('first')
            log('second\n')
            self.assertEqual((Path(temp) / 'models.log').read_text().splitlines(), ['first', 'second'])
            limit, android_models.LOG_LIMIT = android_models.LOG_LIMIT, 5
            try:
                log('third')
            finally:
                android_models.LOG_LIMIT = limit
            self.assertEqual((Path(temp) / 'models.log').read_text().splitlines(), ['third'])
            self.assertEqual((Path(temp) / 'models-previous.log').read_text().splitlines(), ['first', 'second'])


class ProgressTest(unittest.TestCase):
    def test_fraction_phase_place_and_estimate(self):
        with tempfile.TemporaryDirectory() as temp:
            job, state = Path(temp), Path(temp) / 'state'
            state.mkdir()
            (state / 'visits.txt').write_text('sea 44\nM_NewD2 0\n')
            (state / 'progress.txt').write_text('start\nphase objects 0.0\nload Aa\n')
            now = [100.0]
            progress = android_models.Progress(job, state, clock=lambda: now[0], every=0)
            self.assertTrue(progress.line('WWHD-PROGRESS 10 100 Aa'))
            self.assertFalse(progress.line('[dsweep] batch 1'))
            value = android_models.read_json(job / 'models-progress.json')
            self.assertEqual((value['phase'], value['place'], value['eta_seconds']), ('objects', '', None))
            self.assertAlmostEqual(value['fraction'], 0.06)  # 60 % of the bar for the models, 40 % for the visits
            now[0] += 60
            progress.line('WWHD-PROGRESS 100 100 Zz')
            value = android_models.read_json(job / 'models-progress.json')
            self.assertAlmostEqual(value['fraction'], 0.6)
            self.assertEqual(value['eta_seconds'], 44)  # 0.54 of the bar in 60 s: 0.4 left
            with (state / 'progress.txt').open('a') as out:
                out.write('phase visits 1.0\nvisit sea:44:-1\nvisit-done visit-sea:44:-1\nvisit M_NewD2:0:-1\n')
            progress.update(force=True)
            value = android_models.read_json(job / 'models-progress.json')
            self.assertEqual((value['phase'], value['place']), ('visits', 'M_NewD2'))
            self.assertAlmostEqual(value['fraction'], 0.8)
            with (state / 'progress.txt').open('a') as out:
                out.write('sweep-done\n')
            progress.update(force=True)
            self.assertEqual(android_models.read_json(job / 'models-progress.json')['fraction'], 1.0)


if __name__ == '__main__':
    unittest.main()
