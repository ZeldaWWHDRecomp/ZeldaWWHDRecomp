"""Synthetic shader build and console launcher from a real Windows SMB share."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import shader_prebuild as prebuild

PACKAGE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PACKAGE / 'tools'))
import shaderprep


class GenerationFolders(unittest.TestCase):
    def test_inherits_permissions_and_retries_collision(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            collision = root / 'generation-aaaaaaaa'
            collision.mkdir()
            with patch.object(prebuild.secrets, 'token_hex', side_effect=['aaaaaaaa', 'bbbbbbbb']), \
                 patch.object(prebuild.os, 'mkdir', wraps=os.mkdir) as mkdir:
                result = prebuild.create_generation(root)
            self.assertEqual(result, root / 'generation-bbbbbbbb')
            self.assertEqual(mkdir.call_count, 2)
            for call in mkdir.call_args_list:
                self.assertEqual(len(call.args), 1)  # no mode=0o700, including on Windows
                self.assertEqual(call.kwargs, {})
            (result / 'headstart.bin').write_bytes(b'synthetic')
            self.assertEqual(list(collision.iterdir()), [])

    def test_permission_error_is_not_retried(self):
        with patch.object(prebuild.os, 'mkdir', side_effect=PermissionError('denied')) as mkdir:
            with self.assertRaises(PermissionError):
                prebuild.create_generation('unused')
            mkdir.assert_called_once()

    def test_collision_retries_are_bounded(self):
        with patch.object(prebuild.os, 'mkdir', side_effect=FileExistsError()) as mkdir:
            with self.assertRaises(FileExistsError):
                prebuild.create_generation('unused')
            self.assertEqual(mkdir.call_count, 100)

    def test_unc_longpath_spelling(self):
        import ntpath
        with patch.object(prebuild.os, 'name', 'nt'), patch.object(prebuild.os, 'path', ntpath):
            unc = r'\\server\share\PC Games\shader-prebuild\generation-12345678'
            extended = r'\\?\UNC\server\share\PC Games\shader-prebuild\generation-12345678'
            self.assertEqual(prebuild.longpath(unc), extended)
            self.assertEqual(prebuild.longpath(extended), extended)

    def test_batch_maps_its_own_folder_and_restores_directory(self):
        batch = (PACKAGE / 'tools/installer/install-windows.bat').read_text().lower()
        self.assertIn('pushd "%~dp0"', batch)
        self.assertIn('popd', batch)
        self.assertNotIn('cd /d', batch)
        self.assertIn('exit /b %setup_result%', batch)

    def build_headstart(self, root):
        game = root / 'synthetic game'
        game.mkdir()
        (game / 'synthetic.sharcfb').write_bytes(b'synthetic archive identity, no shaders')
        template = root / 'template.bin'
        shaderprep.write_template(shaderprep.TemplateBuilder().document(), template)
        cache = root / 'shader-prebuild'
        result = prebuild.prepare(game, template, None, cache, PACKAGE, python=sys.executable)
        self.assertEqual(result['status'], 'warm_pending', result)
        generation = Path(result['generation'])
        self.assertTrue((generation / 'headstart.bin').is_file())
        self.assertTrue((cache / 'active.json').is_file())
        # Exercise the cleanup spelling too (\\?\UNC\ on native Windows Python).
        self.assertTrue(Path(prebuild.longpath(generation)).is_dir())
        shutil.rmtree(prebuild.longpath(generation))

    def test_real_shader_builder_local(self):
        with tempfile.TemporaryDirectory() as temporary:
            self.build_headstart(Path(temporary))


@unittest.skipUnless(sys.platform == 'win32', 'requires a real Windows SMB share')
class WindowsUNC(unittest.TestCase):
    build_headstart = GenerationFolders.build_headstart

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='wwhd-unc-')
        self.addCleanup(temporary.cleanup)
        local = Path(temporary.name).resolve()
        if not local.drive or local.drive.startswith('\\\\'):
            self.skipTest('system temporary directory is not on a local drive')
        share = Path('\\\\localhost\\' + local.drive[0] + '$', *local.parts[1:])
        # Probe only access to the admin share. Once it works, shader/launcher errors
        # are test failures, never reasons to skip or substitute a local path.
        try:
            probe = share / 'share-probe'
            probe.write_bytes(b'probe')
            probe.unlink()
        except OSError as error:
            self.skipTest('local admin SMB share unavailable: %s' % error)
        self.share = share

    def test_real_shader_builder_unc(self):
        self.build_headstart(self.share)

    def test_console_batch_unc(self):
        fixture = os.environ.get('WWHD_UNC_CONSOLE_FIXTURE')
        self.assertTrue(fixture, 'Windows CI must provide the console fixture executable')
        release = self.share / 'UNC release'
        tools = release / 'tools'
        tools.mkdir(parents=True)
        batch = tools / 'Setup in a console window.bat'
        shutil.copyfile(PACKAGE / 'tools/installer/install-windows.bat', batch)
        shutil.copyfile(fixture, release / 'Wind Waker HD.exe')
        result = subprocess.run([os.environ.get('COMSPEC', 'cmd.exe'), '/d', '/c', str(batch)],
                                cwd=os.environ['SystemRoot'], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('console fixture ok', result.stdout)
        self.assertNotIn('UNC paths are not supported', result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
