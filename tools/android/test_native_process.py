"""Exercise streaming/backpressure and cancellation at the installer boundary."""
import threading
import unittest

from native_process import ProcessBridge


class ProcessTests(unittest.TestCase):
    def process(self, runner, cancel=lambda: False):
        bridge = ProcessBridge(runner, cancel)
        return bridge.Popen(["/extractor"], stdin=bridge.PIPE,
                            stdout=bridge.PIPE, stderr=bridge.PIPE)

    def test_keys_are_private_and_streams_are_drained_concurrently(self):
        def runner(command, **options):
            self.assertEqual(options["input"], b"synthetic keys\n")
            for _ in range(40):
                options["on_output"]("stderr", b"diagnostic\n")
                options["on_output"]("stdout", b"progress 1")
                options["on_output"]("stdout", b" 2\n")
            return 0, b"", b""
        child = self.process(runner)
        errors = []
        reader = threading.Thread(target=lambda: errors.append(child.stderr.read()))
        reader.start()
        child.stdin.write(b"synthetic keys\n")
        child.stdin.close()
        self.assertEqual(list(child.stdout), [b"progress 1 2\n"] * 40)
        self.assertEqual(child.wait(), 0)
        reader.join(2)
        self.assertFalse(reader.is_alive())
        self.assertEqual(errors, [b"diagnostic\n" * 40])

    def test_abandoned_progress_cancels_and_reaps_worker(self):
        def runner(command, **options):
            while True:
                options["on_output"]("stdout", b"progress 1 2\n")
        child = self.process(runner)
        reader = threading.Thread(target=child.stderr.read)
        reader.start()
        child.stdin.close()
        lines = iter(child.stdout)
        self.assertEqual(next(lines), b"progress 1 2\n")
        lines.close()
        with self.assertRaises(InterruptedError):
            child.wait()
        reader.join(2)
        self.assertFalse(child.thread.is_alive())
        self.assertFalse(reader.is_alive())

    def test_cancel_with_full_unread_stderr_reaps_worker(self):
        filled = threading.Event()
        def runner(command, **options):
            options["on_output"]("stdout", b"progress 1 2\n")
            for _ in range(16):
                options["on_output"]("stderr", b"diagnostic\n")
            filled.set()
            options["on_output"]("stderr", b"blocked diagnostic\n")
            return 0, b"", b""
        child = self.process(runner)
        child.stdin.close()
        lines = iter(child.stdout)
        self.assertEqual(next(lines), b"progress 1 2\n")
        self.assertTrue(filled.wait(2))
        closer = threading.Thread(target=lines.close)
        closer.start()
        closer.join(2)
        stuck = closer.is_alive()
        # Clean up even if a regression blocks completion on the full pipe.
        diagnostics = child.stderr.read()
        closer.join(2)
        self.assertFalse(stuck, "cancellation waited for an abandoned stderr reader")
        self.assertFalse(child.thread.is_alive())
        self.assertEqual(diagnostics, b"diagnostic\n" * 16)
        with self.assertRaises(InterruptedError):
            child.wait()

    def test_failure_reaches_stdout_reader_and_wait(self):
        def runner(command, **options):
            raise OSError("synthetic spawn failure")
        child = self.process(runner)
        reader = threading.Thread(target=child.stderr.read)
        reader.start()
        child.stdin.close()
        with self.assertRaisesRegex(OSError, "spawn failure"):
            list(child.stdout)
        with self.assertRaises(OSError):
            child.wait()
        reader.join(2)
        self.assertFalse(reader.is_alive())

    def test_key_input_is_bounded_before_starting_a_child(self):
        child = self.process(lambda *args, **kwargs: self.fail("must not start"))
        with self.assertRaises(ValueError):
            child.stdin.write(b"x" * 4097)
        # BytesIO finalization must not launch a child after rejected input.
        child.stdin.owner.start = lambda data: None
        child.stdin.close()

    def test_installed_library_environment_reaches_both_information_and_extraction(self):
        environment = {"LD_LIBRARY_PATH": "/installed/native"}
        def runner(command, **options):
            self.assertEqual(options["env"], environment)
            return 0, b"", b""
        bridge = ProcessBridge(runner, env=environment)
        self.assertEqual(bridge.run(["/extractor"], stdout=-1, stderr=-1, stdin=-3).returncode, 0)
        child = bridge.Popen(["/extractor"], stdin=-1, stdout=-1, stderr=-1)
        reader = threading.Thread(target=child.stderr.read)
        reader.start()
        child.stdin.close()
        self.assertEqual(list(child.stdout), [])
        self.assertEqual(child.wait(), 0)
        reader.join(2)
        self.assertFalse(reader.is_alive())
