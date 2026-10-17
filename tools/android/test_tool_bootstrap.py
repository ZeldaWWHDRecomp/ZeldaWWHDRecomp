"""The runtime's bootstraps for a mod package's Python preparation tool (runtime/src/mods/guest_build.h)."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

HEADER = Path(__file__).resolve().parents[2] / "runtime/src/mods/guest_build.h"


def constant(name):
    match = re.search(r"inline constexpr const char\* %s=\s*((?:\"(?:[^\"\\\\]|\\\\.)*\"\s*)+);" % name, HEADER.read_text())
    return "".join(json.loads(part) for part in re.findall(r"\"(?:[^\"\\\\]|\\\\.)*\"", match.group(1)))


class ToolBootstrapTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        package = self.root / "package"
        package.mkdir()
        (package / "helper.py").write_text("VALUE = 'imported'\n")
        (package / "tool.py").write_text(
            "import json, os, sys, helper\n"
            "print(json.dumps({'cwd': os.getcwd(), 'argv': sys.argv, 'helper': helper.VALUE, 'name': __name__}))\n")
        self.data = self.root / "data"
        self.data.mkdir()

    def run_tool(self, arguments, cwd):
        result = subprocess.run([sys.executable, "-B", "-c"] + arguments, cwd=cwd, check=True,
                                stdout=subprocess.PIPE, text=True)
        return json.loads(result.stdout)

    def test_desktop_bootstrap_imports_the_tools_siblings(self):
        out = self.run_tool([constant("tool_bootstrap"), str(self.root / "package/tool.py"), "--out", "x"], self.data)
        self.assertEqual(out["helper"], "imported")
        self.assertEqual(out["name"], "__main__")
        self.assertEqual(out["argv"], [str(self.root / "package/tool.py"), "--out", "x"])

    def test_android_bootstrap_enters_the_data_folder_first(self):
        # started from another folder, with a relative tool path: resolved before entering the folder
        out = self.run_tool([constant("tool_bootstrap_in_folder"), str(self.data), "package/tool.py", "--out", "x"], self.root)
        self.assertEqual(os.path.realpath(out["cwd"]), os.path.realpath(self.data))
        self.assertEqual(out["helper"], "imported")
        self.assertEqual(out["argv"], [str(self.root / "package/tool.py"), "--out", "x"])


if __name__ == "__main__":
    unittest.main()
