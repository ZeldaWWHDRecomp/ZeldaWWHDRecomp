"""Keep authored fixture acceptance isolated from real-user/release validation."""
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
import zipfile

from debug_fixture_build import register, synthetic_rpx
from prepare_python import source_files
from setup_adapter import Adapter

ROOT = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location('release_guard', ROOT / 'tools/release/guard.py')
guard = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(guard)


class DebugFixtureTests(unittest.TestCase):
    def test_exact_fixture_only_and_no_global_registration(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            game = root / 'game'
            (game / 'code').mkdir(parents=True)
            rpx = game / 'code/cking.rpx'
            rpx.write_bytes(synthetic_rpx())
            production = Adapter(ROOT, root / 'production', 'test')
            debug = Adapter(ROOT, root / 'debug', 'test')
            lookup = production.setup.game_builds.by_sha256
            with self.assertRaises(production.setup.SetupError):
                production.setup.check_game_version(game)
            register(debug.setup)
            self.assertEqual(debug.setup.check_game_version(game).name, 'USA')
            debug.translate(game)
            self.assertIs(production.setup.game_builds.by_sha256, lookup)
            with self.assertRaises(production.setup.SetupError):
                production.setup.check_game_version(game)
            rpx.write_bytes(synthetic_rpx() + b'changed')
            with self.assertRaises(debug.setup.SetupError):
                debug.setup.check_game_version(game)
            self.assertIs(production.setup.game_builds.by_sha256, lookup)

    def test_translator_override_restored_on_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            adapter = Adapter(ROOT, Path(temp), 'test')
            module = adapter.setup.game_builds
            lookup, hooks, files = module.by_sha256, module.read_hooks, module.hook_files
            def fail(*args):
                self.assertIsNot(module.by_sha256, lookup)
                raise RuntimeError('authored interruption')
            adapter.setup.recompile = fail
            register(adapter.setup)
            with self.assertRaisesRegex(RuntimeError, 'authored interruption'):
                adapter.setup.recompile(None, None)
            self.assertIs(module.by_sha256, lookup)
            self.assertIs(module.read_hooks, hooks)
            self.assertIs(module.hook_files, files)

    def test_release_source_inventory_and_guard(self):
        release = {str(p.relative_to(ROOT)) for p in source_files(ROOT)}
        debug = {str(p.relative_to(ROOT)) for p in source_files(ROOT, True)}
        self.assertFalse(release & guard.ANDROID_TEST_FILES)
        self.assertIn('tools/recomp/builds/eu.json', release)
        self.assertIn('tools/android/debug_fixture_build.py', debug)
        for test_resource in guard.ANDROID_TEST_FILES:
            with self.subTest(resource=test_resource), tempfile.TemporaryDirectory() as temp:
                stream = io.BytesIO()
                with zipfile.ZipFile(stream, 'w') as archive:
                    archive.writestr(test_resource, b'authored test resource')
                artifact = Path(temp) / 'app.apk'
                with zipfile.ZipFile(artifact, 'w') as archive:
                    archive.writestr('assets/python-source.zip', stream.getvalue())
                self.assertTrue(guard.scan(artifact)[0])
                self.assertFalse(guard.scan(artifact, allow_android_test_fixtures=True)[0])
