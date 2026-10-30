"""Known-distribution trace fixture exercises gameplay bounds and compile attribution."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest

from analyze_performance import analyze


class PerformanceAnalysisTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.trace = self.root / 'known.csv'
        # Ten gameplay intervals: 10,20,...100 ms. Boot/menu intervals at the
        # excluded start boundary and after the end must not change their quantiles.
        self.trace.write_text('''kind,frame,key,start_ns,end_ns,thread,result,cpu_ns
frame,100,0,0,5000000000,1,0,-1
frame,101,0,5000000000,5010000000,1,0,-1
frame,102,0,5010000000,5030000000,1,0,-1
frame,103,0,5030000000,5060000000,1,0,-1
frame,104,0,5060000000,5100000000,1,0,-1
frame,105,0,5100000000,5150000000,1,0,-1
frame,106,0,5150000000,5210000000,1,0,-1
frame,107,0,5210000000,5280000000,1,0,-1
frame,108,0,5280000000,5360000000,1,0,-1
frame,109,0,5360000000,5450000000,1,0,-1
frame,110,0,5450000000,5550000000,1,0,-1
frame,111,0,5550000000,9550000000,1,0,-1
pipeline_wait,100,aaaa,0,1000000000,1,0,-1
pipeline_wait,105,aaaa,5100000000,5110000000,1,0,-1
pipeline_wait,105,bbbb,5110000000,5140000000,2,1,1000000
shader_translation,105,cccc,5100000000,5108000000,3,0,3500000
shader_pending,111,cccc,5550000000,8550000000,3,-1,-1
''', encoding='utf-8')
        self.report = self.root / 'native.txt'
        self.report.write_text('''Wind Waker HD test
Output FPS 29.7 (average 28.8), render interval 33.67 ms (worst 88.00), average logic steps/s 29.2
Game CPU: 12.00 ms (avg 11.50)
Render CPU: 20.00 ms (avg 19.25)
GPU interval: n/a
Dynamic resolution: 0.75x of 1x (3D view 960x540), down to 0.5x, GPU busy 70%, 2 changes
Shader compile: fast (setting).
''', encoding='utf-8')

    def test_distribution_excludes_boundaries_and_attributes_compilation(self):
        result = analyze(self.trace, 100, 110, [self.report], 'known')
        frame = result['frame_intervals']
        self.assertEqual(frame['count'], 10)
        self.assertEqual(frame['total_ms'], 550)
        self.assertEqual((frame['p50_ms'], frame['p95_ms'], frame['p99_ms']), (50, 100, 100))
        self.assertEqual((frame['intervals_ge_50ms'], frame['intervals_ge_100ms']), (6, 1))
        self.assertAlmostEqual(frame['updated_frames_per_second'], 200/11)
        self.assertEqual(result['window']['first_observed_frame'], 101)
        self.assertEqual(result['window']['last_observed_frame'], 110)
        pipeline = result['events_by_kind']['pipeline_wait']
        self.assertEqual((pipeline['count'], pipeline['total_ms'], pipeline['max_ms']), (2, 40, 30))
        self.assertEqual((pipeline['unique_keys'], pipeline['nonzero_results']), (2, 1))
        self.assertNotIn('shader_pending', result['events_by_kind'])
        self.assertEqual(result['events_by_kind']['shader_translation']['cpu']['total_ms'], 3.5)
        report = result['reports'][0]['metrics']
        self.assertEqual(report['average_logic_steps_per_second'], 29.2)
        self.assertEqual(report['game_cpu']['average_ms'], 11.5)
        self.assertEqual(report['render_cpu']['current_ms'], 20)
        self.assertNotIn('gpu_interval', report)

    def test_cli_writes_json_and_rejects_empty_window(self):
        output = self.root / 'result.json'
        run = subprocess.run([sys.executable, str(Path(__file__).with_name('analyze_performance.py')),
            str(self.trace), '--start-frame', '100', '--end-frame', '110',
            '--report', str(self.report), '--output', str(output)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(json.loads(output.read_text(encoding='utf-8'))['frame_intervals']['p50_ms'], 50)
        with self.assertRaisesRegex(ValueError, 'No frame events'):
            analyze(self.trace, 200, 300)

    def test_corrupt_trace_fails_instead_of_producing_benchmark(self):
        self.trace.write_text('kind,frame\nframe,101\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'CSV columns'):
            analyze(self.trace, 100, 110)


if __name__ == '__main__':
    unittest.main()
