"""Validate packaging rejection paths with authored ELF metadata, never a compiler substitute."""
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

from package_toolchain import LLVM_REVISION, NDK_REVISION, NAMES, digest, package


def elf(machine=183, interpreter=b"/system/bin/linker64\0"):
    data = bytearray(120) + interpreter
    data[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HH", data, 16, 3, machine)
    struct.pack_into("<I", data, 20, 1)
    struct.pack_into("<Q", data, 32, 64)
    struct.pack_into("<HH", data, 54, 56, 1)
    struct.pack_into("<I", data, 64, 3)
    struct.pack_into("<Q", data, 72, 120)
    struct.pack_into("<Q", data, 96, len(interpreter))
    return data


class PackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source, self.output = self.root / "compiler", self.root / "apk"
        for directory in ("bin", "lib/clang/20/include", "sysroot/usr/include", "sysroot/usr/lib", "licenses"):
            (self.source / directory).mkdir(parents=True)
        for name in NAMES: (self.source / "bin" / name).write_bytes(elf())
        (self.source / "licenses/test.txt").write_text("authored packaging fixture")
        self.metadata = {"llvm_revision": LLVM_REVISION, "ndk_revision": NDK_REVISION,
                         "abi": "arm64-v8a", "api": 33}
        self.seal()

    def seal(self):
        self.metadata["sha256"] = {str(path.relative_to(self.source)): digest(path)
                                   for path in self.source.rglob("*") if path.is_file() and path.name != "build.json"}
        (self.source / "build.json").write_text(json.dumps(self.metadata))

    def test_packages_executable_names_and_data_separately(self):
        result = package(self.source, self.output)
        self.assertEqual(result["executables"], NAMES)
        for original, renamed in NAMES.items():
            self.assertEqual((self.source / "bin" / original).read_bytes(),
                             (self.output / "jniLibs/arm64-v8a" / renamed).read_bytes())
        with zipfile.ZipFile(self.output / "assets/toolchain-data.zip") as bundle:
            self.assertIn("licenses/test.txt", bundle.namelist())
            self.assertFalse(any(name.startswith("bin/") for name in bundle.namelist()))

    def test_rejects_changed_or_added_files(self):
        (self.source / "bin/clang").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"): package(self.source, self.output)
        (self.source / "bin/clang").write_bytes(elf())
        (self.source / "extra").write_bytes(b"unexpected")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"): package(self.source, self.output)

    def test_rejects_other_source_revision(self):
        self.metadata["llvm_revision"] = "unreviewed"
        self.seal()
        with self.assertRaisesRegex(ValueError, "pinned source"): package(self.source, self.output)

    def test_rejects_wrong_abi_and_desktop_interpreter(self):
        (self.source / "bin/clang").write_bytes(elf(machine=62))
        self.seal()
        with self.assertRaisesRegex(ValueError, "selected ABI"): package(self.source, self.output)
        (self.source / "bin/clang").write_bytes(elf(interpreter=b"/lib64/ld-linux-x86-64.so.2\0"))
        self.seal()
        with self.assertRaisesRegex(ValueError, "Android dynamic interpreter"): package(self.source, self.output)

    def test_rejects_symlink(self):
        (self.source / "link").symlink_to(self.source / "bin/clang")
        self.seal()
        with self.assertRaisesRegex(ValueError, "symlinks"): package(self.source, self.output)


if __name__ == "__main__": unittest.main()
