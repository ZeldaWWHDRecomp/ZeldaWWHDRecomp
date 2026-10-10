#!/usr/bin/env python3
"""Package Android runtime objects and their exact link recipe, excluding game code.

Like desktop release packaging, the unchanged installer later links these objects
with the user's translated code. Nothing from libgamecode.a enters this payload.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("desktop_packaging", ROOT / "tools/release/package.py")
desktop = importlib.util.module_from_spec(spec)
spec.loader.exec_module(desktop)
NDK_REVISION = "30.0.16248370"
TRIPLES = {"arm64-v8a": "aarch64", "x86_64": "x86_64"}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def link_arguments(build):
    lines = subprocess.check_output(["ninja", "-C", str(build), "-t", "commands", "wwhd"], text=True).splitlines()
    commands = [part.strip() for part in lines[-1].split(" && ")]
    matches = [command for command in commands if re.search(r'(^|\s)-o\s+"?libmain\.so"?(\s|$)', command)]
    if len(matches) != 1:
        raise ValueError("Cannot identify Android libmain.so link command")
    arguments = []
    for arg in desktop.split_command(matches[0]):
        if arg.startswith("@"):
            arguments.extend(desktop.split_command((build / arg[1:]).read_text().replace("\n", " ")))
        else:
            arguments.append(arg)
    return arguments


def recipe(arguments, build, sdk, strip):
    """Preserve object/library order, removing only host/output-specific options."""
    result, copied = [], {}
    objects = gamecode = 0
    args = iter(arguments[1:])
    for arg in args:
        if arg == "-o":
            next(args)
            result.extend(["-o", "{out}"])
        elif arg.startswith(("--target=", "--sysroot=", "-Wl,-rpath,")) or arg == "-g":
            continue
        elif arg == "-Xlinker":
            value = next(args)
            if not value.startswith("--dependency-file="):
                result.extend([arg, value])
        elif arg.startswith("-Wl,-soname,"):
            result.append("-Wl,-soname,libwwhdgame.so")
        elif arg.startswith("-"):
            result.append(arg)
        else:
            source = (build / arg).resolve()
            if source.name == "libgamecode.a":
                gamecode += 1
                result.append("{gamecode}")
                continue
            if not source.is_file() or source.suffix not in (".o", ".a", ".so"):
                raise ValueError("Unexpected Android link input: " + arg)
            if source not in copied:
                relative = Path("obj" if source.suffix == ".o" else "lib") / (str(len(copied)) + "-" + source.name)
                # Keep a shared library's basename: its SONAME must resolve to
                # the Android-installed dependency at load time.
                if source.suffix == ".so":
                    relative = Path("lib") / source.name
                destination = sdk / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
                subprocess.run([str(strip), "--strip-debug", str(destination)], check=True)
                copied[source] = "{sdk}/" + relative.as_posix()
                objects += source.suffix == ".o"
            result.append(copied[source])
    if not objects or gamecode != 1 or "-shared" not in result or "{out}" not in result:
        raise ValueError("Incomplete Android runtime link recipe")
    result.append("-Wl,-z,max-page-size=16384")
    return result


def package(build, ndk, abi, output):
    build, ndk, output = build.resolve(), ndk.resolve(), output.resolve()
    if output.exists():
        raise ValueError("Output must be a new directory")
    if output.is_relative_to(build) or build.is_relative_to(output):
        raise ValueError("Runtime output must be separate from native build")
    properties = {key.strip(): value.strip() for key, value in
                  (line.split("=", 1) for line in (ndk / "source.properties").read_text().splitlines() if "=" in line)}
    if properties.get("Pkg.Revision", "").strip() != NDK_REVISION:
        raise ValueError("Unexpected NDK revision")
    tools = list(ndk.glob("toolchains/llvm/prebuilt/*/bin/llvm-strip"))
    if len(tools) != 1:
        raise ValueError("NDK llvm-strip not uniquely located")
    arguments = link_arguments(build)
    target = next((arg for arg in arguments if arg.startswith("--target=")), "")
    if not target.startswith("--target=" + TRIPLES[abi]) or not target.endswith("android33"):
        raise ValueError("Native build architecture/API differs from requested runtime")
    output.mkdir(parents=True)
    assets = output / "assets"
    assets.mkdir()
    with tempfile.TemporaryDirectory(dir=output) as temporary:
        root = Path(temporary)
        sdk = root / "sdk"
        sdk.mkdir()
        flags = [arg for arg in desktop.gamecode_flags(str(build)) if not arg.startswith("--target=") and arg != "-g"]
        if "-fPIC" not in flags:
            flags.append("-fPIC")
        manifest = {"schema": 1, "host_api": 1, "abi": abi, "api": 33, "ndk_revision": NDK_REVISION,
                    "gamecode_cflags": flags, "link": recipe(arguments, build, sdk, tools[0])}
        shutil.copytree(ROOT / "runtime/include", sdk / "include")
        # Hash runtime objects, libraries, headers and flags, not a git label.
        manifest["files"] = {str(path.relative_to(sdk)): digest(path) for path in sorted(sdk.rglob("*")) if path.is_file()}
        identity = hashlib.sha256(json.dumps(manifest, sort_keys=True).encode()).hexdigest()
        manifest["identity"] = identity
        (sdk / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        with zipfile.ZipFile(assets / "runtime-sdk.zip", "w", zipfile.ZIP_DEFLATED) as bundle:
            for path in sorted(sdk.rglob("*")):
                if path.is_file():
                    bundle.write(path, path.relative_to(root))
        metadata = {"schema": 1, "host_api": 1, "identity": identity, "abi": abi, "api": 33,
                    "sha256": digest(assets / "runtime-sdk.zip"),
                    "installed_bytes": sum(path.stat().st_size for path in sdk.rglob("*") if path.is_file()),
                    "archive_bytes": (assets / "runtime-sdk.zip").stat().st_size}
        (assets / "runtime-sdk.json").write_text(json.dumps(metadata, indent=2) + "\n")
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--ndk", required=True, type=Path)
    parser.add_argument("--abi", required=True, choices=TRIPLES)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(package(args.build, args.ndk, args.abi, args.out), indent=2))


if __name__ == "__main__":
    main()
