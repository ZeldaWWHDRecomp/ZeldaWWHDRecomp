"""Extraction fault checks through the unchanged installer's pipe protocol."""
from pathlib import Path
import tempfile
import unittest

from setup_adapter import Adapter, Paused, digest, inventory

PACKAGE = Path(__file__).resolve().parents[2]


class ExtractionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.image = self.root / "synthetic.bin"
        self.image.write_bytes(b"authored fixture identity")
        self.calls, self.events = [], []
        self.rpx = self.root / "synthetic.rpx"
        self.rpx.write_bytes(b"authored synthetic executable")

    def adapter(self, pause=lambda: False):
        adapter = Adapter(PACKAGE, self.root / "jobs", "port-v1", self.events.append, pause)
        adapter.setup.SUPPORTED_RPX_SHA256 = digest(self.rpx)
        return adapter

    def runner(self, command, **options):
        self.calls.append(command)
        if "info" in command:
            return 0, (b"format wua\nselected 0005000010143500_v0\n"
                       b"version 0\nbytes 100\n"
                       b"title 0005000010143500 0 0005000010143500_v0 3 100\n"), b""
        out = Path(command[-1])
        (out / "code").mkdir(parents=True)
        (out / "code/cking.rpx").write_bytes(self.rpx.read_bytes())
        (out / "content").mkdir()
        (out / "content/fixture.bin").write_bytes(b"authored content")
        (out / "meta").mkdir()
        (out / "meta/meta.xml").write_text("<menu/>")
        options["on_output"]("stdout", b"phase extract\nprogress 100 100\n")
        return 0, b"", b""

    def extract(self, adapter=None, runner=None):
        return (adapter or self.adapter()).extract(
            self.image, "archive", "/synthetic-extractor", "test-identity", runner or self.runner)

    def test_pause_during_dump_checksum_stops_before_native_extraction(self):
        self.image.write_bytes(b"authored dump bytes" * 200000)
        checks = 0
        def pause():
            nonlocal checks
            checks += 1
            return checks >= 4
        with self.assertRaises(Paused):
            self.extract(self.adapter(pause))
        self.assertEqual(self.calls, [])
        self.assertFalse(list((self.root / "jobs").iterdir()))
        self.assertEqual(self.events[-1]["state"], "paused")
        # Retry hashes the same input and completes the unchanged extractor path.
        game = self.extract()
        self.assertEqual((game / "code/cking.rpx").read_bytes(), self.rpx.read_bytes())

    def test_verified_checkpoint_reused_and_corruption_rebuilt(self):
        game = self.extract()
        expected = inventory(game)
        self.assertEqual(game, self.extract(runner=lambda *a, **k: self.fail("checkpoint not reused")))
        (game / "content/fixture.bin").write_bytes(b"corrupt")
        self.assertEqual(game, self.extract())
        self.assertEqual(expected, inventory(game))
        self.assertEqual(len(self.calls), 4)

    def test_failed_new_input_preserves_completed_generation(self):
        game = self.extract()
        expected = inventory(game)
        self.image.write_bytes(b"different authored input")
        def fail(*args, **kwargs):
            raise OSError("synthetic tool failure")
        with self.assertRaises(OSError):
            self.extract(runner=fail)
        self.assertEqual(expected, inventory(game))
        self.assertFalse(list((self.root / "jobs").rglob("pending-*")))

    def test_insufficient_extraction_space_keeps_previous_generation(self):
        previous = self.extract()
        expected = inventory(previous)
        self.image.write_bytes(b"new authored input")
        adapter = self.adapter()
        adapter.setup.free_space = lambda path: 0
        with self.assertRaisesRegex(adapter.setup.SetupError, "not enough free disk space"):
            self.extract(adapter)
        self.assertEqual(inventory(previous), expected)
        self.assertEqual(len(self.calls), 3)  # Two original commands; only info for failed input.
        self.assertFalse(list((self.root / "jobs").rglob("pending-*")))

    def test_pause_during_native_execution_keeps_no_partial_checkpoint(self):
        paused = [False]
        def runner(command, **options):
            if "info" in command:
                return self.runner(command, **options)
            paused[0] = True
            options["on_output"]("stdout", b"phase extract\n")
        with self.assertRaises(Paused):
            self.extract(self.adapter(lambda: paused[0]), runner)
        self.assertFalse(list((self.root / "jobs").rglob("pending-*")))
        self.assertFalse(list((self.root / "jobs").rglob("complete.json")))
        self.assertTrue(self.extract().is_dir())

    def test_log_open_failure_restores_installer_hooks(self):
        adapter = self.adapter()
        original = adapter.setup.subprocess, adapter.setup.extractor, adapter.setup.GUI, adapter.setup.LOG
        def fail(*args):
            raise OSError("synthetic log failure")
        adapter.setup.Log.open = fail
        with self.assertRaises(OSError):
            self.extract(adapter)
        self.assertEqual(original, (adapter.setup.subprocess, adapter.setup.extractor,
                                    adapter.setup.GUI, adapter.setup.LOG))
        self.assertFalse(list((self.root / "jobs").rglob("pending-*")))
