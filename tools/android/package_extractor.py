#!/usr/bin/env python3
"""Package our CMake-built Android extractor for Android-installed execution."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

from build_toolchain import NDK_REVISION
from package_toolchain import MACHINES, digest, validate_executable

NAME = "libwwhd-extract-driver.so"


def package(build, ndk, abi, output):
    build, ndk, output = (Path(p).resolve() for p in (build, ndk, output))
    if abi not in MACHINES:
        raise ValueError("Unsupported extractor ABI")
    if output == build or build in output.parents:
        raise ValueError("Extractor package must be outside the build directory")
    properties = (ndk / "source.properties").read_text()
    if not any(line.strip() == "Pkg.Revision = " + NDK_REVISION for line in properties.splitlines()):
        raise ValueError("Extractor requires the pinned NDK")
    cache = (build / "CMakeCache.txt").read_text().splitlines()
    values = {line.split(":", 1)[0]: line.split("=", 1)[1]
              for line in cache if ":" in line and "=" in line and not line.startswith("//")}
    if values.get("ANDROID_ABI") != abi or values.get("ANDROID_PLATFORM") != "android-33":
        raise ValueError("Extractor build ABI/API mismatch")
    if Path(values.get("CMAKE_TOOLCHAIN_FILE", "")).resolve() != ndk / "build/cmake/android.toolchain.cmake":
        raise ValueError("Extractor build must use the pinned NDK toolchain")
    if not (build / "wwhd-zstd.txt").read_text().startswith("bundled "):
        raise ValueError("Extractor must use our pinned bundled zstd")
    executable = build / "wwhd-extract"
    validate_executable(executable, MACHINES[abi])
    if output.exists():
        raise ValueError("Use a fresh extractor package directory")
    strips = sorted((ndk / "toolchains/llvm/prebuilt").glob("*/bin/llvm-strip"))
    if len(strips) != 1:
        raise ValueError("Cannot identify NDK llvm-strip")
    native, assets = output / "jniLibs" / abi, output / "assets"
    native.mkdir(parents=True)
    assets.mkdir()
    installed = native / NAME
    shutil.copyfile(executable, installed)
    subprocess.run([str(strips[0]), "--strip-debug", str(installed)], check=True)
    installed.chmod(0o755)
    validate_executable(installed, MACHINES[abi])
    root = Path(__file__).resolve().parents[2]
    sources = sorted((root / "tools/wudextract").glob("*")) + [root / "CMakeLists.txt", root / "cmake/Zstd.cmake"]
    source_hashes = {str(p.relative_to(root)): digest(p) for p in sources if p.is_file()}
    licenses = assets / "extractor-licenses"
    licenses.mkdir()
    shutil.copyfile(root / "LICENSE", licenses / "project.txt")
    shutil.copyfile(build / "_deps/zstd-src/LICENSE", licenses / "zstd.txt")
    metadata = {"schema": 1, "abi": abi, "api": 33, "ndk_revision": NDK_REVISION,
                "executable": NAME, "sha256": digest(installed),
                "installed_bytes": installed.stat().st_size, "source_sha256": source_hashes}
    (assets / "extractor-apk.json").write_text(json.dumps(metadata, sort_keys=True, indent=2) + "\n")
    return metadata


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--ndk", required=True, type=Path)
    parser.add_argument("--abi", required=True, choices=tuple(MACHINES))
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(package(args.build, args.ndk, args.abi, args.out), indent=2))
