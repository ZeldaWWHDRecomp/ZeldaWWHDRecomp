"""Exercise the real SDK payload with the unchanged release guard; no game files."""
from pathlib import Path
import ast
import shutil
import subprocess
import sys
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

    def test_installer_local_imports_are_shipped(self):
        installer = Path(package.ROOT) / "tools/installer"
        shipped = set(package.INSTALLER_FILES)
        for source in installer.glob("*.py"):
            if source.relative_to(package.ROOT).as_posix() not in shipped:
                continue
            for node in ast.walk(ast.parse(source.read_text())):
                names = ([alias.name for alias in node.names] if isinstance(node, ast.Import)
                         else [node.module] if isinstance(node, ast.ImportFrom) and node.module else [])
                for name in names:
                    sibling = installer / (name.split(".")[0] + ".py")
                    if sibling.is_file():
                        self.assertIn(sibling.relative_to(package.ROOT).as_posix(), shipped)

    def test_packaged_installer_import_in_isolated_python(self):
        # -I excludes both cwd and the script directory, like Windows' ._pth.
        # Use only shipped files; importing from the source checkout would hide omissions.
        for layout in ("macos", "linux", "windows", "appimage", "android"):
            with self.subTest(layout=layout), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp) / layout
                for relative in package.TOOL_FILES + package.INSTALLER_FILES:
                    dest = root / relative
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(Path(package.ROOT) / relative, dest)
                maps = root / "tools/recomp/builds"
                shutil.copytree(Path(package.ROOT) / "tools/recomp/builds", maps)
                result = subprocess.run(
                    [sys.executable, "-I", "-B", "-c",
                     "import runpy,sys; runpy.run_path(sys.argv[1], run_name='package_import_test')",
                     str(root / "tools/installer/setup.py")],
                    cwd=tmp, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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
