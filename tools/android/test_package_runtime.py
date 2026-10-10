"""Check that runtime delivery preserves linking and excludes generated game code."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from package_runtime import recipe


class RuntimeRecipeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build, self.sdk = self.root / "build", self.root / "sdk"
        self.build.mkdir()
        for name in ("runtime.o", "libgamecode.a", "libgpu.a", "libSDL3.so"):
            (self.build / name).write_bytes(("authored fixture " + name).encode())

    @patch("package_runtime.subprocess.run")
    def test_retains_order_and_relocates_runtime_without_game_archive(self, strip):
        result = recipe(["clang++", "--target=aarch64-none-linux-android33", "--sysroot=/host/ndk",
                         "-shared", "-Xlinker", "--dependency-file=link.d", "-Wl,-soname,libmain.so",
                         "-o", "libmain.so", "runtime.o", "libgamecode.a", "libgpu.a", "libSDL3.so",
                         "libgpu.a", "-llog"], self.build, self.sdk, Path("llvm-strip"))
        start = result.index("{sdk}/obj/0-runtime.o")
        self.assertEqual(result[start:start + 5], ["{sdk}/obj/0-runtime.o", "{gamecode}",
                         "{sdk}/lib/1-libgpu.a", "{sdk}/lib/libSDL3.so", "{sdk}/lib/1-libgpu.a"])
        self.assertFalse(any("/host/" in arg or "dependency-file" in arg for arg in result))
        self.assertIn("-Wl,-soname,libwwhdgame.so", result)
        self.assertEqual((self.sdk / "obj/0-runtime.o").read_bytes(), (self.build / "runtime.o").read_bytes())
        self.assertFalse(any("gamecode" in str(path) for path in self.sdk.rglob("*")))
        self.assertEqual(strip.call_count, 3)

    @patch("package_runtime.subprocess.run")
    def test_missing_or_duplicate_game_archive_rejected(self, strip):
        for count in (0, 2):
            with self.subTest(count=count), self.assertRaisesRegex(ValueError, "Incomplete"):
                recipe(["clang++", "-shared", "-o", "libmain.so", "runtime.o"] +
                       ["libgamecode.a"] * count, self.build, self.sdk, Path("llvm-strip"))

    def test_unknown_link_inputs_rejected(self):
        with self.assertRaisesRegex(ValueError, "Unexpected Android link input"):
            recipe(["clang++", "unexpected.rsp"], self.build, self.sdk, Path("llvm-strip"))


if __name__ == "__main__":
    unittest.main()
