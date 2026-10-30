"""Validate reproducible compiler inputs and path/ABI choices without ADB."""
from pathlib import Path
import shlex
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import build_runtime_patch as builder
import runtime_patch


class RuntimePatchBuilderTest(unittest.TestCase):
    def test_archive_is_reproducible_and_excludes_unrequested_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            paths = {'runtime/include/cpu.h': b'// authored\r\n',
                     'runtime/src/mods/context_touch.h': b'// authored header\r\n',
                     'runtime/src/mods/startup_touch.h': b'// authored startup\r\n',
                     builder.SOURCE: b'// authored source\r\n',
                     'runtime/src/irrelevant.cpp': b'do not ship this translation unit'}
            for name, raw in paths.items():
                path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(raw)
            toolchain = root / 'data.zip'
            with zipfile.ZipFile(toolchain, 'w') as bundle:
                bundle.writestr('sysroot/usr/include/stdio.h', b'// pinned fixture\n')
                bundle.writestr('lib/clang/20/include/stdarg.h', b'// pinned fixture\n')
                bundle.writestr('sysroot/usr/lib/ignore.a', b'not needed for compiling one object')
            digest = runtime_patch.file_sha(toolchain)
            first, second = root / 'one.tar.gz', root / 'two.tar.gz'
            result = builder.build_archive(root, toolchain, digest, first)
            # An LF checkout and another output filename produce identical input bytes.
            (root / 'runtime/include/cpu.h').write_bytes(b'// authored\n')
            builder.build_archive(root, toolchain, digest, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(result['sha256'], runtime_patch.file_sha(first))
            with tarfile.open(first, 'r:gz') as archive:
                self.assertEqual(set(archive.getnames()), {
                    'runtime/include/cpu.h', 'runtime/src/mods/context_touch.h',
                    'runtime/src/mods/startup_touch.h', builder.SOURCE,
                    'toolchain/sysroot/usr/include/stdio.h', 'toolchain/lib/clang/20/include/stdarg.h'})
                for member in archive:
                    self.assertEqual((member.mtime, member.uid, member.gid, member.mode), (0, 0, 0, 0o644))
            with self.assertRaisesRegex(ValueError, 'pinned compiler'):
                builder.build_archive(root, toolchain, '0' * 64, root / 'bad.tar.gz')

    def test_installed_library_path_rejects_ambiguous_or_escaped_base(self):
        result = builder.native_library_directory('package:/data/app/~~token/dev.windwake.android-ab+/base.apk\n')
        self.assertEqual(result, '/data/app/~~token/dev.windwake.android-ab+/lib/arm64')
        for value in ('package:/sdcard/base.apk', 'package:/data/app/../base.apk',
                      'package:/data/app/a/base.apk\npackage:/data/app/b/base.apk',
                      'package:/data/app/a/base.apk;echo injected'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                builder.native_library_directory(value)

    def test_recipe_keeps_signed_char_abi_and_fixed_remote_prefixes(self):
        flags = builder.compiler_flags()
        self.assertIn('-fsigned-char', flags)
        self.assertIn('--target=aarch64-linux-android33', flags)
        self.assertIn('-fPIC', flags)
        self.assertIn('-ffp-contract=off', flags)
        self.assertIn('-ffile-prefix-map=' + builder.REMOTE + '=.', flags)
        self.assertIn('-fmacro-prefix-map=' + builder.REMOTE + '=.', flags)
        self.assertEqual(flags[-2:], ['-o', builder.REMOTE + '/context_touch.cpp.o'])

    def test_driver_path_with_equals_executes_directly_without_toybox_env(self):
        driver = '/data/app/~~random==/dev.windwake.android-random==/lib/arm64/libwwhd-clang-driver.so'
        adb = builder.Adb(Path('fixture-adb'), 'fixture-serial')
        with patch.object(builder.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, b'version', b'')) as run:
            builder.invoke_driver(adb, driver, '--version')
            command = run.call_args.args[0]
            self.assertEqual(command[:4], ['fixture-adb', '-s', 'fixture-serial', 'shell'])
            self.assertEqual(shlex.split(command[4]), [driver, '--version'])
            builder.invoke_driver(adb, driver, *builder.compiler_flags(), timeout=300)
            self.assertEqual(shlex.split(run.call_args.args[0][4])[0], driver)
            self.assertNotIn('LD_LIBRARY_PATH=', run.call_args.args[0][4])


if __name__ == '__main__':
    unittest.main()
