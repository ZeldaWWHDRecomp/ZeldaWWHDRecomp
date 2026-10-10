#!/usr/bin/env python3
"""Add an authored runtime object to a debug Python/SDK package for APK update tests.

Uses the pinned desktop NDK only to build this tiny runtime SDK object. The
emulator must still translate, compile and link with our Android-hosted compiler.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

from package_runtime import NDK_REVISION, TRIPLES
from prepare_python import write_entry


def sha(data):
    return hashlib.sha256(data).hexdigest()


def update(package, ndk, revision, fail_link, output):
    package, ndk, output = package.resolve(), ndk.resolve(), output.resolve()
    if output.exists(): raise ValueError("Output must be a new directory")
    if output.is_relative_to(package) or package.is_relative_to(output): raise ValueError("Output must be separate from the package")
    if type(revision) is not int or revision < 1: raise ValueError("Positive fixture revision required")
    properties = dict(line.split("=", 1) for line in (ndk / "source.properties").read_text().splitlines() if "=" in line)
    if next((v.strip() for k, v in properties.items() if k.strip() == "Pkg.Revision"), None) != NDK_REVISION:
        raise ValueError("Unexpected NDK revision")
    with zipfile.ZipFile(package / "assets/python-source.zip") as bundle:
        entries = {name: bundle.read(name) for name in bundle.namelist()}
    if "tools/android/service_fixture.py" not in entries:
        raise ValueError("Requires explicitly packaged debug service fixture")
    manifest = json.loads(entries["sdk/manifest.json"])
    metadata = json.loads((package / "assets/runtime-sdk.json").read_text())
    original_identity = manifest.pop("identity")
    if original_identity != sha(json.dumps(manifest, sort_keys=True).encode()) or metadata["identity"] != original_identity:
        raise ValueError("Runtime SDK identity mismatch")
    if manifest["abi"] not in TRIPLES or manifest["abi"] != metadata["abi"] or manifest["host_api"] != 1 or manifest["api"] != 33:
        raise ValueError("Unsupported runtime fixture ABI/API")
    files = {name[4:]: sha(data) for name, data in entries.items() if name.startswith("sdk/") and name != "sdk/manifest.json"}
    if files != manifest["files"]: raise ValueError("Runtime SDK file inventory mismatch")
    if "update_fixture_revision" in manifest: raise ValueError("Requires the original runtime SDK")
    tools = list(ndk.glob("toolchains/llvm/prebuilt/*/bin/clang"))
    if len(tools) != 1: raise ValueError("NDK clang not uniquely located")
    with tempfile.TemporaryDirectory() as temporary:
        source, obj = Path(temporary) / "fixture.c", Path(temporary) / "fixture.o"
        source.write_text(("extern int wwhd_update_fixture_missing(void);\n" if fail_link else "") +
            '__attribute__((visibility("default"))) int wwhd_update_fixture_revision(void) { return ' +
            ("wwhd_update_fixture_missing()" if fail_link else str(revision)) + "; }\n")
        subprocess.run([str(tools[0]), "--target=" + TRIPLES[manifest["abi"]] + "-linux-android33",
                        "-fPIC", "-O2", "-c", str(source), "-o", str(obj)], check=True)
        entries["sdk/obj/update_fixture.o"] = obj.read_bytes()
    manifest["files"]["obj/update_fixture.o"] = sha(entries["sdk/obj/update_fixture.o"])
    manifest["link"] += ["{sdk}/obj/update_fixture.o", "-Wl,-u,wwhd_update_fixture_revision", "-Wl,-z,defs"]
    manifest["update_fixture_revision"] = revision
    manifest["update_fixture_fail_link"] = fail_link
    manifest["identity"] = sha(json.dumps(manifest, sort_keys=True).encode())
    entries["sdk/manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    sdk_bytes = io.BytesIO()
    with zipfile.ZipFile(sdk_bytes, "w") as sdk:
        for name, data in sorted(entries.items()):
            if name.startswith("sdk/"): write_entry(sdk, name, data)
    metadata.update(identity=manifest["identity"], sha256=sha(sdk_bytes.getvalue()),
                    archive_bytes=len(sdk_bytes.getvalue()),
                    installed_bytes=sum(len(data) for name, data in entries.items() if name.startswith("sdk/")))
    shutil.copytree(package, output)
    with zipfile.ZipFile(output / "assets/python-source.zip", "w") as bundle:
        for name, data in sorted(entries.items()): write_entry(bundle, name, data)
    (output / "assets/runtime-sdk.json").write_text(json.dumps(metadata, indent=2) + "\n")
    return {"revision": revision, "fail_link": fail_link, "runtime_identity": manifest["identity"],
            "runtime_object_sha256": manifest["files"]["obj/update_fixture.o"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--revision", type=int, required=True)
    parser.add_argument("--fail-link", action="store_true")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(update(args.package, args.ndk, args.revision, args.fail_link, args.out), indent=2))


if __name__ == "__main__": main()
