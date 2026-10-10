"""Catch omission of NDK per-architecture resource libraries."""
from pathlib import Path
import tempfile
import unittest
from build_toolchain import copy_runtime_libraries


class RuntimeResourcesTests(unittest.TestCase):
    def test_includes_unwind_directory_and_selected_builtins(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "ndk"
            for arch in ("aarch64", "x86_64"):
                (source / arch).mkdir(parents=True)
                (source / arch / "libunwind.a").write_bytes((arch + " unwind fixture").encode())
                (source / ("libclang_rt.builtins-" + arch + "-android.a")).write_bytes(arch.encode())
            for arch in ("aarch64", "x86_64"):
                target = root / arch
                copy_runtime_libraries(source, target, arch)
                self.assertEqual((target / arch / "libunwind.a").read_bytes(),
                                 (source / arch / "libunwind.a").read_bytes())
                self.assertEqual((target / ("libclang_rt.builtins-" + arch + "-android.a")).read_bytes(), arch.encode())
                self.assertEqual(len(list(target.iterdir())), 2)


if __name__ == "__main__": unittest.main()
