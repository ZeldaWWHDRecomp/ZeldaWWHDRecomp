#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The release's Python tools start as the Windows release starts them (issue #280).

The Windows release runs its tools with the official embeddable Python (tools/python). Its python314._pth sets
sys.path to exactly the lines it lists (the standard library zip and tools/python itself), turns on isolated mode and
leaves out site (no site-packages). So the folder of a script is not on sys.path, and a tool that imports a sibling
module (actor_model.py imports notebook_model.py) must put its own folder there itself. On Windows every own model of
the world editor failed to prepare with "No module named 'notebook_model'" until the tools located their siblings; a
player worked around it by adding "..\\story_mods" to the ._pth by hand. The ._pth stays the official file
(python-windows-files.json pins it): the tools find their own modules, on every platform and interpreter mode.

Checks, with no game files:
- every tools/story_mods script the game starts (runtime/src/mods/*.cpp) is a file the release ships (TOOL_FILES);
- the game starts each of them through the bootstrap that puts the tool's folder on sys.path (BuildBridge::
  tool_python, runtime/src/mods/guest_build.h), and each starts that way under the release Python with the tool's own
  sys.path line taken out (the bootstrap alone is enough);
- every shipped tool (TOOL_FILES and INSTALLER_FILES), copied into a folder laid out as the release is, starts from
  another working directory under an interpreter that reads a copy of the official ._pth (macOS, Linux; "-I -S"
  where that cannot be built), and every module it and the modules it loads import later, inside functions, resolves.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import sysconfig
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import package  # noqa: E402

# The official python314._pth of the pinned embeddable zip (python-windows-files.json has its SHA-256).
OFFICIAL_PTH = b"python314.zip\r\n.\r\n\r\n# Uncomment to run site.main() automatically\r\n#import site\r\n"
# Shipped tools that need a package the embeddable Python doesn't have; the game and setup never start them.
NEEDS_PACKAGES = {"tools/wudextract.py": "Crypto"}  # manual disc reader: pycryptodome, see its usage text
# Imports inside functions that put their module's folder on sys.path right before (the probe can't see that).
PLACED_IMPORTS = {"guard"}  # shaderprep.py guard_template: tools/release

# Runs inside the release interpreter (no access to this file's folder): loads a tool (a script: it has a main) without
# running its main, then
# imports every module that the tool and each project module it loaded import anywhere (also inside functions), the
# way the tool's own code would later. Prints the names that do not resolve, as JSON.
PROBE = r'''
import ast, importlib, json, os, runpy, sys
tool, tree, placed = sys.argv[1], os.path.realpath(sys.argv[2]), set(sys.argv[3].split(","))
sys.argv = [tool, "--help"]
runpy.run_path(tool, run_name="__wwhd_probe__")
stdlib = set(sys.stdlib_module_names)
seen, missing = set(), {}
def ours(module):
    path = getattr(module, "__file__", None)
    return (module.__name__.split(".")[0] not in stdlib and bool(path) and os.path.isfile(path)
            and os.path.realpath(path).startswith(tree + os.sep))
def imports(path):
    for node in ast.walk(ast.parse(open(path, encoding="utf-8").read())):
        if isinstance(node, ast.Import):
            yield from (alias.name for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and not node.level and node.module:
            yield node.module
pending = [tool] + [m.__file__ for m in list(sys.modules.values()) if ours(m)]
while pending:
    path = os.path.realpath(pending.pop())
    if path in seen:
        continue
    seen.add(path)
    for name in imports(path):
        if name.split(".")[0] in stdlib or name in placed:
            continue
        try:
            module = importlib.import_module(name)
        except ImportError as error:
            missing.setdefault(name, os.path.relpath(path, tree) + ": " + str(error))
            continue
        if ours(module):
            pending.append(module.__file__)
print("WWHD-PROBE " + json.dumps(missing, sort_keys=True))
'''


def started_by_the_game():
    """story_mods scripts the runtime starts: the "<name>.py" strings of its mod sources that are such scripts"""
    names = set()
    folder = os.path.join(ROOT, "runtime", "src", "mods")
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".cpp"):
            continue
        with open(os.path.join(folder, name), encoding="utf-8", errors="replace") as f:
            text = f.read()
        if '"story_mods"' not in text:
            continue
        for script in re.findall(r'"([a-z_]+\.py)"', text):
            if os.path.isfile(os.path.join(ROOT, "tools", "story_mods", script)):
                names.add("tools/story_mods/" + script)
    return names


def bootstrap():
    """BuildBridge::tool_bootstrap: the Python code the game runs a tool with (-c CODE TOOL ARGS...)"""
    with open(os.path.join(ROOT, "runtime", "src", "mods", "guest_build.h"), encoding="utf-8") as f:
        match = re.search(r'inline constexpr const char\* tool_bootstrap=\s*((?:"(?:[^"\\]|\\.)*"\s*)+);', f.read())
    return "".join(json.loads(part) for part in re.findall(r'"(?:[^"\\]|\\.)*"', match.group(1)))


def release_python(package_dir):
    """[interpreter] of a release-like tools/python: a link to this Python next to a copy of the official ._pth, with
    the zip line pointing at this Python's standard library. None where the platform can't (Windows: python.exe
    needs its DLL beside it)."""
    if os.name == "nt":
        return None
    folder = os.path.join(package_dir, "tools", "python")
    os.makedirs(folder)
    executable = os.path.join(folder, "python")
    os.symlink(os.path.realpath(sys.executable), executable)
    stdlib = [sysconfig.get_path("stdlib"), sysconfig.get_path("platstdlib")]
    dynload = os.path.join(sysconfig.get_path("platstdlib"), "lib-dynload")
    lines = OFFICIAL_PTH.decode().replace("\r\n", "\n").split("\n")
    lines = [entry for line in lines for entry in (stdlib + [dynload] if line == "python314.zip" else [line])]
    with open(executable + "._pth", "w") as f:
        f.write("\n".join(dict.fromkeys(lines)))
    # Python reads the ._pth next to the path it resolves itself to; where that is the real interpreter (Linux follows
    # the link), the copy is never read and the caller falls back to -I -S, which isolates the same way.
    probe = subprocess.run([executable, "-c", "import sys; print(sys.flags.isolated, sys.flags.no_site)"],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, timeout=60)
    if probe.stdout.split() != ["1", "1"]:
        return None
    return [executable]


class ShippedTools(unittest.TestCase):
    def test_official_pth_is_the_pinned_one(self):
        for pins in ("python-windows-files.json", "python-windows-arm64-files.json"):
            with open(os.path.join(HERE, pins)) as f:
                self.assertEqual(json.load(f)["files"]["python314._pth"], hashlib.sha256(OFFICIAL_PTH).hexdigest(), pins)

    def test_every_tool_the_game_starts_is_shipped(self):
        started = started_by_the_game()
        self.assertIn("tools/story_mods/actor_model.py", started)
        self.assertIn("tools/story_mods/kit_model.py", started)
        missing = sorted(started - set(package.TOOL_FILES))
        self.assertEqual(missing, [], "the game starts these tools, but the release doesn't ship them")

    def test_the_game_starts_story_tools_through_the_bootstrap(self):
        folder = os.path.join(ROOT, "runtime", "src", "mods")
        direct = []
        for name in sorted(os.listdir(folder)):
            if name.endswith(".cpp"):
                with open(os.path.join(folder, name), encoding="utf-8", errors="replace") as f:
                    text = f.read()
                if '"story_mods"' in text and re.search(r"bridge\.python\b", text):
                    direct.append(name)
        self.assertEqual(direct, [], "these start a story_mods tool with bridge.python, not bridge.tool_python()")
        with tempfile.TemporaryDirectory() as temporary:
            temporary = os.path.realpath(temporary)
            tree = os.path.join(temporary, "release")
            for rel in package.TOOL_FILES:
                target = os.path.join(tree, rel)
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
                    text = f.read()
                if rel in started_by_the_game():  # without its own line, the bootstrap must find the siblings
                    text = text.replace("sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))", "pass")
                with open(target, "w", encoding="utf-8") as f:
                    f.write(text)
            python = release_python(tree) or [sys.executable, "-I", "-S"]
            elsewhere = os.path.join(temporary, "elsewhere")
            os.makedirs(elsewhere)
            env = {k: v for k, v in os.environ.items() if k not in ("PYTHONPATH", "PYTHONHOME", "PYTHONSTARTUP")}
            env["PYTHONDONTWRITEBYTECODE"] = "1"
            for rel in sorted(started_by_the_game()):
                with self.subTest(tool=rel):
                    result = subprocess.run(python + ["-c", bootstrap(), os.path.join(tree, rel), "--help"], cwd=elsewhere,
                                            env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=120)
                    self.assertNotIn("ModuleNotFoundError", result.stderr)
                    self.assertEqual(result.returncode, 0, result.stderr[-400:])

    def test_shipped_tools_start_under_the_release_python(self):
        shipped = [rel for rel in package.TOOL_FILES + package.INSTALLER_FILES if rel.endswith(".py")]
        self.assertIn("tools/story_mods/actor_model.py", shipped)
        with tempfile.TemporaryDirectory() as temporary:
            temporary = os.path.realpath(temporary)
            tree = os.path.join(temporary, "release")
            for rel in package.TOOL_FILES + package.INSTALLER_FILES:
                target = os.path.join(tree, rel)
                os.makedirs(os.path.dirname(target), exist_ok=True)
                shutil.copy2(os.path.join(ROOT, rel), target)
            python = release_python(tree) or [sys.executable, "-I", "-S"]
            probe = os.path.join(temporary, "probe", "probe.py")
            os.makedirs(os.path.dirname(probe))
            with open(probe, "w") as f:
                f.write(PROBE)
            elsewhere = os.path.join(temporary, "elsewhere")
            os.makedirs(elsewhere)
            env = {k: v for k, v in os.environ.items() if k not in ("PYTHONPATH", "PYTHONHOME", "PYTHONSTARTUP")}
            env["PYTHONDONTWRITEBYTECODE"] = "1"

            def run(*arguments):
                return subprocess.run(python + list(arguments), cwd=elsewhere, env=env, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, text=True, timeout=120)

            flags = run("-c", "import sys; print(sys.flags.isolated, sys.flags.no_site, 'site' in sys.modules, "
                              "any('site-packages' in p for p in sys.path))")
            self.assertEqual(flags.stdout.split(), ["1", "1", "False", "False"], flags.stderr)
            for rel in shipped:
                with self.subTest(tool=rel):
                    started = run(os.path.join(tree, rel), "--help")
                    if rel in NEEDS_PACKAGES:
                        self.assertIn("No module named '%s'" % NEEDS_PACKAGES[rel], started.stderr)
                        continue
                    self.assertNotIn("ModuleNotFoundError", started.stderr)
                    self.assertNotIn("ImportError", started.stderr)
                    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
                        if "__main__" not in f.read():
                            continue  # a library: its imports are checked with the tools that load it
                    probed = run(probe, os.path.join(tree, rel), tree, ",".join(sorted(PLACED_IMPORTS)))
                    lines = [line for line in probed.stdout.splitlines() if line.startswith("WWHD-PROBE ")]
                    self.assertTrue(lines, probed.stderr[-400:])
                    self.assertEqual(json.loads(lines[-1][len("WWHD-PROBE "):]), {},
                                     "imports of this tool that the release Python cannot resolve")


if __name__ == "__main__":
    unittest.main()
