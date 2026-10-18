#!/usr/bin/env python3
"""tools/decomp_build/gen.py on a small synthetic decompilation (no game files, no submodule needed)."""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "recomp"))
import builds as game_builds  # noqa: E402


def runtime_hook():
    """some function the runtime hooks (a non-site entry of tools/recomp/hooks*.txt)"""
    entries, _ = game_builds.read_hooks(game_builds.hook_files(os.path.join(ROOT, "tools", "recomp")),
                                        game_builds.canonical_build())
    return next(canon for site, canon, _, _ in entries if not site)


class GenTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.src = os.path.join(self.tmp.name, "decomp")
        self.out = os.path.join(self.tmp.name, "out")
        for d in ("wwhd_src", "tools/verify/include", "tools/verify/units"):
            os.makedirs(os.path.join(self.src, d))
        open(os.path.join(self.src, "tools/verify/include/gabi.h"), "w").close()
        hooked = runtime_hook()

        def write(rel, text):
            with open(os.path.join(self.src, rel), "w") as f:
                f.write(text)

        # a.cpp shared by two verify units (one game unit "a"); b.cpp verifies 0x02000100 too
        write("wwhd_src/a.cpp", "VERIFY(0x02000100, f_one)\nVERIFY(0x02000200, Cls::two)\n"
                                "/* VERIFY(0x02000300, commented) */\n// VERIFY(0x02000400, gone)\n"
                                "VERIFY(0x%08X, hooked_by_runtime)\n" % hooked)
        write("wwhd_src/a2.cpp", "VERIFY(0x02000500, \"quoted\")\n")
        write("wwhd_src/b.cpp", "VERIFY(0x02000100, dup)\nVERIFY(0x02000600, b_fn)\n")
        write("tools/verify/units/a.txt", "src wwhd_src/a.cpp\n")
        write("tools/verify/units/a_more.txt", "src wwhd_src/a.cpp wwhd_src/a2.cpp  # comment\n")
        write("tools/verify/units/bunit.txt", "src wwhd_src/b.cpp\n")
        write("tools/verify/units/empty.txt", "# no sources\n")
        self.hooked = hooked

    def tearDown(self):
        self.tmp.cleanup()

    def run_gen(self):
        r = subprocess.run([sys.executable, os.path.join(HERE, "gen.py"), self.src, self.out], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r.stdout

    def read(self, name):
        with open(os.path.join(self.out, name)) as f:
            return f.read()

    def test_mouse_camera_hooks_stay_in_runtime(self):
        # Recompiled camera bodies must retain the mouse sites in decomp builds too.
        with open(os.path.join(self.src, "wwhd_src/a2.cpp"), "a") as f:
            for addr in (0x024FA9C8, 0x0250D4E8, 0x0250FDC8):
                f.write("VERIFY(0x%08X, camera_%08X)\n" % (addr, addr))
        self.run_gen()
        excluded = self.read("excluded.txt")
        listed = self.read("decomp_hooks.txt")
        for addr in ("024FA9C8", "0250D4E8", "0250FDC8"):
            self.assertIn(addr, excluded)
            self.assertNotIn(addr, listed)

    def test_lists(self):
        out = self.run_gen()
        self.assertIn("2 game units, 4 functions decompiled, 2 excluded", out)
        listed = [l.split()[0] for l in self.read("decomp_hooks.txt").splitlines() if not l.startswith("#")]
        self.assertEqual(listed, ["02000100", "02000200", "02000500", "02000600"])
        excluded = self.read("excluded.txt")
        self.assertIn("%08X" % self.hooked, excluded)
        self.assertIn("already a runtime hook", excluded)
        self.assertIn("also verified in a (kept there)", excluded)
        # recomp.py reads the list with the hooks parser
        entries, _ = game_builds.read_hooks([os.path.join(self.out, "decomp_hooks.txt")], game_builds.canonical_build())
        self.assertEqual([e[1] for e in entries], [0x02000100, 0x02000200, 0x02000500, 0x02000600])

    def test_glue(self):
        self.run_gen()
        hooks = self.read("hooks.cpp")
        self.assertIn('void hook_02000500(Cpu* c) { decomp::call(c, 2); }', hooks)
        self.assertIn('{0x02000500u, 0, "\\"quoted\\"", f_02000500_orig}', hooks)
        self.assertIn('{"a_more", 0}', hooks)
        self.assertIn("const unsigned kNumSlots = 4;", hooks)
        units = self.read("units.cmake")
        self.assertIn('set(WWHD_DECOMP_UNIT_COUNT 2)', units)
        self.assertIn('set(WWHD_DECOMP_UNIT_1_NAME "bunit")', units)
        self.assertIn('#define DECOMP_UNIT "a"', self.read("glue/0.cpp"))
        units = json.loads(self.read("units.json"))
        self.assertEqual(units["functions"], 4)
        self.assertEqual([u["name"] for u in units["units"]], ["a", "bunit"])
        self.assertEqual(sorted(os.path.basename(x["path"]) for x in units["units"][0]["sources"]), ["a.cpp", "a2.cpp"])
        self.assertTrue(units["units"][0]["glue"].endswith(os.path.join("glue", "0.cpp")))

    def test_unchanged_outputs_keep_their_time(self):
        self.run_gen()
        p = os.path.join(self.out, "hooks.cpp")
        os.utime(p, (1, 1))
        self.run_gen()
        self.assertEqual(os.path.getmtime(p), 1)

    def test_unmarked_shared_source_does_not_join(self):
        # c1/c2 share only c_pending.cpp (no VERIFY marks: support code): two game units, each with its copy
        with open(os.path.join(self.src, "wwhd_src/c_pending.cpp"), "w") as f:
            f.write("void stub() {}\n")
        for u, fn in (("c1", "0x02000800"), ("c2", "0x02000900")):
            with open(os.path.join(self.src, "wwhd_src/%s.cpp" % u), "w") as f:
                f.write("VERIFY(%s, f_%s)\n" % (fn, u))
            with open(os.path.join(self.src, "tools/verify/units/%s.txt" % u), "w") as f:
                f.write("src wwhd_src/%s.cpp wwhd_src/c_pending.cpp\n" % u)
        self.assertIn("4 game units", self.run_gen())
        units = self.read("units.cmake")
        self.assertIn('_NAME "c1")', units)
        self.assertIn('_NAME "c2")', units)
        self.assertEqual(units.count("wwhd_src/c_pending.cpp\""), 3)  # c1, c2 and the configure dependencies

    def test_verify_named(self):
        with open(os.path.join(self.src, "wwhd_src/b.cpp"), "a") as f:
            f.write('VERIFY_NAMED(0x02000700, static_cast<void (C::*)(int)>(&C::set), "&C::set(int)");\n')
        self.run_gen()
        self.assertIn("02000700\tbunit\t&C::set(int)", self.read("functions.tsv"))

    def test_glued_source_needs_the_game(self):
        with open(os.path.join(self.src, "wwhd_src/b.cpp"), "a") as f:
            f.write("/* WWHD glue (tools/verify/autoglue.py)\n*/\n")
        r = subprocess.run([sys.executable, os.path.join(HERE, "gen.py"), self.src, self.out], capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("WWHD_DECOMP_GAME", r.stderr)

    def test_missing_tree(self):
        r = subprocess.run([sys.executable, os.path.join(HERE, "gen.py"), os.path.join(self.tmp.name, "nothing"), self.out],
                           capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("git submodule update --init decomp", r.stderr)


if __name__ == "__main__":
    unittest.main()
