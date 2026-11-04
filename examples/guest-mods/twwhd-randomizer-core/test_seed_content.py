from pathlib import Path
import sys
import tempfile
from contextlib import nullcontext
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import seed_content


class SeedContent(unittest.TestCase):
    def test_delta_and_rejections(self):
        with tempfile.TemporaryDirectory() as temp:
            base, seed = Path(temp)/'base', Path(temp)/'seed'
            base.mkdir(); seed.mkdir()
            for root in (base, seed):
                (root/'same').write_bytes(b'same')
                (root/'changed').write_bytes(b'old')
            (seed/'changed').write_bytes(b'new')
            (seed/'added').write_bytes(b'add')
            result = seed_content.plan_content_delta(base, seed)
            self.assertEqual([r['path'] for r in result['files']], ['added', 'changed'])
            self.assertEqual(result['total_size'], 6)
            with patch.object(seed_content, 'TOTAL_LIMIT', 5), self.assertRaises(ValueError):
                seed_content.plan_content_delta(base, seed)
            with patch.object(seed_content, 'FILE_LIMIT', 2), self.assertRaises(ValueError):
                seed_content.plan_content_delta(base, seed)
            with seed_content.os.scandir(seed) as scan:
                entries = list(scan)
            entries.append(SimpleNamespace(name='Same', stat=lambda **kwargs: (seed/'same').stat()))
            with patch.object(seed_content.os, 'scandir', return_value=nullcontext(entries)), self.assertRaises(ValueError):
                seed_content._inventory(seed)
            (seed/'link').symlink_to(base/'same')
            with self.assertRaises(ValueError): seed_content.plan_content_delta(base, seed)
            (seed/'link').unlink(); (seed/'same').unlink()
            with self.assertRaises(ValueError): seed_content.plan_content_delta(base, seed)

    def test_copy_is_private_exclusive_and_verified(self):
        with tempfile.TemporaryDirectory() as temp:
            base, seed, output = [Path(temp)/name for name in ('base', 'seed', 'output')]
            base.mkdir(); seed.mkdir(); (seed/'nested').mkdir()
            (seed/'nested/item').write_bytes(b'asset')
            for nested in (base/'output', seed/'output'):
                with self.assertRaises(ValueError):
                    seed_content.copy_content_delta(base, seed, nested)
                self.assertFalse(nested.exists())
            result = seed_content.copy_content_delta(base, seed, output)
            self.assertEqual(result['total_size'], 5)
            self.assertEqual((output/'content/nested/item').read_bytes(), b'asset')
            self.assertTrue((output/'seed-content.json').is_file())
            with self.assertRaises(FileExistsError):
                seed_content.copy_content_delta(base, seed, output)
            self.assertEqual((output/'content/nested/item').read_bytes(), b'asset')
            wrong = {'files': [{'path': 'nested/item', 'size': 5, 'sha256': '0'*64}], 'total_size': 5}
            with patch.object(seed_content, 'plan_content_delta', return_value=wrong), self.assertRaises(ValueError):
                seed_content.copy_content_delta(base, seed, Path(temp)/'failed')
            self.assertFalse((Path(temp)/'failed/seed-content.json').exists())

    def test_identity_covers_unchanged_content_too(self):
        from seed_identity import seed_identity
        with tempfile.TemporaryDirectory() as temp:
            base, seed = Path(temp)/'base', Path(temp)/'seed'
            base.mkdir();seed.mkdir()
            for root in (base,seed):(root/'same').write_bytes(b'first')
            first = seed_content.plan_content_delta(base,seed)
            for root in (base,seed):(root/'same').write_bytes(b'other')
            second = seed_content.plan_content_delta(base,seed)
            self.assertEqual(first['files'],[]);self.assertEqual(second['files'],[])
            self.assertNotEqual(first['seed_content_sha256'],second['seed_content_sha256'])
            self.assertNotEqual(seed_identity('a'*64,first['seed_content_sha256']),
                                seed_identity('a'*64,second['seed_content_sha256']))
            self.assertNotEqual(seed_identity('a'*64,first['seed_content_sha256']),
                                seed_identity('b'*64,first['seed_content_sha256']))
            with self.assertRaises(ValueError):seed_identity('A'*64,first['seed_content_sha256'])


if __name__ == '__main__':
    unittest.main()
