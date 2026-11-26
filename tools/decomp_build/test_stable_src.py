#!/usr/bin/env python3
"""tools/decomp_build/stable_src.py: the link follows the named tree; a file whose content the build already saw keeps
the time the build saw, every other file gets a time newer than any output (ninja rebuilds exactly those)"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import stable_src  # noqa: E402

T0 = 1_000_000_000 * 10**9  # an old time (ns)
NOW = 2_000_000_000 * 10**9


@unittest.skipIf(os.name == "nt", "stable_src serves the Android build on macOS/Linux hosts (symbolic links)")
class StableSrcTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.tmp.name
        self.root = os.path.join(t, "build")
        self.a, self.b = os.path.join(t, "wt-a"), os.path.join(t, "wt-b")
        for tree in (self.a, self.b):
            os.makedirs(os.path.join(tree, "src"))
            subprocess.run(["git", "init", "-q", tree], check=True)
            self.write(tree, ".gitignore", "build/\n")
            self.write(tree, "src/same.cpp", "int same;\n")
            self.write(tree, "src/diff.cpp", "int diff_%s;\n" % os.path.basename(tree))
            self.write(tree, "build/out.o", "ignored\n")
        self.write(self.b, "src/new.cpp", "int only_b;\n")
        for dp, _, fs in os.walk(t):
            for f in fs:
                os.utime(os.path.join(dp, f), ns=(T0, T0))
        os.makedirs(self.root)

    def tearDown(self):
        self.tmp.cleanup()

    @staticmethod
    def write(tree, rel, text):
        p = os.path.join(tree, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w") as f:
            f.write(text)

    @staticmethod
    def mtime(tree, rel):
        return os.stat(os.path.join(tree, rel)).st_mtime_ns

    def test_switch_between_trees(self):
        # first run: nothing recorded, every file is new
        self.assertEqual(stable_src.switch(self.root, {"src": self.a}, now=NOW), {"src": (0, 3)})
        self.assertEqual(os.path.realpath(os.path.join(self.root, "src")), os.path.realpath(self.a))
        build_saw = self.mtime(self.a, "src/same.cpp")
        self.assertEqual(build_saw, NOW)
        # the other tree: same content keeps the time the build saw, a changed or new file is newer than the outputs
        later = NOW + 10**9
        self.assertEqual(stable_src.switch(self.root, {"src": self.b}, now=later), {"src": (2, 2)})
        self.assertEqual(os.path.realpath(os.path.join(self.root, "src")), os.path.realpath(self.b))
        self.assertEqual(self.mtime(self.b, "src/same.cpp"), build_saw)
        self.assertEqual(self.mtime(self.b, ".gitignore"), build_saw)
        self.assertEqual(self.mtime(self.b, "src/diff.cpp"), later)
        self.assertEqual(self.mtime(self.b, "src/new.cpp"), later)
        self.assertEqual(self.mtime(self.b, "build/out.o"), T0)  # ignored: untouched
        # back to the first tree: diff.cpp differs from what the build saw last (wt-b's) -> newer again
        last = later + 10**9
        self.assertEqual(stable_src.switch(self.root, {"src": self.a}, now=last), {"src": (2, 1)})
        self.assertEqual(self.mtime(self.a, "src/diff.cpp"), last)
        self.assertEqual(self.mtime(self.a, "src/same.cpp"), build_saw)

    def test_same_tree_edit_and_touch(self):
        stable_src.switch(self.root, {"src": self.a}, now=NOW)
        # an edit after the run: content differs from the record -> newer than any output
        self.write(self.a, "src/diff.cpp", "int edited;\n")
        os.utime(os.path.join(self.a, "src/diff.cpp"), ns=(T0, T0))  # even with an old time
        # a touch without a change: back to the recorded time
        os.utime(os.path.join(self.a, "src/same.cpp"), ns=(NOW + 5, NOW + 5))
        later = NOW + 10**9
        self.assertEqual(stable_src.switch(self.root, {"src": self.a}, now=later), {"src": (2, 1)})
        self.assertEqual(self.mtime(self.a, "src/diff.cpp"), later)
        self.assertEqual(self.mtime(self.a, "src/same.cpp"), NOW)

    def test_two_links_and_record(self):
        stable_src.switch(self.root, {"src": self.a, "decomp-src": self.b}, now=NOW)
        stable_src.switch(self.root, {"src": self.b}, now=NOW + 1)  # decomp-src keeps its record and link
        with open(os.path.join(self.root, stable_src.RECORD)) as f:
            rec = json.load(f)
        self.assertEqual(sorted(rec), ["decomp-src", "src"])
        self.assertEqual(rec["decomp-src"]["tree"], os.path.realpath(self.b))
        self.assertEqual(rec["src"]["tree"], os.path.realpath(self.b))
        self.assertEqual(os.path.realpath(os.path.join(self.root, "decomp-src")), os.path.realpath(self.b))

    def test_adopt_then_switch(self):
        # a build folder moved to the links from wt-a: recorded as it is (no time changes), then wt-b
        self.assertEqual(stable_src.switch(self.root, {"src": self.a}, now=NOW, adopt=True), {"src": (3, 0)})
        self.assertEqual(self.mtime(self.a, "src/diff.cpp"), T0)
        self.assertEqual(os.path.realpath(os.path.join(self.root, "src")), os.path.realpath(self.a))
        self.assertEqual(stable_src.switch(self.root, {"src": self.b}, now=NOW), {"src": (2, 2)})
        self.assertEqual(self.mtime(self.b, "src/same.cpp"), T0)
        self.assertEqual(self.mtime(self.b, "src/diff.cpp"), NOW)
        self.assertEqual(self.mtime(self.a, "src/diff.cpp"), T0)  # the adopted tree is never touched

    def test_not_a_link(self):
        os.makedirs(os.path.join(self.root, "src"))
        r = subprocess.run([sys.executable, os.path.join(HERE, "stable_src.py"), self.root, "src=" + self.a],
                           capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("is not a link", r.stderr)

    def test_without_git(self):
        shutil.rmtree(os.path.join(self.a, ".git"))
        self.assertEqual(stable_src.switch(self.root, {"src": self.a}, now=NOW), {"src": (0, 4)})  # build/out.o too


if __name__ == "__main__":
    unittest.main()
