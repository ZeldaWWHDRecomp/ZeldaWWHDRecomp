#!/usr/bin/env python3
"""The Apple device build's tools resolve in a packaged release's layout (tools/apple, tools/guestmod,
sdk/include), not only in a source checkout. Lays out the files package.py ships, without building anything."""
import importlib.util
import os
import shutil
import sys
import tempfile
import types
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


package = load("package_for_layout", os.path.join(ROOT, "tools", "release", "package.py"))


class ReleaseLayout(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="wwhd-apple-layout-")
        self.pkg = os.path.join(self.tmp, "WindWakerHD")
        for rel in package.TOOL_FILES + package.INSTALLER_FILES:
            dst = os.path.join(self.pkg, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(os.path.join(ROOT, rel), dst)
        for rel in package.APPLE_TOOL_FILES:
            dst = os.path.join(self.pkg, "tools", "apple", os.path.basename(rel))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(os.path.join(ROOT, rel), dst)
        shutil.copytree(os.path.join(ROOT, "runtime", "include"), os.path.join(self.pkg, "sdk", "include"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_bundle_mods_finds_the_builder_and_headers(self):
        bm = load("bundle_mods_in_release", os.path.join(self.pkg, "tools", "apple", "bundle_mods.py"))
        self.assertEqual(os.path.realpath(bm.REPO), os.path.realpath(self.pkg))
        self.assertTrue(bm.guest_mod_builder().is_file(), bm.guest_mod_builder())
        self.assertEqual(os.path.realpath(bm.default_include()), os.path.realpath(os.path.join(self.pkg, "sdk", "include")))

    def test_apple_tools_resolve_inside_the_release(self):
        installer = os.path.join(self.pkg, "tools", "installer")
        sys.path.insert(0, installer)
        try:
            ad = load("apple_device_in_release", os.path.join(installer, "apple_device.py"))
        finally:
            sys.path.remove(installer)
        setup = types.SimpleNamespace(PKG=self.pkg)
        for name in ("bundle_mods.py", "make_icon.py"):
            got = os.path.realpath(ad.apple_tool(setup, name))
            self.assertEqual(got, os.path.realpath(os.path.join(self.pkg, "tools", "apple", name)))


if __name__ == "__main__":
    unittest.main()
