"""Reject a mismatched build before producing an installable extractor."""
from pathlib import Path
import tempfile
import unittest

from package_extractor import package, NDK_REVISION


class ExtractorPackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build, self.ndk, self.output = (self.root / name for name in ("build", "ndk", "apk"))
        self.build.mkdir()
        self.ndk.mkdir()
        (self.ndk / "source.properties").write_text("Pkg.Revision = " + NDK_REVISION + "\n")
        self.cache()
        (self.build / "wwhd-zstd.txt").write_text("system\n")

    def cache(self, abi="arm64-v8a", api="android-33", toolchain=None):
        toolchain = toolchain or self.ndk / "build/cmake/android.toolchain.cmake"
        (self.build / "CMakeCache.txt").write_text(
            f"ANDROID_ABI:STRING={abi}\nANDROID_PLATFORM:STRING={api}\nCMAKE_TOOLCHAIN_FILE:FILEPATH={toolchain}\n")

    def rejected(self, message):
        with self.assertRaisesRegex(ValueError, message):
            package(self.build, self.ndk, "arm64-v8a", self.output)
        self.assertFalse(self.output.exists())

    def test_rejects_wrong_abi_or_api(self):
        self.cache(abi="x86_64")
        self.rejected("ABI/API")
        self.cache(api="android-29")
        self.rejected("ABI/API")

    def test_rejects_wrong_ndk_and_toolchain(self):
        self.cache(toolchain=self.root / "different-ndk/toolchain.cmake")
        self.rejected("pinned NDK toolchain")
        self.cache()
        (self.ndk / "source.properties").write_text("Pkg.Revision = 1\n")
        self.rejected("pinned NDK")

    def test_rejects_system_zstd(self):
        self.rejected("pinned bundled zstd")
