"""model_sweep_keys.py: the pipelines of a hidden and a presenting run of the same slice are compared by their
portable key digests."""
import io
from contextlib import redirect_stdout
from pathlib import Path
import tempfile
import unittest

import model_sweep_keys


class KeysTest(unittest.TestCase):
    def write(self, folder, name, digests):
        path = Path(folder) / name
        path.write_text(''.join('%d %d %s %016x %d\n' % (100 + i, i, d, i, 1) for i, d in enumerate(digests)))
        return str(path)

    def test_same_pipelines_in_another_order_are_identical(self):
        with tempfile.TemporaryDirectory() as temp:
            a = self.write(temp, 'hidden.txt', ['00000000000000aa', '00000000000000bb', '00000000000000aa'])
            b = self.write(temp, 'present.txt', ['00000000000000bb', '00000000000000aa'])
            self.assertEqual(model_sweep_keys.digests(a), ['00000000000000aa', '00000000000000bb'])
            with redirect_stdout(io.StringIO()) as out:
                self.assertEqual(model_sweep_keys.main([a, b]), 0)
            self.assertIn('identical', out.getvalue())

    def test_a_pipeline_only_one_run_made_is_named(self):
        with tempfile.TemporaryDirectory() as temp:
            a = self.write(temp, 'hidden.txt', ['00000000000000aa'])
            b = self.write(temp, 'present.txt', ['00000000000000aa', '00000000000000cc', 'not-a-digest'])
            with redirect_stdout(io.StringIO()) as out:
                self.assertEqual(model_sweep_keys.main([a, b]), 1)
            self.assertIn('00000000000000cc', out.getvalue())
            self.assertEqual(model_sweep_keys.compare(['a'], ['a', 'c'])['only_second'], ['c'])


if __name__ == '__main__':
    unittest.main()
