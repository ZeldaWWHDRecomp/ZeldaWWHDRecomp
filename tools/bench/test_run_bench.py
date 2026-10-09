"""Benchmark orchestration regressions, using a fake process and no game files."""
import pathlib
import tempfile
import types
import unittest
from unittest.mock import patch
import run_bench as bench


class BenchmarkTests(unittest.TestCase):
    def test_invalid_warmup_stops_before_measured_runs(self):
        with tempfile.TemporaryDirectory() as directory:
            argv = ['run_bench.py', '--binary', '/unused/game', '--state-dir', '/unused/states',
                    '--warmup', '--runs', '10', '--out', directory]
            with patch.object(bench.sys, 'argv', argv), patch.object(bench.os.path, 'exists', return_value=True), \
                 patch.object(bench, 'run_once', return_value={'status': 'load exceeded limit'}) as run:
                with self.assertRaises(SystemExit) as stopped:
                    bench.main()
                self.assertEqual(stopped.exception.code, 1)
                self.assertEqual(run.call_count, 1)
                self.assertEqual(run.call_args.args[1], 'warmup')

    def run_fake(self, root, complete, phase=None, profile=True):
        (root / 'source').mkdir()
        args = types.SimpleNamespace(binary=str(root / 'base'), variant_binaries={'new': str(root / 'new')},
            save=str(root / 'source'), game='/unused/game', state_dir='/unused/states', scene='still', slot=1,
            load_frame=450, origin=650, cache_dir=None, press_from=120, press_every=30, renderer='metal',
            fps='30', visible=False, display_hz=0, uncapped=False, gate=None, wait_for_others=True,
            watch_others=True, max_load=12, seconds=60, timeout=100, skip_windows=0,
            quiet_load_max=None, exclusive_bench=False, exclusive_work=False, min_free_gb=0)
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
        self.assertEqual(launched, [bench.os.path.abspath(root / 'new')])
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
        self.assertEqual(result['binary'], bench.os.path.abspath(pathlib.Path(directory) / 'new'))
"""Benchmark statistics and profiler extraction tests; no game input."""
import unittest
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest import mock
from run_bench import benchmark_pids, logic_cpu_samples, quiet_reasons, run_statistics, worker_pids
import run_bench


