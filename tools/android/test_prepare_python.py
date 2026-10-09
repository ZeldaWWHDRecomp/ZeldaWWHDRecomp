"""Resource identities must survive repackaging without changing source bytes."""
import io
import hashlib
from pathlib import Path
import tempfile
from unittest.mock import patch
import unittest
import zipfile

from prepare_python import write_entry, verified_download


class ResourceIdentityTests(unittest.TestCase):
    def test_same_source_bytes_have_same_archive_identity(self):
        def bundle(name, data):
            stream = io.BytesIO()
            with zipfile.ZipFile(stream, "w") as archive:
                write_entry(archive, name, data)
            return stream.getvalue()
        first = bundle("tools/setup.py", b"authored source")
        self.assertEqual(first, bundle("tools/setup.py", b"authored source"))
        self.assertNotEqual(first, bundle("tools/setup.py", b"changed source"))
        with zipfile.ZipFile(io.BytesIO(first)) as archive:
            item = archive.getinfo("tools/setup.py")
            self.assertEqual(item.date_time, (1980, 1, 1, 0, 0, 0))
            self.assertEqual(archive.read(item), b"authored source")


class DownloadTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.archive = Path(temporary.name) / "python.tar.gz"
        self.partial = self.archive.with_name(self.archive.name + ".partial")
        self.content = b"authored upstream archive bytes"
        self.expected = hashlib.sha256(self.content).hexdigest()

    def fetch(self, content=None):
        with patch("prepare_python.urllib.request.urlopen", return_value=io.BytesIO(
                self.content if content is None else content)):
            return verified_download("https://fixture.invalid/python.tar.gz", self.archive, self.expected)

    def test_verified_cache_needs_no_network_and_removes_stale_partial(self):
        self.archive.write_bytes(self.content)
        self.partial.write_bytes(b"interrupted transfer")
        with patch("prepare_python.urllib.request.urlopen", side_effect=AssertionError("network not needed")):
            self.assertEqual(verified_download("https://fixture.invalid", self.archive, self.expected), self.archive)
        self.assertEqual(self.archive.read_bytes(), self.content)
        self.assertFalse(self.partial.exists())

    def test_bad_download_is_never_published_and_retry_succeeds(self):
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            self.fetch(b"corrupt authored bytes")
        self.assertFalse(self.archive.exists())
        self.assertFalse(self.partial.exists())
        self.fetch()
        self.assertEqual(self.archive.read_bytes(), self.content)

    def test_damaged_cache_is_replaced_only_with_verified_bytes(self):
        self.archive.write_bytes(b"damaged cached archive")
        self.fetch()
        self.assertEqual(self.archive.read_bytes(), self.content)
        self.assertFalse(self.partial.exists())

    def test_interrupted_transfer_and_failed_sync_leave_no_cache(self):
        import errno
        class Interrupted(io.BytesIO):
            def read(self, count):
                if self.tell(): raise OSError("authored interrupted transfer")
                return super().read(5)
        with patch("prepare_python.urllib.request.urlopen", return_value=Interrupted(self.content)):
            with self.assertRaisesRegex(OSError, "interrupted transfer"):
                verified_download("https://fixture.invalid", self.archive, self.expected)
        self.assertFalse(self.archive.exists())
        self.assertFalse(self.partial.exists())
        with patch("prepare_python.os.fsync", side_effect=OSError(errno.ENOSPC, "authored full disk")):
            with self.assertRaises(OSError): self.fetch()
        self.assertFalse(self.archive.exists())
        self.assertFalse(self.partial.exists())
        self.fetch()
        self.assertEqual(self.archive.read_bytes(), self.content)


if __name__ == "__main__":
    unittest.main()
