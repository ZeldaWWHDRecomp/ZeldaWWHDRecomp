"""Benchmark orchestration regressions, using a fake process and no game files."""
import pathlib
import tempfile
import types
import unittest
from unittest.mock import patch
import run_bench as bench


class BenchmarkTests(unittest.TestCase):
    def run_fake(self, root, complete, phase=None, profile=True):
        (root / 'source').mkdir()
        args = types.SimpleNamespace(binary='/unused/base', variant_binaries={'new': '/unused/new'},
            save=str(root / 'source'), game='/unused/game', state_dir='/unused/states', scene='still', slot=1,
            load_frame=450, origin=650, cache_dir=None, press_from=120, press_every=30, renderer='metal',
            fps='30', visible=False, display_hz=0, uncapped=False, gate=None, wait_for_others=True,
            watch_others=True, max_load=12, seconds=60, timeout=100, skip_windows=0)
        launched = []

        class Process:
            pid = 42
            def __init__(self, command, **kw):
                launched.append(command[0])
                if phase is not None:
                    self_test.assertEqual(phase[0], 2, 'must wait for load AND the other game to clear')
                kw['stdout'].write('[savestate] Loaded slot 1\n')
                if profile:
                    kw['stdout'].write('[prof] frame 120: 120 frames (0 hold), 33.33 ms/frame, 30.0 swaps/s, '
                        '30.0 logic steps/s; render thread CPU 1.00 ms/frame, in ops 0.50 ms/frame, '
                        'idle (waiting for commands) 0.50 ms/frame\n')
                if complete:
                    pathlib.Path(kw['cwd'], 'test_done').touch()
            def poll(self):
                return 0

        self_test = self
        games = lambda *_: ['foreign game'] if phase is not None and phase[0] == 1 else []
        load = lambda: 20.0 if phase is not None and phase[0] == 0 else 5.0
        def sleep(_):
            if phase is not None:
                phase[0] += 1

        with patch.object(bench.subprocess, 'Popen', Process), patch.object(bench, 'other_games', games), \
             patch.object(bench, 'load1', load), patch.object(bench.time, 'sleep', sleep):
            result = bench.run_once(args, 'new', {}, 1, str(root / 'out'))
        self.assertEqual(launched, ['/unused/new'])
        return result

    def test_exit_after_loading_without_completing_is_not_success(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_fake(pathlib.Path(directory), False)
        self.assertEqual(result['status'], 'exited before scenario completed')

    def test_completed_run_without_measured_windows_is_not_a_sample(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_fake(pathlib.Path(directory), True, profile=False)
        self.assertEqual(result['status'], 'no measured profiling windows')

    def test_game_starting_during_load_wait_is_checked_again(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_fake(pathlib.Path(directory), True, [0])
        self.assertEqual(result['status'], 'ok')
        self.assertEqual(result['binary'], '/unused/new')


if __name__ == '__main__':
    unittest.main()
