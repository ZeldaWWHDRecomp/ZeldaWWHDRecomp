import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import core_randomizer


class ProgressContention(unittest.TestCase):
    def test_transient_reader_contention_retries_atomic_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'progress.json';path.write_text('{"phase":"old"}')
            replace=core_randomizer.os.replace;attempts=[]
            def contended(source, target):
                attempts.append(target)
                if len(attempts)<3:
                    self.assertEqual(json.loads(path.read_text()),{'phase':'old'})
                    raise PermissionError('status reader still holds the file')
                replace(source,target)
            with patch.object(core_randomizer.os,'replace',side_effect=contended), patch.object(core_randomizer.time,'sleep'):
                core_randomizer.publish_progress(path,'Ready to import')
            self.assertEqual(json.loads(path.read_text()),{'phase':'Ready to import'})
            self.assertFalse(path.with_suffix('.json.tmp').exists())

    def test_busy_status_keeps_previous_complete_phase_without_failing_build(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'progress.json';path.write_text('{"phase":"old"}')
            with patch.object(core_randomizer.os,'replace',side_effect=PermissionError('busy')), patch.object(core_randomizer.time,'sleep'):
                core_randomizer.publish_progress(path,'Ready to import')
            self.assertEqual(json.loads(path.read_text()),{'phase':'old'})
            self.assertFalse(path.with_suffix('.json.tmp').exists())

    def test_other_io_failure_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(core_randomizer.os,'replace',side_effect=OSError('disk failure')):
                with self.assertRaisesRegex(OSError,'disk failure'):
                    core_randomizer.publish_progress(Path(directory)/'progress.json','Preparing')
