"""Exercise the real SDK payload with the unchanged release guard; no game files."""
from pathlib import Path
import tempfile
import unittest
import zipfile

import guard
import package


class SDKPackage(unittest.TestCase):
    def test_platform_sdk_archives_pass_guard(self):
        for platform in ("macos-arm64", "linux-x86_64", "linux-aarch64", "windows-x86_64"):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp) / ("WindWakerHD-test-" + platform)
                package.copy_sdk_headers(str(root))
                headers = root / "sdk/guest/include/wwhd"
                self.assertTrue((headers / "functions.h").is_file())
                self.assertTrue((headers / "bindings.h").is_file())
                self.assertFalse((headers.parent / "game").exists())
                archive = Path(tmp) / "sdk.zip"
                with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
                    for path in root.rglob("*"):
                        if path.is_file():
                            z.write(path, path.relative_to(root.parent).as_posix())
                problems, count = guard.scan(str(archive))
                self.assertGreater(count, 10)
                self.assertEqual(problems, [])

    def test_guard_still_rejects_game_tree_and_function_body(self):
        problems = []
        guard.check_entry("sdk/guest/include/game/link.h", b"/* declarations */", problems)
        self.assertTrue(any("game file tree" in problem for problem in problems))
        problems = []
        guard.check_entry("sdk/guest/include/wwhd/bindings.h",
                          b"void f_02000000(Cpu* __restrict c) {\n", problems)
        self.assertTrue(any("recompiled game functions" in problem for problem in problems))
        problems = []
        guard.check_entry("sdk/guest/include/wwhd/functions.h", b"0" * 32, problems)
        self.assertTrue(any("key-like" in problem for problem in problems))


if __name__ == "__main__":
    unittest.main()
