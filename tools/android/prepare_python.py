#!/usr/bin/env python3
"""Package checksum-pinned official CPython Android builds and our JNI bridge.

Generated resources stay under build/. Pass -PwwhdPythonDir=.../package to
Gradle to include them. No compiler/recompiler other than our existing pipeline
is downloaded by this script; the NDK only builds the small embedding bridge.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import time
import urllib.request
import zipfile

VERSION = "3.14.8"
BUILDS = {
    "arm64-v8a": ("aarch64-linux-android", "11324313957da3736f44e90257304d288864321a4dd376ed0cc35a54eacf9a33"),
    "x86_64": ("x86_64-linux-android", "58eb3b2d76ef57e076985a0a6b093cf08906d65ea4ab78cccc6d894d4250c972"),
}


def write_entry(bundle, name, data):
    # Resource identity must depend on bytes, never local mtimes. Otherwise an
    # identical app rebuild would invalidate completed translation/compilation.
    item = zipfile.ZipInfo(str(name).replace("\\", "/"), date_time=(1980, 1, 1, 0, 0, 0))
    item.compress_type = zipfile.ZIP_DEFLATED
    item.external_attr = 0o100644 << 16
    item.create_system = 3
    bundle.writestr(item, data)


def verified_download(url, archive, expected):
    """Publish only checksum-verified bytes; interrupted transfers restart safely."""
    archive = Path(archive)
    temporary = archive.with_name(archive.name + ".partial")
    # A partial transfer is never a cache hit, including after process death.
    temporary.unlink(missing_ok=True)
    if archive.exists():
        with archive.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() == expected:
                return archive
        archive.unlink()  # A damaged cache must not poison every later retry.
    try:
        digest = hashlib.sha256()
        with urllib.request.urlopen(url, timeout=60) as response, temporary.open("wb") as stream:
            while block := response.read(1024 * 1024):
                digest.update(block)
                stream.write(block)
            if digest.hexdigest() != expected:
                raise ValueError("CPython archive checksum mismatch")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, archive)
        directory = os.open(archive.parent, os.O_RDONLY)
        try: os.fsync(directory)
        finally: os.close(directory)
        return archive
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--abi", choices=BUILDS, required=True)
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runtime-sdk", type=Path, help="validated package_runtime.py output")
    parser.add_argument("--extraction-fixture", type=Path, help="test-only authored ZArchive")
    parser.add_argument("--service-fixture", action="store_true", help="include debug-only synthetic service entry point; never use in release packages")
    args = parser.parse_args()
    started = time.monotonic()
    root = Path(__file__).resolve().parents[2]
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    host, expected = BUILDS[args.abi]
    archive = out / "python.tar.gz"
    url = f"https://www.python.org/ftp/python/{VERSION}/python-{VERSION}-{host}.tar.gz"
    verified_download(url, archive, expected)
    extracted = out / "upstream"
    if extracted.exists(): shutil.rmtree(extracted)
    extracted.mkdir()
    with tarfile.open(archive) as bundle:
        bundle.extractall(extracted, filter="data")
    prefix = extracted / "prefix"
    package = out / "package"
    if package.exists(): shutil.rmtree(package)
    native = package / "jniLibs" / args.abi
    assets = package / "assets"
    native.mkdir(parents=True)
    assets.mkdir()
    for name in ("libpython3.14.so", "libcrypto_python.so", "libssl_python.so", "libsqlite3_python.so"):
        shutil.copyfile(prefix / "lib" / name, native / name)
    compilers = list(args.ndk.glob("toolchains/llvm/prebuilt/*/bin/clang"))
    if len(compilers) != 1: raise ValueError("NDK clang not uniquely located")
    subprocess.run([str(compilers[0]), f"--target={host}33", "-shared", "-fPIC",
                    "-Wl,-z,max-page-size=16384", "-Wl,-z,defs", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(prefix / "include/python3.14"), str(root / "android/python/bridge.c"),
                    str(root / "android/python/process.c"),
                    "-L" + str(prefix / "lib"), "-lpython3.14", "-ldl", "-o", str(native / "libwwhdpython.so")], check=True)
    with zipfile.ZipFile(assets / "python-home.zip", "w", zipfile.ZIP_DEFLATED) as bundle:
        for path in sorted((prefix / "lib/python3.14").rglob("*")):
            if path.is_file() and "__pycache__" not in path.parts:
                write_entry(bundle, path.relative_to(prefix), path.read_bytes())
    source_paths = [root / "tools/installer/setup.py", root / "tools/android/setup_adapter.py",
                    root / "tools/android/native_process.py",
                    root / "tools/android/ondevice_setup.py",
                    root / "tools/android/embedding_smoke.py", root / "tools/rpx.py"]
    source_paths += [path for path in (root / "tools/recomp").rglob("*")
                     if path.is_file() and path.suffix in (".py", ".txt") and "__pycache__" not in path.parts]
    with zipfile.ZipFile(assets / "python-source.zip", "w", zipfile.ZIP_DEFLATED) as bundle:
        for path in sorted(source_paths): write_entry(bundle, path.relative_to(root), path.read_bytes())
        if args.service_fixture:
            write_entry(bundle, "tools/android/service_fixture.py", (root / "tools/android/service_fixture.py").read_bytes())
        if args.extraction_fixture:
            write_entry(bundle, "tools/android/extraction-fixture.bin", args.extraction_fixture.read_bytes())
            write_entry(bundle, "tools/android/extraction-fixture.json", args.extraction_fixture.with_suffix(".json").read_bytes())
        if args.runtime_sdk:
            sdk_assets = args.runtime_sdk / "assets"
            metadata = json.loads((sdk_assets / "runtime-sdk.json").read_text())
            archive_path = sdk_assets / "runtime-sdk.zip"
            with archive_path.open("rb") as stream:
                actual = hashlib.file_digest(stream, "sha256").hexdigest()
            if metadata["abi"] != args.abi or actual != metadata["sha256"]:
                raise ValueError("Runtime SDK architecture/checksum mismatch")
            with zipfile.ZipFile(archive_path) as sdk:
                for item in sdk.infolist():
                    name = Path(item.filename)
                    if name.is_absolute() or ".." in name.parts or name.parts[0] != "sdk":
                        raise ValueError("Invalid runtime SDK path")
                    write_entry(bundle, item.filename, sdk.read(item))
            shutil.copyfile(sdk_assets / "runtime-sdk.json", assets / "runtime-sdk.json")
            # Authored placeholders satisfy runtime references absent from the
            # tiny synthetic translation; they are used only by the debug probe.
            import sys
            sys.path.insert(0, str(root / "tools/recomp"))
            import stubgen
            import tempfile
            with tempfile.TemporaryDirectory(dir=out) as temporary:
                stubgen.main(temporary)
                write_entry(bundle, "tools/android/runtime_fixture.c", (Path(temporary) / "code_000.c").read_bytes())
        else:
            for path in sorted((root / "runtime/include").rglob("*")):
                if path.is_file(): write_entry(bundle, Path("sdk/include") / path.relative_to(root / "runtime/include"), path.read_bytes())
    metrics = {"version": VERSION, "abi": args.abi, "url": url, "sha256": expected,
               "prepare_seconds": time.monotonic() - started,
               "package_bytes": sum(path.stat().st_size for path in package.rglob("*") if path.is_file())}
    (assets / "python-build.json").write_text(json.dumps(metrics, indent=2) + "\n")
    print(json.dumps(metrics, indent=2))


if __name__ == "__main__":
    main()
