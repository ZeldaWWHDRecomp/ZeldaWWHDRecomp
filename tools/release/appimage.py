#!/usr/bin/env python3
"""Pack an already-packaged Linux release folder as one AppImage (issue #55).

usage: appimage.py PKG_DIR --out OUT_DIR [--appimagetool PATH]

PKG_DIR is the WindWakerHD-VERSION-linux-ARCH folder tools/release/package.py produced (with
wind-waker-hd, tools/, sdk/, portable.txt, ...). An AppImage mount is read-only, so the result
drops portable.txt: setup then keeps the game, its code, saves and settings in the per-user
folders (~/.local/share/wwhd and ~/.config/wwhd, setup.py default_data_dir) instead of next to
the program, and nothing is ever written beside the .AppImage.

Adds what appimagetool wants at the AppDir root (AppRun, a .desktop, the project's own icon) and
squashes it with the AppImage project's appimagetool. Output: OUT_DIR/<basename of PKG_DIR>.AppImage.
"""
import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import urllib.request

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
# the project's own icon (not game data: guard.py sees it like any other PNG)
ICON = os.path.join(ROOT, "android", "app", "src", "main", "res", "mipmap-xxxhdpi", "ic_launcher.png")

# appimagetool (the AppImage project's packager), pinned. "continuous" moves; these are release
# assets. Change a pin only together with a new release.
APPIMAGETOOL_VERSION = "1.9.1"
APPIMAGETOOL = {
    "x86_64": ("appimagetool-x86_64.AppImage",
               "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"),
    "aarch64": ("appimagetool-aarch64.AppImage",
                "f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158"),
}
APPIMAGETOOL_URL = "https://github.com/AppImage/appimagetool/releases/download/%s/" % APPIMAGETOOL_VERSION

APPRUN = """#!/bin/sh
# Wind Waker HD (AppImage): start the release's launcher (next to tools/ and sdk/).
# This image is read-only: the setup keeps the game, its code, saves and settings in the
# per-user folders (issue #55), never beside this file.
exec "$(dirname "$0")/wind-waker-hd" "$@"
"""

DESKTOP = """[Desktop Entry]
Type=Application
Name=Wind Waker HD
Comment=The Wind Waker HD (native PC port); the first start prepares the game from your own disc dump
Exec=wind-waker-hd
Icon=wwhd
Categories=Game;
Terminal=false
Actions=setup;

[Desktop Action setup]
Name=Setup (repair, update, change the game)
Exec=wind-waker-hd --setup
"""


def host_arch():
    m = platform.machine().lower()
    return {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "aarch64", "aarch64": "aarch64"}.get(m)


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def ensure_appimagetool(given, arch):
    """The pinned appimagetool for `arch`, or the one given/installed. Returns its path."""
    if given:
        if not os.path.isfile(given):
            sys.exit("appimagetool not found: " + given)
        return given
    which = shutil.which("appimagetool")
    if which:
        return which
    if arch not in APPIMAGETOOL:
        sys.exit("no pinned appimagetool for %s (got --appimagetool to a build of it)" % arch)
    name, want = APPIMAGETOOL[arch]
    url = APPIMAGETOOL_URL + name
    # never in the output folder: that must hold only the artifacts
    dst = os.path.join(tempfile.gettempdir(), "wwhd-appimagetool-%s-%s" % (arch, APPIMAGETOOL_VERSION))
    if not (os.path.isfile(dst) and file_sha256(dst) == want):
        print("downloading appimagetool %s (%s)" % (APPIMAGETOOL_VERSION, arch))
        tmp = dst + ".part"
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "wwhd-package"})
            with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
                shutil.copyfileobj(r, f)
        except OSError as e:
            sys.exit("download of appimagetool failed: %s\nFetch %s yourself and pass --appimagetool" % (e, url))
        if file_sha256(tmp) != want:
            os.remove(tmp)
            sys.exit("the download of %s is corrupt or was changed (SHA-256 mismatch)" % url)
        os.replace(tmp, dst)
        os.chmod(dst, 0o755)
    return dst


def package_version(pkg):
    try:
        with open(os.path.join(pkg, "sdk", "manifest.json")) as f:
            return json.load(f).get("version") or ""
    except (OSError, ValueError):
        return ""


def make_appdir(pkg, appdir):
    """PKG_DIR as an AppDir: no portable.txt (read-only mount), plus AppRun, .desktop and the icon."""
    # data/ is an installation, portable.txt would send every write into the read-only mount
    # (setup.py PORTABLE, host::portable)
    # the zip's own "Wind Waker HD.desktop" (an `sh -c 'cd ...'` launcher for the unpacked folder) is
    # not a valid entry inside an AppImage, and appimagetool validates every .desktop at the root;
    # the AppImage gets its own wwhd.desktop below
    shutil.copytree(pkg, appdir, symlinks=True,
                    ignore=shutil.ignore_patterns("data", "portable.txt", ".appimagetool-*", "*.desktop"))
    with open(os.path.join(appdir, "AppRun"), "w") as f:
        f.write(APPRUN)
    os.chmod(os.path.join(appdir, "AppRun"), 0o755)
    with open(os.path.join(appdir, "wwhd.desktop"), "w") as f:
        f.write(DESKTOP)
    if not os.path.isfile(ICON):
        sys.exit("project icon not found: " + ICON)
    shutil.copyfile(ICON, os.path.join(appdir, "wwhd.png"))
    shutil.copyfile(ICON, os.path.join(appdir, ".DirIcon"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pkg", help="the WindWakerHD-VERSION-linux-ARCH folder package.py produced")
    ap.add_argument("--out", required=True, help="where to write the .AppImage")
    ap.add_argument("--appimagetool", help="appimagetool to use (default: one on PATH, else the pinned download)")
    a = ap.parse_args()

    pkg = os.path.normpath(os.path.abspath(a.pkg))
    if not os.path.isfile(os.path.join(pkg, "tools", "installer", "setup.py")):
        sys.exit("%s is not a packaged release (no tools/installer/setup.py)" % pkg)
    if not os.path.isfile(os.path.join(pkg, "wind-waker-hd")):
        sys.exit("%s has no wind-waker-hd (package it with --setup-gui)" % pkg)
    if not os.path.isfile(os.path.join(pkg, "portable.txt")):
        sys.exit("%s has no portable.txt: package.py always writes one (the zip is the portable "
                 "release; the AppImage drops it)" % pkg)
    arch = host_arch()
    if not arch:
        sys.exit("unsupported machine " + platform.machine())
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    name = os.path.basename(pkg)
    target = os.path.join(out, name + ".AppImage")
    tool = ensure_appimagetool(a.appimagetool, arch)

    work = tempfile.mkdtemp(prefix="wwhd-appimage-")
    try:
        appdir = os.path.join(work, name + ".AppDir")
        make_appdir(pkg, appdir)
        env = dict(os.environ)
        env.update({"ARCH": arch, "APPIMAGE_EXTRACT_AND_RUN": "1"})  # no FUSE needed to run appimagetool
        version = package_version(pkg)
        if version:
            env["VERSION"] = version
        subprocess.run([tool, appdir, target], env=env, check=True)
        if not os.path.isfile(target):
            sys.exit("appimagetool did not write " + target)
        os.chmod(target, 0o755)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("wrote", target, os.path.getsize(target) // (1 << 20), "MiB")


if __name__ == "__main__":
    main()
