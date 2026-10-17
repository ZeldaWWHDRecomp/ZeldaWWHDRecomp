"""Idle-gate process recognition, with synthetic ps fixtures only."""
import unittest
from full_state_memory_bench import benchmark_processes


class ProcessGateTests(unittest.TestCase):
    def test_python_drivers_and_native_benchmark(self):
        fixture = """11 /usr/bin/python3 /tmp/run_bench.py --out /tmp/result
12 /Frameworks/Python.framework/Python /tmp/full_state_memory_bench.py /tmp/bench
13 /tmp/full_state_memory_bench save
14 /opt/Python3 /tmp/run_bench.py --out /tmp/result
"""
        self.assertEqual(benchmark_processes(fixture, 99), [11, 12, 13, 14])

    def test_self_unrelated_arguments_and_quoted_paths(self):
        fixture = """21 /usr/bin/python3 /tmp/full_state_memory_bench.py
22 /usr/bin/python3 /tmp/unrelated.py
23 /bin/echo /tmp/run_bench.py
24 '/path with space/Python' '/path with space/run_bench.py'
25 /bin/echo 'unterminated
"""
        self.assertEqual(benchmark_processes(fixture, 21), [24])


if __name__ == '__main__':
    unittest.main()
