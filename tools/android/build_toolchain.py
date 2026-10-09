#!/usr/bin/env python3
"""Build our Android-hosted clang/lld from the locked upstream LLVM revision.

The result runs on Android, unlike the desktop-hosted compiler in the NDK.
Packaging the executables into an APK and app-level execution tests are separate.
"""
import argparse
import hashlib
import json
import platform
from pathlib import Path
import shutil
import subprocess
import time

LLVM_REVISION = "87f0227cb60147a26a1eeb4fb06e3b505e9c7261"  # llvmorg-20.1.8
NDK_REVISION = "30.0.16248370"
TARGETS = {
    "arm64-v8a": ("AArch64", "aarch64-linux-android"),
    "x86_64": ("X86", "x86_64-linux-android"),
}


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def copy_runtime_libraries(resource_lib, destination_root, arch):
    for lib in resource_lib.glob("*" + arch + "*"):
        destination = destination_root / lib.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        if lib.is_file():
            shutil.copy2(lib, destination)
        elif lib.is_dir():
            # NDK 30 puts libunwind.a in a per-architecture directory rather
            # than encoding the architecture in the archive's filename.
            shutil.copytree(lib, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--ndk", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--abi", required=True, choices=TARGETS)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    source, ndk, out = args.source.resolve(), args.ndk.resolve(), args.out.resolve()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != LLVM_REVISION:
        parser.error("LLVM source must be exactly " + LLVM_REVISION)
    if subprocess.check_output(["git", "-C", str(source), "status", "--porcelain"], text=True).strip():
        parser.error("LLVM source must have no local modifications")
    properties = {key.strip(): value.strip() for key, value in
                  (line.split("=", 1) for line in (ndk / "source.properties").read_text().splitlines() if "=" in line)}
    if properties.get("Pkg.Revision") != NDK_REVISION:
        parser.error("NDK must be exactly " + NDK_REVISION)
    backend, triple = TARGETS[args.abi]
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    ndk_tools = ndk / "toolchains/llvm/prebuilt" / host
    native, target, package = out / "native", out / "target", out / "package"
    if package.exists():
        parser.error("remove the old package directory before rebuilding")
    common = ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
              "-DLLVM_INCLUDE_TESTS=OFF", "-DLLVM_INCLUDE_BENCHMARKS=OFF",
              "-DLLVM_INCLUDE_EXAMPLES=OFF", "-DLLVM_ENABLE_ZLIB=OFF",
              "-DLLVM_ENABLE_ZSTD=OFF", "-DLLVM_ENABLE_LIBXML2=OFF",
              "-DLLVM_ENABLE_LIBEDIT=OFF", "-DLLVM_ENABLE_TERMINFO=OFF",
              "-DLLVM_ENABLE_BINDINGS=OFF", "-DLLVM_ENABLE_PROJECTS=clang;lld",
              "-DLLVM_TARGETS_TO_BUILD=" + backend]
    start = time.monotonic()
    run("cmake", "-S", source / "llvm", "-B", native, *common)
    run("cmake", "--build", native, "--parallel", args.jobs,
        "--target", "llvm-tblgen", "clang-tblgen")
    run("cmake", "-S", source / "llvm", "-B", target, *common,
        "-DCMAKE_TOOLCHAIN_FILE=" + str(ndk / "build/cmake/android.toolchain.cmake"),
        "-DANDROID_ABI=" + args.abi, "-DANDROID_PLATFORM=android-33",
        "-DANDROID_STL=c++_static", "-DLLVM_HOST_TRIPLE=" + triple,
        "-DLLVM_DEFAULT_TARGET_TRIPLE=" + triple + "33",
        "-DLLVM_TABLEGEN=" + str(native / "bin/llvm-tblgen"),
        "-DCLANG_TABLEGEN=" + str(native / "bin/clang-tblgen"),
        "-DLLVM_NATIVE_TOOL_DIR=" + str(native / "bin"),
        "-DLLVM_ENABLE_THREADS=ON", "-DLLVM_ENABLE_PIC=ON")
    run("cmake", "--build", target, "--parallel", args.jobs,
        "--target", "clang", "lld", "llvm-ar", "clang-resource-headers")
    (package / "bin").mkdir(parents=True)
    for name, built in (("clang", "clang"), ("ld.lld", "lld"), ("llvm-ar", "llvm-ar")):
        destination = package / "bin" / name
        shutil.copy2(target / "bin" / built, destination)
        run(ndk_tools / "bin/llvm-strip", "--strip-unneeded", destination)
    shutil.copytree(target / "lib/clang/20/include", package / "lib/clang/20/include")
    # Retain only this target's sysroot. Headers are common; target libraries
    # include API-specific bionic stubs and CRT objects needed for linking.
    shutil.copytree(ndk_tools / "sysroot/usr/include", package / "sysroot/usr/include")
    shutil.copytree(ndk_tools / "sysroot/usr/lib" / triple, package / "sysroot/usr/lib" / triple)
    # NDK resource directory version is discovered rather than assumed.
    versions = list((ndk_tools / "lib/clang").glob("*/lib/linux"))
    if len(versions) != 1:
        raise RuntimeError("unexpected NDK compiler-rt layout")
    resource_lib = versions[0]
    arch = "aarch64" if args.abi == "arm64-v8a" else "x86_64"
    copy_runtime_libraries(resource_lib, package / "lib/clang/20/lib/linux", arch)
    (package / "licenses").mkdir()
    shutil.copy2(source / "llvm/LICENSE.TXT", package / "licenses/LLVM.txt")
    shutil.copy2(ndk / "NOTICE", package / "licenses/NDK.txt")
    shutil.copy2(ndk / "NOTICE.toolchain", package / "licenses/NDK-toolchain.txt")
    files = sorted(p for p in package.rglob("*") if p.is_file())
    metadata = {"llvm_revision": revision, "ndk_revision": NDK_REVISION,
                "abi": args.abi, "api": 33, "build_seconds": time.monotonic() - start,
                "installed_bytes": sum(p.stat().st_size for p in files),
                "sha256": {str(p.relative_to(package)): digest(p) for p in files}}
    (package / "build.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps({k: v for k, v in metadata.items() if k != "sha256"}, indent=2))


if __name__ == "__main__":
    main()
