"""Host-JDK checks for Android save publication, startup touch, input quirks and redraw pacing."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "android/app/src/main/java/org/wwhdrecomp/wwhd"

@unittest.skipUnless(shutil.which("javac") and shutil.which("java"), "host JDK required")
class SaveStartupTests(unittest.TestCase):
    def test_java_contracts(self):
        production = ["SessionLease", "SafeSaveArchive", "TransactionalSaveStore", "InternalInputDevices",
                      "StartupTouch", "TouchRedraw", "TouchLayout", "TouchPresets", "TouchStick",
                      "ContextPack", "ContextAssets", "GoldenRings"]
        tests = ["SaveSafetyTest", "InternalInputDevicesTest", "StartupTouchTest", "TouchRedrawTest",
                 "TouchLayoutTest", "TouchPresetsTest", "TouchStickTest", "ContextButtonsTest"]
        with tempfile.TemporaryDirectory(prefix="wwhd-save-startup-") as out:
            subprocess.run(["javac", "-encoding", "UTF-8", "-d", out,
                            *[str(SRC / (name + ".java")) for name in production],
                            *[str(ROOT / "tools/android" / (name + ".java")) for name in tests]], check=True)
            for name in tests:
                with self.subTest(name=name):
                    subprocess.run(["java", "-ea", "-cp", out, "org.wwhdrecomp.wwhd." + name], check=True)

if __name__ == "__main__":
    unittest.main()
