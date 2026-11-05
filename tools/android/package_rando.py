#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Package the in-app randomizer (tools/rando's wwhd-rando, built with the pinned NDK) for the APK.

  cmake -S tools/rando -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release \\
        -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-33
  cmake --build <build>
  package_rando.py --build <build> --ndk <ndk> --abi arm64-v8a --out <dir>
  gradle ... -PwwhdRandoDir=<dir>

<dir>/jniLibs/<abi>/libwwhd-rando-driver.so (Android installs it executable in nativeLibraryDir),
<dir>/assets/rando-data/ (the randomizer's data: no game files), <dir>/assets/licenses/ (its licence texts),
<dir>/assets/rando-apk.json (its identity, checked by AndroidRandomizer.java before it runs).
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

from build_toolchain import NDK_REVISION
from package_toolchain import MACHINES, digest, validate_executable

NAME = "libwwhd-rando-driver.so"
LICENSES = {  # (beside the APK's other licence texts, assets/licenses)
    "TWWHD-Randomizer-MIT.txt": "tools/rando/twwhd_rando/LICENSE.txt",
    "yaml-cpp-MIT.txt": "tools/rando/twwhd_rando/libs/yaml-cpp/LICENSE",
    "TinyXML-2-Zlib.txt": "tools/rando/twwhd_rando/libs/tinyxml2/LICENSE.txt",
    "base64pp-MIT.txt": "tools/rando/twwhd_rando/libs/base64pp/LICENSE",
    "BS-thread-pool-MIT.txt": "tools/rando/twwhd_rando/libs/thread-pool/LICENSE.txt",
}


def package(build, ndk, abi, output):
    build, ndk, output = (Path(p).resolve() for p in (build, ndk, output))
    if abi not in MACHINES:
        raise ValueError("Unsupported randomizer ABI")
    if output == build or build in output.parents:
        raise ValueError("Randomizer package must be outside the build directory")
    properties = (ndk / "source.properties").read_text()
    if not any(line.strip() == "Pkg.Revision = " + NDK_REVISION for line in properties.splitlines()):
        raise ValueError("Randomizer requires the pinned NDK")
    cache = (build / "CMakeCache.txt").read_text().splitlines()
    values = {line.split(":", 1)[0]: line.split("=", 1)[1]
              for line in cache if ":" in line and "=" in line and not line.startswith("//")}
    if values.get("ANDROID_ABI") != abi or values.get("ANDROID_PLATFORM") != "android-33":
        raise ValueError("Randomizer build ABI/API mismatch")
    if Path(values.get("CMAKE_TOOLCHAIN_FILE", "")).resolve() != ndk / "build/cmake/android.toolchain.cmake":
        raise ValueError("Randomizer build must use the pinned NDK toolchain")
    executable, data = build / "wwhd-rando", build / "data"
    validate_executable(executable, MACHINES[abi])
    if not (data / "logic" / "location_data.yaml").is_file():
        raise ValueError("Randomizer data missing in the build (target wwhd-rando-data)")
    if output.exists():
        raise ValueError("Use a fresh randomizer package directory")
    strips = sorted((ndk / "toolchains/llvm/prebuilt").glob("*/bin/llvm-strip"))
    if len(strips) != 1:
        raise ValueError("Cannot identify NDK llvm-strip")
    native, assets = output / "jniLibs" / abi, output / "assets"
    native.mkdir(parents=True)
    assets.mkdir()
    installed = native / NAME
    shutil.copyfile(executable, installed)
    subprocess.run([str(strips[0]), "--strip-all", str(installed)], check=True)
    installed.chmod(0o755)
    validate_executable(installed, MACHINES[abi])
    shutil.copytree(data, assets / "rando-data")
    root = Path(__file__).resolve().parents[2]
    licenses = assets / "licenses"
    licenses.mkdir()
    for name, rel in LICENSES.items():
        shutil.copyfile(root / rel, licenses / name)
    files = sorted(p for p in (assets / "rando-data").rglob("*") if p.is_file())
    data_hash = digest_tree(assets / "rando-data", files)
    metadata = {"schema": 1, "abi": abi, "api": 33, "ndk_revision": NDK_REVISION, "version": "1.2.0",
                "executable": NAME, "sha256": digest(installed), "installed_bytes": installed.stat().st_size,
                "data_sha256": data_hash, "data_files": len(files)}
    (assets / "rando-apk.json").write_text(json.dumps(metadata, sort_keys=True, indent=2) + "\n")
    return metadata


def digest_tree(root, files):
    import hashlib
    h = hashlib.sha256()
    for p in files:
        h.update(p.relative_to(root).as_posix().encode() + b"\0" + bytes.fromhex(digest(p)))
    return h.hexdigest()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--ndk", required=True, type=Path)
    parser.add_argument("--abi", required=True, choices=tuple(MACHINES))
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(package(args.build, args.ndk, args.abi, args.out), indent=2))
