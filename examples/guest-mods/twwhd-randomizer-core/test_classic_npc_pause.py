"""Staged classic consumers; neither descriptor set is part of mod.c yet."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class ClassicNpcPause(unittest.TestCase):
    def test_callback_and_branch_matrix(self):
        source=Path(__file__).with_name('classic_npc_pause_test.c')
        for mode in (0,1):
            with tempfile.TemporaryDirectory() as directory:
                exe=Path(directory)/'test'
                subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror',f'-DWWHD_RANDOMIZER_CLASSIC_MODE={mode}',str(source),'-o',str(exe)],check=True)
                subprocess.run([str(exe)],check=True)

    def test_missing_or_invalid_flag_fails_closed(self):
        root=Path(__file__).parent
        for header in ('classic_abe.h','classic_pause.h'):
            for prefix in ('','#define WWHD_RANDOMIZER_CLASSIC_MODE 2\n'):
                with tempfile.TemporaryDirectory() as directory:
                    source=Path(directory)/'bad.c'
                    source.write_text(prefix+f'#include "{header}"\n')
                    result=subprocess.run([os.environ.get('CC','clang'),'-std=c11','-fsyntax-only','-I',str(root),str(source)],capture_output=True)
                    self.assertNotEqual(result.returncode,0)
                    self.assertIn(b'classic mode',result.stderr)
