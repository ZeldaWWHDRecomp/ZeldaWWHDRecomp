#!/usr/bin/env python3
"""tools/decomp_build/windows_compile_check.py's own logic, without a compiler or the decompilation: the compile jobs it
takes from units.json (as tools/installer/decomp_setup.py build_once lists them), the unit options as setup substitutes
the manifest's placeholders, the error location shown for a glued copy, the link smoke's units, and gen.py --no-game's
stand-ins for the game (placeholder strings, zero image, skipped table checks). The CMake side (WWHD_DECOMP_FLAGS_ONLY
before anything that needs the runtime's dependencies) is checked against CMakeLists.txt."""
import os
import re
import sys
import tempfile
import types
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import gen  # noqa: E402
import windows_compile_check as wcc  # noqa: E402


class Jobs(unittest.TestCase):
    def units(self, out):
        return [{"name": "u0", "glue": out + "/glue/0.cpp", "sources": [
                    {"path": out + "/glued/wwhd_src/a.cpp", "quote": "/d/wwhd_src"},
                    {"path": "/d/wwhd_src/b.cpp", "quote": None}]},
                {"name": "u1", "glue": out + "/glue/1.cpp", "sources": [{"path": "/d/wwhd_src/b.cpp", "quote": None}]},
                {"name": "u2", "glue": out + "/glue/2.cpp", "sources": [{"path": "/d/wwhd_src/c.cpp", "quote": None}]}]

    def test_setup_order_tables_and_glue(self):
        with tempfile.TemporaryDirectory() as out:
            os.makedirs(os.path.join(out, "native"))
            open(os.path.join(out, "native", "1.h"), "w").close()  # only unit 1 has a table
            jobs = wcc.compile_jobs(self.units(out), out, glue={0, 2})
            t1 = wcc.fwd(os.path.join(out, "native", "1.h"))
            self.assertEqual(jobs, [
                (0, out + "/glued/wwhd_src/a.cpp", ["-iquote", "/d/wwhd_src"]),
                (0, "/d/wwhd_src/b.cpp", []),
                (0, out + "/glue/0.cpp", []),
                (1, "/d/wwhd_src/b.cpp", ["-include", t1]),  # a source of two units: compiled for each (own table)
                (2, "/d/wwhd_src/c.cpp", []),
                (2, out + "/glue/2.cpp", [])])

    def test_only(self):
        with tempfile.TemporaryDirectory() as out:
            jobs = wcc.compile_jobs(self.units(out), out, only=["b.cpp"])
            self.assertEqual([(i, s) for i, s, _ in jobs], [(0, "/d/wwhd_src/b.cpp"), (1, "/d/wwhd_src/b.cpp")])

    def test_smoke_units(self):
        units = [{"sources": [1] * n} for n in (9, 5, 1, 7, 1)]
        self.assertEqual(wcc.smoke_units(units, 3), [0, 2, 4])
        self.assertEqual(wcc.smoke_units(units, 1), [0])
        self.assertEqual(wcc.smoke_units(units, 0), [])


class Flags(unittest.TestCase):
    def test_placeholders(self):
        portable = ["-I{sdk}/include", "-I{decomp}/tools/verify/include", "-I{out}/hinc", "-O2", "-include",
                    "{tools}/decomp_build/frames_vis.h", "-I{sdk}/decomp/include"]
        got = wcc.local_flags(portable, {"decomp": "/d", "tools": "/t", "out": "/o"})
        self.assertEqual(got, ["-I" + wcc.fwd(os.path.join(ROOT, "runtime", "include")), "-I/d/tools/verify/include",
                               "-I/o/hinc", "-O2", "-include", "/t/decomp_build/frames_vis.h",
                               "-I" + wcc.fwd(os.path.join(ROOT, "runtime", "src"))])
        with self.assertRaises(SystemExit):
            wcc.local_flags(["-I{sdk}/lib"], {})

    def test_setup_steps_are_setups(self):
        # the check runs gen.py with setup's arguments and adds setup's per-OS options
        import decomp_platform
        import decomp_setup
        self.assertIs(wcc.decomp_setup, decomp_setup)
        tc = types.SimpleNamespace(kind="llvm-mingw", cxx=["x86_64-w64-mingw32-clang++"])
        self.assertEqual(decomp_platform.unit_flags_extra(tc, False), ["-flto=thin"])
        src = open(os.path.join(HERE, "windows_compile_check.py")).read()
        self.assertIn("decomp_setup.FAST_GEN_ARGS", src)
        self.assertIn("decomp_setup.HOOK_SHARD", src)
        self.assertIn("decomp_platform.unit_flags_extra(tc, fast)", src)
        self.assertIn("decomp_platform.merge_commands(tc, ROOT, out, uobjs, fast)", src)

    def test_cmake_flags_only_before_dependencies(self):
        text = open(os.path.join(ROOT, "CMakeLists.txt")).read()
        at = text.index("if(WWHD_DECOMP_FLAGS_ONLY)")
        block = text[at:text.index("endif()\n", text.index("return()", at))]
        self.assertIn("wwhd_decomp_link(", block)
        self.assertIn("return()", block)
        before = text[:at]
        self.assertIn("include(cmake/Decomp.cmake)", before)
        self.assertIn("wwhd_decomp_configure()", before)
        # nothing before it may need the runtime's dependencies or the generated code
        for need in ("find_package(", "FetchContent", "GEN_SOURCES", "WindowsDependencies.cmake"):
            self.assertNotIn(need, before, need)
        # global compile options before the probe reach the unit options (as in the full build): only these
        self.assertEqual(re.findall(r"add_compile_options\(([^)]*)\)", before), ["-fsigned-char"])