class BenchmarkStatistics(unittest.TestCase):
    def test_pair_differences_match_pair_numbers_in_ab_ba_order(self):
        def sample(name, pair, value, status='ok'):
            return {'variant': name, 'run': pair, 'status': status,
                    'summary': {'frame_ms': value, 'logic_cpu_ms': value / 2}}
        results = [sample('base', 1, 10), sample('hooks', 1, 12),
                   sample('hooks', 2, 8), sample('base', 2, 10),
                   sample('base', 3, 10), sample('hooks', 3, 14),
                   sample('base', 4, 10), sample('hooks', 4, 99, 'disturbed')]
        report = run_bench.paired_statistics(results, ['base', 'hooks'])['metrics']['frame_ms']
        self.assertEqual([p['difference_ms'] for p in report['pairs']], [2, -2, 4])
        self.assertEqual(report['difference_ms']['median'], 2)
        self.assertEqual(report['difference_ms']['iqr'], 3)
        self.assertEqual(report['difference_ms']['n'], 3)
        self.assertTrue(report['run_iqr_exceeds_median_difference'])
        self.assertTrue(report['paired_iqr_exceeds_paired_median'])

    def test_worker_detection_checks_programs_not_shell_command_text(self):
        listing = '''101 /usr/bin/python3 /repo/tools/bench/run_bench.py
102 /usr/bin/ninja -j 2
103 /usr/bin/clang++ -c file.cpp
104 /usr/bin/cmake --build build
105 /usr/bin/cmake --version
106 /usr/bin/clangd --background-index
107 /bin/zsh -c 'ninja -j 2; python smoke.py'
108 /Applications/Python.app/Contents/MacOS/Python -u /other/run_smoke.py
109 /usr/bin/python3 -m unittest
110 /usr/bin/python3 /repo/build/sdk2-perf/wait_and_run.py
'''
        self.assertEqual(worker_pids(listing, 101), [102, 103, 104, 108, 109])

    def test_ten_run_inclusive_quartiles(self):
        result = run_statistics(list(range(1, 11)))
        self.assertEqual(result['median'], 5.5)
        self.assertEqual((result['q1'], result['q3'], result['iqr']), (3.25, 7.75, 4.5))
        self.assertEqual(result['n'], 10)

    def test_benchmark_detection_excludes_self_and_shell_wrappers(self):
        listing = """101 /usr/bin/python3 /repo/tools/bench/run_bench.py --runs 10
102 /usr/bin/python3 /other/run_bench.py --runs 2
103 /bin/zsh -lc 'python3 /repo/tools/bench/run_bench.py'
104 /usr/bin/python3 /repo/tools/bench/test_run_bench.py
105 /usr/bin/wwhd --game /game
106 /Applications/Python.app/Contents/MacOS/Python /other/run_bench.py --runs 10
"""
        self.assertEqual(benchmark_pids(listing, 101), [102, 106])

    def test_quiet_gate_checks_strict_load_and_disk_reserve(self):
        args = SimpleNamespace(quiet_load_max=12, exclusive_bench=False, min_free_gb=15, out='build')
        with mock.patch('run_bench.load1', return_value=12), mock.patch('run_bench.shutil.disk_usage', return_value=SimpleNamespace(free=14e9)):
            reasons = quiet_reasons(args)
            self.assertEqual(len(reasons), 2)
        with mock.patch('run_bench.load1', return_value=11.99), mock.patch('run_bench.shutil.disk_usage', return_value=SimpleNamespace(free=16e9)):
            self.assertEqual(quiet_reasons(args), [])

    @unittest.skipIf(os.name == 'nt', 'fixture executables use POSIX shebangs')
    def test_cli_runs_distinct_binaries_and_records_logic_cpu(self):
        repo = Path(__file__).resolve().parents[2]
        (repo / 'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='bench-fixture-', dir=repo / 'build') as directory:
            root = Path(directory)
            for name in ['save', 'states', 'game']:
                (root / name).mkdir()
            (root / 'states/slot1.bin').touch()  # synthetic empty fixture, never loaded by a game
            executables = []
            for name, frame, cpu in [('a', 6, 4), ('b', 8, 5)]:
                executable = root / name
                log = '[fixture] Loaded slot 1\n'
                log += f'[prof] frame 120: 120 frames (0 hold), {frame}.00 ms/frame, 150.0 swaps/s, 150.0 logic steps/s; render thread CPU 3.00 ms/frame, in ops 2.00 ms/frame, idle (waiting for commands) 1.00 ms/frame\n'
                log += (f'[interp] main thread CPU per pass: logic {cpu}.00 ms\n' * 3)
                executable.write_text('#!' + sys.executable + '\nimport os\n'
                                      'assert os.environ["WWHD_UNCAPPED"] == "1"\n'
                                      'assert os.environ["WWHD_RENDERER_RUNTIME"] == "metal"\n'
                                      'open("test_done", "w").close()\n'
                                      'print(' + repr(log) + ')\n')
                executable.chmod(0o755)
                executables.append(executable)
            command = [sys.executable, str(repo / 'tools/bench/run_bench.py'),
                       '--binary', str(executables[0]), '--variant', 'a:', '--variant', 'b:',
                       '--variant-binary', 'b=' + str(executables[1]),
                       '--save', str(root / 'save'), '--state-dir', str(root / 'states'),
                       '--game', str(root / 'game'), '--scene', 'still', '--runs', '1',
                       '--renderer', 'metal', '--uncapped',
                       '--out', str(root / 'out'), '--skip-windows', '0', '--no-wait', '--no-watch']
            result = subprocess.run(command, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads((root / 'out/summary.json').read_text())
            self.assertEqual([run['status'] for run in report['runs']], ['ok', 'ok'])
            self.assertEqual([Path(run['binary']).name for run in report['runs']], ['a', 'b'])
            self.assertEqual(report['variants']['a']['frame_ms']['median'], 6)
            self.assertEqual(report['variants']['b']['logic_cpu_ms']['median'], 5)

    def test_single_run_has_zero_spread(self):
        result = run_statistics([4.0])
        self.assertEqual(result['iqr'], 0)
        self.assertEqual(result['stdev'], 0)

    def test_actual_logic_cpu_is_distinct_from_throughput(self):
        lines = ['[interp] 150.0 logic steps/s; main thread CPU per pass: logic 5.25 ms, blended hold 2.00 ms',
                 '[prof] frame 120: 120 frames, 6.00 ms/frame',
                 '[interp] 140.0 logic steps/s; main thread CPU per pass: logic 5.75 ms, blended hold 1.00 ms']
        self.assertEqual(logic_cpu_samples(lines), [5.25, 5.75])

    def test_disturbed_retries_preserve_all_fifteen_interleaved_samples(self):
        repo = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory(prefix='bench-retry-', dir=repo / 'build') as directory:
            root = Path(directory)
            (root / 'slot1.bin').touch()  # synthetic fixture
            attempts, accepted = {}, []

            def sample(args, name, env, index, out):
                key = name, index
                attempts[key] = attempts.get(key, 0) + 1
                disturbed = key == ('a', 1) and attempts[key] <= 4
                if not disturbed:
                    accepted.append(key)
                return {'variant': name, 'run': index, 'status': 'disturbed' if disturbed else 'ok',
                        'windows': 3, 'load_before': 11, 'load_after': 13 if disturbed else 11,
                        'summary': {'frame_ms': 6, 'logic_cpu_ms': 4}}

            argv = ['run_bench.py', '--binary', 'fixture', '--state-dir', str(root),
                    '--scene', 'still', '--variant', 'a:', '--variant', 'b:', '--runs', '15',
                    '--warmup', '--retry-disturbed', '--out', str(root / 'out')]
            with mock.patch.object(sys, 'argv', argv), mock.patch('run_bench.run_once', side_effect=sample), contextlib.redirect_stdout(io.StringIO()):
                run_bench.main()
            report = json.loads((root / 'out/summary.json').read_text())
            expected = [('warmup', 0)]
            for i in range(1, 16):
                expected.extend((name, i) for name in (('a', 'b') if i % 2 else ('b', 'a')))
            self.assertEqual(accepted, expected)
            self.assertEqual(len(report['runs']), 30)
            self.assertTrue(all(r['status'] == 'ok' for r in report['runs']))
            for name in ('a', 'b'):
                for metric in ('frame_ms', 'logic_cpu_ms'):
                    self.assertEqual(report['variants'][name][metric]['n'], 15)

    def test_other_failures_and_default_retries_remain_bounded(self):
        for retry_disturbed, status in [(False, 'disturbed'), (True, 'timeout')]:
            result = {'status': status, 'windows': 0, 'load_before': 11, 'load_after': 11, 'summary': {}}
            with mock.patch('run_bench.run_once', return_value=result) as sample, contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(run_bench.run_with_retries(SimpleNamespace(retry_disturbed=retry_disturbed), 'a', {}, 1, 'fixture'), result)
                self.assertEqual(sample.call_count, 3)


if __name__ == '__main__':
    unittest.main()
