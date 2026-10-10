"""SDK update fixtures change real link inputs without altering the shared pipeline."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from package_update_fixture import update, sha
from package_runtime import NDK_REVISION
from prepare_python import write_entry


class UpdateFixtureTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.package, self.ndk = self.root / "package", self.root / "ndk"
        (self.package / "assets").mkdir(parents=True)
        compiler = self.ndk / "toolchains/llvm/prebuilt/host/bin/clang"
        compiler.parent.mkdir(parents=True)
        compiler.touch()
        (self.ndk / "source.properties").write_text("Pkg.Revision = " + NDK_REVISION)
        self.entries = {"sdk/obj/runtime.o": b"authored runtime", "sdk/include/cpu.h": b"authored header",
                        "tools/android/service_fixture.py": b"explicit debug fixture",
                        "tools/recomp/recomp.py": b"shared recompiler", "tools/installer/setup.py": b"shared setup"}
        self.manifest = {"schema": 1, "host_api": 1, "abi": "arm64-v8a", "api": 33,
                         "files": {name[4:]: sha(data) for name, data in self.entries.items() if name.startswith("sdk/")},
                         "link": ["-shared", "{sdk}/obj/runtime.o", "{gamecode}"]}
        self.manifest["identity"] = sha(json.dumps(self.manifest, sort_keys=True).encode())
        self.entries["sdk/manifest.json"] = json.dumps(self.manifest).encode()
        (self.package / "assets/runtime-sdk.json").write_text(json.dumps({"abi": "arm64-v8a", "identity": self.manifest["identity"]}))
        self.write_package()

    def write_package(self):
        with zipfile.ZipFile(self.package / "assets/python-source.zip", "w") as bundle:
            for name, data in self.entries.items(): write_entry(bundle, name, data)

    def compile(self, command, **kwargs):
        # Packaging unit only: emulator acceptance uses actual NDK objects and on-phone linking.
        Path(command[-1]).write_bytes(Path(command[command.index("-c") + 1]).read_bytes())

    def test_changed_runtime_bytes_and_inventory_preserve_shared_scripts(self):
        with patch("package_update_fixture.subprocess.run", side_effect=self.compile):
            first = update(self.package, self.ndk, 1, False, self.root / "one")
            second = update(self.package, self.ndk, 2, False, self.root / "two")
        self.assertNotEqual(first["runtime_identity"], second["runtime_identity"])
        self.assertNotEqual(first["runtime_object_sha256"], second["runtime_object_sha256"])
        with zipfile.ZipFile(self.root / "two/assets/python-source.zip") as bundle:
            manifest = json.loads(bundle.read("sdk/manifest.json"))
            for name, data in self.entries.items():
                if name != "sdk/manifest.json": self.assertEqual(bundle.read(name), data)
            self.assertEqual(manifest["files"]["obj/update_fixture.o"], sha(bundle.read("sdk/obj/update_fixture.o")))
            identity = manifest.pop("identity")
            self.assertEqual(identity, sha(json.dumps(manifest, sort_keys=True).encode()))
            self.assertIn("{sdk}/obj/update_fixture.o", manifest["link"])

    def test_failed_update_has_an_actual_unresolved_call(self):
        with patch("package_update_fixture.subprocess.run", side_effect=self.compile):
            update(self.package, self.ndk, 3, True, self.root / "bad")
        with zipfile.ZipFile(self.root / "bad/assets/python-source.zip") as bundle:
            self.assertIn(b"wwhd_update_fixture_missing()", bundle.read("sdk/obj/update_fixture.o"))
            self.assertIn("-Wl,-z,defs", json.loads(bundle.read("sdk/manifest.json"))["link"])

    def test_rejects_changed_runtime_bytes_without_receipt(self):
        self.entries["sdk/obj/runtime.o"] = b"corrupt"
        self.write_package()
        with self.assertRaisesRegex(ValueError, "inventory mismatch"):
            update(self.package, self.ndk, 1, False, self.root / "bad")

    def test_rejects_non_fixture_and_nested_output(self):
        with self.assertRaisesRegex(ValueError, "separate"):
            update(self.package, self.ndk, 1, False, self.package / "nested")
        del self.entries["tools/android/service_fixture.py"]
        self.write_package()
        with self.assertRaisesRegex(ValueError, "debug service fixture"):
            update(self.package, self.ndk, 1, False, self.root / "bad")