class Errors(unittest.TestCase):
    def test_glued_and_hinc_paths(self):
        d, out = "/w/decomp", "/tmp/x/gen"
        self.assertEqual(wcc.shown_path(out + "/glued/wwhd_src/d/d_snap.cpp", d, out), "/w/decomp/wwhd_src/d/d_snap.cpp")
        self.assertEqual(wcc.shown_path(out + "/hinc/SSystem/a.h", d, out), "/w/decomp/wwhd_src/include/SSystem/a.h")
        inrepo = os.path.join(ROOT, "decomp", "wwhd_src", "x.cpp")
        self.assertEqual(wcc.shown_path(inrepo, os.path.join(ROOT, "decomp"), out), "decomp/wwhd_src/x.cpp")

    def test_first_error(self):
        d = os.path.join(ROOT, "decomp")
        out = "/tmp/x/gen"
        text = ("In file included from /tmp/x/gen/glued/wwhd_src/SSystem/SComponent/c_lib.cpp:9:\n"
                "%s/wwhd_src/include/SSystem/SComponent/c_lib_callsig_t60.h:12:10: fatal error: 'dlfcn.h' file not "
                "found\n1 error generated.\n" % d)
        self.assertEqual(wcc.first_error(text, d, out), ("decomp/wwhd_src/include/SSystem/SComponent/c_lib_callsig_t60.h",
                                                         12, "'dlfcn.h' file not found"))
        self.assertIsNone(wcc.first_error("1 warning generated.\n", d, out))


class NoGame(unittest.TestCase):
    """gen.py --no-game: Glue.load_no_game's stand-ins on a minimal autoglue / assets pair"""

    def test_stand_ins(self):
        with tempfile.TemporaryDirectory() as src:
            v = os.path.join(src, "tools", "verify")
            os.makedirs(v)
            with open(os.path.join(v, "autoglue.py"), "w") as f:
                f.write("def same_text(h, c):\n    return h == c\n"
                        "def source_spelling(text, pos, content):\n    return 'spelled'\n"
                        "def img_read(a, n):\n    raise AssertionError('image read')\n"
                        "def img_cstr(a):\n    raise AssertionError('image read')\n"
                        "def assert_text_use(*a):\n    return True\n"
                        "def check_table(*a, **k):\n    raise AssertionError('table check')\n")
            with open(os.path.join(v, "assets.py"), "w") as f:
                f.write("OUT = None\ndef img_read(a, n):\n    raise AssertionError('image read')\n"
                        "def stale(p):\n    return False\n")
            saved = list(sys.path)
            try:
                for m in ("autoglue", "assets"):
                    sys.modules.pop(m, None)
                assets, ag = gen.Glue(src, os.path.join(src, "out"), None, no_game=True).load()
                s = ag.img_cstr(0x10001234)
                self.assertEqual(s, gen.NO_GAME_STR + "10001234")
                self.assertTrue(ag.same_text("any source text", s))
                self.assertFalse(ag.same_text("a", "b"))  # real texts still compared
                self.assertEqual(ag.source_spelling("", 0, s), s)  # a `str ADDR` line: its own placeholder bytes
                self.assertEqual(ag.source_spelling("", 0, "x"), "spelled")
                self.assertFalse(ag.assert_text_use())
                self.assertEqual(ag.check_table("t", 0), (0, 0))
                self.assertEqual(ag.img_read(0, 4), bytes(4))
                self.assertEqual(assets.img_read(0, 8), bytes(8))
                self.assertTrue(assets.stale("x"))
                self.assertEqual(assets.OUT, os.path.join(src, "out", "include", "assets"))
            finally:
                sys.path[:] = saved
                for m in ("autoglue", "assets"):
                    sys.modules.pop(m, None)

    def test_no_game_excludes_game(self):
        with tempfile.TemporaryDirectory() as d:
            argv = sys.argv
            try:
                sys.argv = ["gen.py", d, os.path.join(d, "out"), "--no-game", "--game", "x.rpx"]
                with self.assertRaises(SystemExit):
                    gen._main()
            finally:
                sys.argv = argv


if __name__ == "__main__":
    unittest.main()
