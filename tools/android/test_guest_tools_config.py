"""The phone's code-mod build tools file (GuestToolsConfig.java) as the runtime reads it (guest_build.h)."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("javac") and shutil.which("java"), "JDK required")
class GuestToolsConfigTests(unittest.TestCase):
    def test_sample_is_format_1_with_absolute_tools(self):
        with tempfile.TemporaryDirectory() as out:
            subprocess.run(["javac", "-d", out, str(ROOT / "android/app/src/main/java/org/wwhdrecomp/wwhd/GuestToolsConfig.java"),
                            str(ROOT / "tools/android/GuestToolsConfigTest.java")], check=True)
            text = subprocess.run(["java", "-cp", out, "org.wwhdrecomp.wwhd.GuestToolsConfigTest", "--print"],
                                  check=True, stdout=subprocess.PIPE, text=True).stdout
        config = json.loads(text)
        self.assertEqual(config["format_version"], 1)
        self.assertEqual(set(config), {"format_version", "python", "compiler", "builder", "include"})
        for command in (config["python"], config["compiler"]):
            self.assertTrue(command and command[0].startswith("/") and command[0].endswith(".so"))
        self.assertTrue(config["builder"].endswith("tools/guestmod/build_guest_mod.py"))
        self.assertIn("-fuse-ld=lld", config["compiler"])
        self.assertIn("-Wl,-z,max-page-size=16384", config["compiler"])


if __name__ == "__main__":
    unittest.main()
