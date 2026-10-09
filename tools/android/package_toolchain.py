#!/usr/bin/env python3
"""Validate our CI compiler package and prepare APK-installed executables.

Android installs executable ELF files from lib/<abi>/lib*.so into its native
library directory when native-library extraction is enabled. The compiler data
stays in assets; no downloaded executable is run from writable app storage.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zipfile

from build_toolchain import LLVM_REVISION, NDK_REVISION

NAMES = {"clang": "libwwhd-clang-driver.so", "ld.lld": "libwwhd-ld.lld-driver.so",
         "llvm-ar": "libwwhd-ar-driver.so"}
MACHINES = {"arm64-v8a": 183, "x86_64": 62}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validate_executable(path, machine):
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) != 64 or header[:7] != b"\x7fELF\x02\x01\x01" or struct.unpack_from("<I", header, 20)[0] != 1:
            raise ValueError("compiler must be a little-endian ELF64: " + str(path))
        kind, actual_machine = struct.unpack_from("<HH", header, 16)
        if kind != 3 or actual_machine != machine:
            raise ValueError("compiler must be a PIE executable for the selected ABI")
        offset = struct.unpack_from("<Q", header, 32)[0]
        stride, count = struct.unpack_from("<HH", header, 54)
        if stride < 56 or count > 1024:
            raise ValueError("invalid compiler program headers")
        for index in range(count):
            stream.seek(offset + index * stride)
            entry = stream.read(56)
            if len(entry) != 56: raise ValueError("truncated compiler program headers")
            if struct.unpack_from("<I", entry)[0] == 3:  # PT_INTERP
                location = struct.unpack_from("<Q", entry, 8)[0]
                size = struct.unpack_from("<Q", entry, 32)[0]
                if size > 256: raise ValueError("invalid compiler interpreter")
                stream.seek(location)
                if stream.read(size).rstrip(b"\0") != b"/system/bin/linker64":
                    raise ValueError("compiler does not use the Android dynamic interpreter")
                return
    raise ValueError("compiler is missing its Android dynamic interpreter")


def package(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    if output == source or source in output.parents:
        raise ValueError("APK output must be outside the compiler source package")
    metadata = json.loads((source / "build.json").read_text())
    if metadata.get("llvm_revision") != LLVM_REVISION or metadata.get("ndk_revision") != NDK_REVISION:
        raise ValueError("compiler package does not match our pinned source/NDK")
    abi = metadata.get("abi")
    if abi not in MACHINES or metadata.get("api") != 33:
        raise ValueError("unsupported compiler ABI/API")
    if any(path.is_symlink() for path in source.rglob("*")):
        raise ValueError("compiler package must not contain symlinks")
    files = {str(path.relative_to(source)): digest(path) for path in sorted(source.rglob("*"))
             if path.is_file() and path != source / "build.json"}
    if files != metadata.get("sha256"):
        raise ValueError("compiler package inventory/checksum mismatch")
    for executable in NAMES:
        validate_executable(source / "bin" / executable, MACHINES[abi])
    for required in ("lib/clang/20/include", "sysroot/usr/include", "sysroot/usr/lib", "licenses"):
        if not (source / required).is_dir(): raise ValueError("missing compiler data: " + required)
    if output.exists(): raise ValueError("use a fresh APK package directory")
    native, assets = output / "jniLibs" / abi, output / "assets"
    native.mkdir(parents=True)
    assets.mkdir()
    for executable, name in NAMES.items():
        shutil.copyfile(source / "bin" / executable, native / name)
        (native / name).chmod(0o755)
    with zipfile.ZipFile(assets / "toolchain-data.zip", "w", zipfile.ZIP_DEFLATED) as bundle:
        for path in sorted(source.rglob("*")):
            if path.is_file() and path.relative_to(source).parts[0] != "bin":
                bundle.write(path, path.relative_to(source))
    result = {"abi": abi, "api": 33, "llvm_revision": LLVM_REVISION,
              "compiler_identity": digest(source / "build.json"),
              "executables": NAMES,
              "sha256": {name: digest(native / name) for name in NAMES.values()},
              "data_sha256": digest(assets / "toolchain-data.zip")}
    (assets / "toolchain-apk.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(package(args.compiler, args.out), indent=2))


if __name__ == "__main__":
    main()
