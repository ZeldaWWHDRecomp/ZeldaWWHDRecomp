#!/usr/bin/env python3
"""The Vulkan pieces the macOS release ships, from pinned upstream sources (release.yml, macOS job).

usage: macos_vulkan.py --work DIR [--jobs N]

  - Vulkan-Headers (Khronos, tag below): compiled into the game (the renderer includes them);
  - Vulkan-Loader (Khronos, built here from the tagged source for arm64, macOS 14): libvulkan.1.dylib,
    install name @rpath/libvulkan.1.dylib. The game links it as a weak import and the installer gives
    the game an @executable_path rpath, so the copy next to the game is the one it loads;
  - MoltenVK (Khronos' prebuilt release, the macOS tar): libMoltenVK.dylib (thinned to arm64, ad-hoc
    signed again) and its MoltenVK_icd.json, whose library_path is ./libMoltenVK.dylib (next to the
    manifest). The game points the loader at that manifest (gfx/vulkan/backend.cpp, init_appkit).

glslang is not shipped as a library: release builds compile it statically into the game
(-DWWHD_BUNDLED_DEPS=ON, cmake/WindowsDependencies.cmake).

Output in WORK: prefix/ (Vulkan headers and the loader, for -DVulkan_INCLUDE_DIR / -DVulkan_LIBRARY),
runtime/ (the three files to install next to the game: package.py --runtime-file) and licenses/.
Downloads are cached in WORK/dl and checked against the SHA-256 pins below.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request

PINS = {
    "headers": ("https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.363.tar.gz",
                "cbaf687d3c59b9666fe080e7f8396b6b4f4d344a768ee6b337c59852f4526b68"),
    "loader": ("https://github.com/KhronosGroup/Vulkan-Loader/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz",
               "941eff558fc74f248745fb7df367640cf89a8457f216dae498a74ae14b6ba6f7"),
    "moltenvk": ("https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.2/MoltenVK-macos.tar",
                 "f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e"),
}
DEPLOYMENT_TARGET = "14.0"
RUNTIME_FILES = ("libvulkan.1.dylib", "libMoltenVK.dylib", "MoltenVK_icd.json")


def run(cmd, **kw):
    print("+ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, **kw)


def fetch(work, key):
    url, sha = PINS[key]
    path = os.path.join(work, "dl", os.path.basename(url))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if not os.path.isfile(path):
        with urllib.request.urlopen(url) as r, open(path + ".part", "wb") as f:
            shutil.copyfileobj(r, f)
        os.replace(path + ".part", path)
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    if h.hexdigest() != sha:
        os.remove(path)
        sys.exit("%s: SHA-256 %s, expected %s" % (url, h.hexdigest(), sha))
    return path


def extract(archive, dest):
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(dest)
    with tarfile.open(archive) as t:
        t.extractall(dest, filter="data")
    entries = os.listdir(dest)
    return os.path.join(dest, entries[0]) if len(entries) == 1 else dest


def install_name(path):
    out = subprocess.check_output(["otool", "-D", path], text=True).splitlines()
    return out[-1].strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--work", required=True)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    prefix = os.path.join(work, "prefix")
    runtime = os.path.join(work, "runtime")
    licenses = os.path.join(work, "licenses")
    for d in (prefix, runtime, licenses):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
    src = os.path.join(work, "src")

    headers = extract(fetch(work, "headers"), os.path.join(src, "headers"))
    run(["cmake", "-S", headers, "-B", os.path.join(work, "build-headers"), "-DCMAKE_INSTALL_PREFIX=" + prefix])
    run(["cmake", "--install", os.path.join(work, "build-headers")])

    loader = extract(fetch(work, "loader"), os.path.join(src, "loader"))
    lb = os.path.join(work, "build-loader")
    shutil.rmtree(lb, ignore_errors=True)
    run(["cmake", "-S", loader, "-B", lb, "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_PREFIX=" + prefix,
         "-DCMAKE_PREFIX_PATH=" + prefix, "-DCMAKE_OSX_ARCHITECTURES=arm64",
         "-DCMAKE_OSX_DEPLOYMENT_TARGET=" + DEPLOYMENT_TARGET, "-DBUILD_TESTS=OFF", "-DUPDATE_DEPS=OFF",
         "-DCMAKE_INSTALL_NAME_DIR=@rpath"])
    run(["cmake", "--build", lb, "-j", str(a.jobs)])
    run(["cmake", "--install", lb, "--strip"])
    lib = os.path.join(prefix, "lib", "libvulkan.1.dylib")
    if install_name(lib) != "@rpath/libvulkan.1.dylib":
        sys.exit("unexpected install name of %s: %s" % (lib, install_name(lib)))
    shutil.copyfile(os.path.realpath(lib), os.path.join(runtime, "libvulkan.1.dylib"))

    mvk = extract(fetch(work, "moltenvk"), os.path.join(src, "moltenvk"))
    mdir = os.path.join(mvk, "MoltenVK", "dynamic", "dylib", "macOS")
    out = os.path.join(runtime, "libMoltenVK.dylib")
    run(["lipo", os.path.join(mdir, "libMoltenVK.dylib"), "-thin", "arm64", "-output", out])
    if install_name(out) != "@rpath/libMoltenVK.dylib":
        sys.exit("unexpected install name of libMoltenVK.dylib: " + install_name(out))
    with open(os.path.join(mdir, "MoltenVK_icd.json")) as f:
        icd = json.load(f)
    if icd.get("ICD", {}).get("library_path") != "./libMoltenVK.dylib":
        sys.exit("unexpected MoltenVK_icd.json: %r" % icd)
    shutil.copyfile(os.path.join(mdir, "MoltenVK_icd.json"), os.path.join(runtime, "MoltenVK_icd.json"))

    for name in ("libvulkan.1.dylib", "libMoltenVK.dylib"):
        p = os.path.join(runtime, name)
        os.chmod(p, 0o755)
        run(["codesign", "--force", "-s", "-", p])  # ad-hoc, like the rest of the release

    shutil.copyfile(os.path.join(headers, "LICENSE.md"), os.path.join(licenses, "Vulkan-Headers.txt"))
    shutil.copyfile(os.path.join(loader, "LICENSE.txt"), os.path.join(licenses, "Vulkan-Loader.txt"))
    shutil.copyfile(os.path.join(mvk, "LICENSE"), os.path.join(licenses, "MoltenVK.txt"))

    for name in RUNTIME_FILES:
        print("%-20s %8d KiB" % (name, os.path.getsize(os.path.join(runtime, name)) // 1024))
    print("Vulkan_INCLUDE_DIR=%s" % os.path.join(prefix, "include"))
    print("Vulkan_LIBRARY=%s" % lib)


if __name__ == "__main__":
    main()
