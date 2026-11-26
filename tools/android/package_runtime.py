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


def recipe(arguments, build, sdk, strip, sub=None, normal=None):
    """Preserve object/library order, removing only host/output-specific options.
    sub/normal: a decomp build's link line (--decomp-build): its runtime objects whose stripped bytes are a normal
    object's (normal: {sha256: placeholder path}) take that one, the others go to sdk/SUB/; hooks_none.cpp's object
    becomes {decomp_glue} (setup's hooks and game units, tools/installer/decomp_setup.py)."""
    result, copied = [], {}
    glue = 0
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
            if sub is not None and source.name == "hooks_none.cpp.o":
                glue += 1
                result.append("{decomp_glue}")
                continue
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
                if sub is not None and source.suffix != ".so":
                    relative = Path(sub) / relative
                destination = sdk / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
                subprocess.run([str(strip), "--strip-debug", str(destination)], check=True)
                copied[source] = "{sdk}/" + relative.as_posix()
                if sub is not None and source.suffix != ".so" and digest(destination) in normal:
                    copied[source] = normal[digest(destination)]
                    destination.unlink()
                elif sub is None and normal is not None and source.suffix != ".so":
                    normal[digest(destination)] = copied[source]
                objects += source.suffix == ".o"
            result.append(copied[source])
    if not objects or gamecode != 1 or "-shared" not in result or "{out}" not in result:
        raise ValueError("Incomplete Android runtime link recipe")
    if sub is not None and glue != 1:
        raise ValueError("hooks_none.cpp is not on the decomp build's link line: configure it with "
                         "-DWWHD_DECOMP_RUNTIME_ONLY=ON")
    result.append("-Wl,-z,max-page-size=16384")
    return result


def unit_flags(build, source, decomp_dir):
    """how setup compiles a game unit / hooks.cpp (the build's compile command of SOURCE, as the desktop release's
    package.py --decomp-build), without the host's target and sysroot: the app's compiler adds its own"""
    args, src = desktop.compile_entry(str(build), source)
    mapping = {str(ROOT / "runtime/include"): "{sdk}/include", str(ROOT / "runtime/src"): "{sdk}/decomp/include",
               str(ROOT / "tools/decomp_build"): "{tools}/decomp_build", str(build / "decomp"): "{out}",
               decomp_dir: "{decomp}"}
    flags = desktop.portable_flags(args, src, mapping)
    return [a for a in flags if not a.startswith(("--target=", "--sysroot=", "--gcc-toolchain=")) and a != "-g"]


def decomp_parts(build, sdk, strip, sub, fast, normal):
    """one runtime-only decomp build (android/build_native.sh DECOMP=1 DECOMP_RUNTIME_ONLY=1): its link line, unit and
    hooks flags; returns (decompilation folder, parts)"""
    build = build.resolve()
    cache = lambda key: desktop.cache_value(str(build), key)
    if not (desktop.on(cache("WWHD_DECOMP")) and desktop.on(cache("WWHD_DECOMP_RUNTIME_ONLY"))):
        raise ValueError("%s: configure it with -DWWHD_DECOMP=ON -DWWHD_DECOMP_RUNTIME_ONLY=ON" % build)
    if desktop.on(cache("WWHD_DECOMP_FAST")) != fast:
        raise ValueError("%s: %s" % (build, "--decomp-fast-build needs -DWWHD_DECOMP_FAST=ON" if fast
                                     else "--decomp-build is the plain decomp build (-DWWHD_DECOMP_FAST=OFF)"))
    ddir = cache("WWHD_DECOMP_DIR")
    link = recipe(link_arguments(build), build, sdk, strip, sub=sub, normal=normal)
    return ddir, {"link": link, "unit_flags": unit_flags(build, "tools/decomp_build/flags_probe.cpp", ddir),
                  "hooks_flags": unit_flags(build, "tools/decomp_build/hooks_none.cpp", ddir)}


def add_decomp(sdk, dbuild, fbuild, strip, normal):
    """The decomp version's parts (tools/android/decomp_android.py; the desktop's package.py add_decomp): sdk/decomp
    (plain) and sdk/decomp-fast objects that differ from the normal runtime's, the decomp headers, the source pin.
    No game code: the builds have no units and the stub game code is {gamecode}. Returns the manifest's "decomp"."""
    ddir, entry = decomp_parts(dbuild, sdk, strip, "decomp", False, normal)
    if fbuild:
        fdir, fast = decomp_parts(fbuild, sdk, strip, "decomp-fast", True, normal)
        if Path(fdir).resolve() != Path(ddir).resolve():
            raise ValueError("--decomp-fast-build: another decompilation than --decomp-build's")
        entry["fast"] = fast
    for h in sorted((ROOT / "runtime/src/decomp").glob("*.h")):
        destination = sdk / "decomp/include/decomp" / h.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(h, destination)
    pin, _ = desktop.decomp_source_pin(ddir)
    entry.update(pin)
    return entry


def package(build, ndk, abi, output, decomp_build=None, decomp_fast_build=None):
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
        normal = {}
        manifest = {"schema": 1, "host_api": 1, "abi": abi, "api": 33, "ndk_revision": NDK_REVISION,
                    "gamecode_cflags": flags, "link": recipe(arguments, build, sdk, tools[0], normal=normal)}
        if decomp_build:
            manifest["decomp"] = add_decomp(sdk, Path(decomp_build), Path(decomp_fast_build) if decomp_fast_build else None,
                                            tools[0], normal)
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
    parser.add_argument("--decomp-build", type=Path, help="the runtime-only plain decomp build (android/build_native.sh "
                        "DECOMP=1 DECOMP_RUNTIME_ONLY=1, its game folder): setup can then build the decomp version")
    parser.add_argument("--decomp-fast-build", type=Path, help="with --decomp-build: the same with DECOMP_FAST=1 (setup's "
                        "default mode)")
    args = parser.parse_args()
    if args.decomp_fast_build and not args.decomp_build:
        parser.error("--decomp-fast-build needs --decomp-build")
    print(json.dumps(package(args.build, args.ndk, args.abi, args.out, args.decomp_build, args.decomp_fast_build),
                     indent=2))


if __name__ == "__main__":
    main()
