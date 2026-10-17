#!/usr/bin/env python3
"""Package a release: the prebuilt runtime, the tools and the installer, without any game files.

usage: package.py --build BUILD_DIR --platform NAME --version VERSION --out OUT_DIR
                  [--license NAME=PATH ...] [--runtime-file PATH ...] [--linkonly-lib PATH ...]

BUILD_DIR is a Ninja build of this repository against placeholder guest code
(tools/recomp/stubgen.py): the release never contains recompiled game code. This script reads how
CMake compiled the placeholder code and linked the executable, and turns that into a recipe
(sdk/manifest.json) the installer replays on the player's machine with the game code generated
there:

  - gamecode: the compiler flags of build/gen/code_*.c (same flags, same compiler family);
  - link: the exact link line of the wwhd executable, with the runtime's object files and static
    libraries copied into sdk/ and libgamecode.a replaced by the player's own.

Output: OUT_DIR/WindWakerHD-VERSION-NAME/ and OUT_DIR/WindWakerHD-VERSION-NAME.zip.

The zip is the portable release (portable.txt). For the single-file Linux AppImage (issue #55) run
tools/release/appimage.py on the folder this produced: it drops portable.txt, so the read-only mount
sends every write to the per-user folders.
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import stat
import subprocess
import sys
import zipfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OBJ_EXT = (".o", ".obj")
LIB_EXT = (".a", ".lib", ".tbd", ".dylib", ".so")
# files shipped from the source tree (relative to ROOT -> relative to the package)
TOOL_FILES = [
    "tools/rpx.py",
    "tools/shaderprep.py",
    "tools/shaderprep_format.py",
    "tools/release/guard.py",  # shaderprep.py export/merge check templates with it (issue #162)
    "tools/wudextract.py",
    "tools/recomp/recomp.py",
    "tools/recomp/analyze.py",
    "tools/recomp/ppc2c.py",
    "tools/recomp/builds.py",
    "tools/recomp/crlive.py",
    "tools/recomp/leaflocal.py",
    "tools/guestmod/build_guest_mod.py",
    "tools/guestmod/guestmod.py",
    "tools/story_mods/npc_catalogue.py",  # the NPC catalogue from the player's game files (story_npc_catalogue.h)
    "tools/story_mods/notebook_model.py",  # shared player-local BFRES template patching
    "tools/story_mods/actor_model.py",  # original static GLB models using the player's own materials
    "tools/story_mods/skinned_actor_model.py",  # original rigged GLB and player-local skin material
    "tools/story_mods/actor_animation.py",  # original skeletal clips in the native format
    "tools/savegame/gc2hd.py",
    "tools/savegame/wwsave.py",
    "tools/savegame/README.md",
]
INSTALLER_FILES = [
    "tools/installer/setup.py",
    "tools/installer/code_mods.py",
    "tools/installer/decomp_setup.py",
    "tools/installer/shader_prebuild.py",
    "tools/installer/toolchains.json",
    "tools/installer/README.md",
]
SETUP_APP = "Wind Waker HD"
PORTABLE_TXT = """Wind Waker HD (native PC port), portable release.

Start "Wind Waker HD". The first start prepares the game once from your own disc dump (releases never
contain game code, so it is built here); later starts launch the game directly.

Everything the setup and the game create stays in this folder (in "data"): the built game, the game
files extracted from a disc image (an extracted game folder is used where it is), saves, settings,
save states, shader caches, logs and the downloaded compiler. Nothing goes to your user folders
unless you ask for a shortcut. To remove everything, delete this folder.

Hold Shift while starting Wind Waker HD (macOS, Windows), or start it with --setup, to repair,
update, change the game or import saves. The same setup in a terminal: tools/Setup in Terminal.command
(macOS), tools/Setup in a console window.bat (Windows), tools/setup-in-terminal.sh (Linux).

This file marks the folder as portable; without it, setup uses the per-user folders of earlier releases.
"""
MAC_SETUP_PLIST = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>Wind Waker HD</string>
  <key>CFBundleDisplayName</key><string>Wind Waker HD</string>
  <key>CFBundleIdentifier</key><string>io.github.zeldawwhdrecomp.wwhd</string>
  <key>CFBundleExecutable</key><string>wind-waker-hd</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>%s</string>
  <key>CFBundleVersion</key><string>%s</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
"""
LINUX_SETUP_DESKTOP = """[Desktop Entry]
Type=Application
Name=Wind Waker HD
Comment=The Wind Waker HD (native PC port); the first start prepares the game from your own disc dump
Exec=sh -c 'cd "$(dirname "%k")" && exec ./wind-waker-hd'
Terminal=false
Categories=Game;
Actions=setup;

[Desktop Action setup]
Name=Setup (repair, update, change the game)
Exec=sh -c 'cd "$(dirname "%k")" && exec ./wind-waker-hd --setup'
"""


def add_setup_gui(pkg, platform, exe, version):
    """The program a release starts: the first start prepares the game, later starts launch it."""
    if platform.startswith("macos"):
        app = os.path.join(pkg, SETUP_APP + ".app", "Contents")
        copy(exe, os.path.join(app, "MacOS", "wind-waker-hd"))
        v = re.match(r"[0-9.]*", version.lstrip("v")).group().strip(".") or "0"
        with open(os.path.join(app, "Info.plist"), "w") as f:
            f.write(MAC_SETUP_PLIST % (v, v))
        if shutil.which("codesign"):  # ad-hoc: seals the bundle (not a developer signature)
            subprocess.run(["codesign", "--force", "--deep", "-s", "-", os.path.join(pkg, SETUP_APP + ".app")], check=True)
    elif platform.startswith("windows"):
        copy(exe, os.path.join(pkg, SETUP_APP + ".exe"))
    else:
        copy(exe, os.path.join(pkg, "wind-waker-hd"))
        os.chmod(os.path.join(pkg, "wind-waker-hd"), 0o755)
        with open(os.path.join(pkg, SETUP_APP + ".desktop"), "w") as f:
            f.write(LINUX_SETUP_DESKTOP)
        os.chmod(os.path.join(pkg, SETUP_APP + ".desktop"), 0o755)
VENDORED_LICENSES = {
    "Cemu (MPL-2.0)": "runtime/third_party/cemu/LICENSE.txt",
    "Dear ImGui (MIT)": "runtime/third_party/imgui/LICENSE.txt",
    "fmt (MIT)": "runtime/third_party/fmt/LICENSE",
    "ImGuizmo (MIT)": "runtime/third_party/imguizmo/LICENSE",
    "metal-cpp (Apache-2.0)": "runtime/third_party/metal-cpp/LICENSE.txt",
    "xxHash (BSD-2-Clause)": "runtime/third_party/xxhash/LICENSE",
}


def split_command(cmd):
    """Tokenize a command line as Ninja runs it (POSIX shell or Windows CreateProcess rules)."""
    if os.name != "nt":
        return shlex.split(cmd)
    out, cur, quoted, have = [], [], False, False
    for ch in cmd:
        if ch == '"':
            quoted = not quoted
            have = True
        elif ch in " \t" and not quoted:
            if have:
                out.append("".join(cur))
            cur, have = [], False
        else:
            cur.append(ch)
            have = True
    if have:
        out.append("".join(cur))
    return out


def link_command(build):
    lines = subprocess.check_output(["ninja", "-C", build, "-t", "commands", "wwhd"], text=True).splitlines()
    cmd = lines[-1].strip()
    # CMake wraps link rules (": && CMD && :" on POSIX hosts, 'cmd.exe /C "cd . && CMD && ..."' on
    # Windows) and may chain post-build steps (copying SDL3.dll): keep the part that writes wwhd
    m = re.match(r'^(?:\S*[\\/])?cmd(?:\.exe)? /C "(.*)"$', cmd, re.I)
    if m:
        cmd = m.group(1)
    parts = [p.strip() for p in cmd.split(" && ")]
    links = [p for p in parts if re.search(r"(^|\s)-o\s+\"?wwhd(\.exe)?\"?(\s|$)", p)]
    if len(links) != 1:
        sys.exit("cannot find the link step in: " + cmd)
    cmd = links[0]
    args = []
    for a in split_command(cmd):
        if a.startswith("@") and os.path.isfile(os.path.join(build, a[1:])):  # response file (ninja -d keeprsp)
            with open(os.path.join(build, a[1:])) as f:
                args += split_command(f.read().replace("\n", " "))
        else:
            args.append(a)
    return args


def gamecode_flags(build):
    with open(os.path.join(build, "compile_commands.json")) as f:
        db = json.load(f)
    e = next(e for e in db if re.search(r"[\\/]code_000\.c$", e["file"]))
    args = e.get("arguments") or split_command(e["command"])
    out, skip = [], False
    gen_dir = os.path.dirname(e["file"])
    inc_runtime = os.path.normcase(os.path.normpath(os.path.join(ROOT, "runtime", "include")))
    for i, a in enumerate(args[1:], 1):
        if skip:
            skip = False
            continue
        if a in ("-o", "-c", "-MF", "-MT", "-MQ", "-isysroot"):
            skip = True
            continue
        if a in ("-MD", "-MMD") or a == e["file"]:
            continue
        if a.startswith("-I"):
            p = os.path.normcase(os.path.normpath(a[2:]))
            if p == inc_runtime:
                out.append("-I{sdk}/include")
            elif p == os.path.normcase(os.path.normpath(gen_dir)):
                out.append("-I{gen}")
            else:
                sys.exit("unexpected include directory in the gamecode compile command: " + a)
            continue
        if a.startswith("--sysroot") or a.startswith("-isysroot"):
            continue
        out.append(a)
    return out


def is_system_path(p):
    p = p.replace("\\", "/")
    return "/MacOSX.platform/" in p or "/CommandLineTools/SDKs/" in p or p.startswith("/usr/lib/") or p.startswith("/lib/")


def decomp_variant(build):
    """"decomp" for a WWHD_DECOMP=ON build directory, else None."""
    try:
        with open(os.path.join(build, "CMakeCache.txt"), encoding="utf-8", errors="replace") as f:
            on = any(re.match(r"WWHD_DECOMP:BOOL=(ON|1|TRUE|YES)$", l.strip(), re.I) for l in f)
    except OSError:
        return None
    if on and not os.path.isfile(os.path.join(build, "decomp", "decomp_hooks.txt")):
        sys.exit("decomp build without decomp/decomp_hooks.txt in %s" % build)
    return "decomp" if on else None


# The decomp version setup can build (development builds, macOS; tools/installer/decomp_setup.py,
# docs/decomp-build.md): the tools that prepare the decompilation's source. The source is not in the
# release: setup downloads the pinned commit and checks it against the SHA-256 written here.
DECOMP_TOOLS = ["tools/decomp_build/gen.py", "tools/decomp_build/glue.cpp", "tools/decomp_build/frames_vis.h"]
DECOMP_HOOKS_NONE = "tools_decomp_build_hooks_none.cpp.o"  # flattened like the other runtime objects


def cache_value(build, key):
    try:
        with open(os.path.join(build, "CMakeCache.txt"), encoding="utf-8", errors="replace") as f:
            for line in f:
                k, _, v = line.strip().partition("=")
                if k.split(":")[0] == key:
                    return v
    except OSError:
        pass
    return None


def compile_entry(build, suffix):
    with open(os.path.join(build, "compile_commands.json")) as f:
        db = json.load(f)
    e = next((e for e in db if e["file"].replace("\\", "/").endswith(suffix)), None)
    if e is None:
        sys.exit("%s: no compile command for %s" % (build, suffix))
    return e.get("arguments") or split_command(e["command"]), e["file"]


def portable_flags(args, src, mapping, keep_unknown_includes=False):
    """A compile command as a recipe: compiler, output, input and dependency options dropped, the paths of
    `mapping` (longest first) written as placeholders; other include directories dropped (keep_unknown_includes
    False) or an error."""
    norm = lambda p: os.path.normcase(os.path.normpath(p))
    keys = sorted(mapping, key=len, reverse=True)

    def place(p):
        n = norm(p)
        for k in keys:
            if n == norm(k) or n.startswith(norm(k) + os.sep):
                return mapping[k] + n[len(norm(k)):].replace(os.sep, "/")
        return None

    out, i = [], 1
    while i < len(args):
        a = args[i]
        i += 1
        if a in ("-o", "-MF", "-MT", "-MQ", "-isysroot"):
            i += 1
            continue
        if a in ("-c", "-MD", "-MMD") or norm(a) == norm(src):
            continue
        if a in ("-I", "-isystem", "-iquote", "-include"):
            opt, val = a, args[i]
            i += 1
        elif a.startswith("-isystem"):
            opt, val = "-isystem", a[len("-isystem"):]
        elif a.startswith("-I"):
            opt, val = "-I", a[2:]
        else:
            out.append(a)
            continue
        p = place(val)
        if p is None:
            if opt == "-include" or keep_unknown_includes:
                sys.exit("unexpected %s %s in the compile command of %s" % (opt, val, src))
            continue  # the runtime's third-party include directories: nothing a game unit or hooks.cpp uses
        out += ["-I" + p] if opt in ("-I", "-isystem") else [opt, p]
    return out


def add_decomp(pkg, dbuild, linkonly):
    """--decomp-build: the parts of the decomp version setup can build (tools/installer/decomp_setup.py).
    DBUILD is configured like --build, plus -DWWHD_DECOMP=ON -DWWHD_DECOMP_RUNTIME_ONLY=ON (no game needed).
    Returns the manifest's "decomp" entry."""
    on = lambda v: (v or "").upper() in ("ON", "1", "TRUE", "YES")
    if not (on(cache_value(dbuild, "WWHD_DECOMP")) and on(cache_value(dbuild, "WWHD_DECOMP_RUNTIME_ONLY"))):
        sys.exit("--decomp-build %s: configure it with -DWWHD_DECOMP=ON -DWWHD_DECOMP_RUNTIME_ONLY=ON" % dbuild)
    ddir = cache_value(dbuild, "WWHD_DECOMP_DIR")
    if not ddir or not os.path.isfile(os.path.join(ddir, "tools", "verify", "include", "gabi.h")):
        sys.exit("--decomp-build: no decompilation at %s (git submodule update --init decomp)" % ddir)
    # the link line: the runtime objects that are the same as the regular release's are taken from sdk/obj
    # (and its libraries from sdk/lib), the others go to sdk/decomp/obj; hooks_none.cpp's object becomes {decomp_glue} (setup's hooks and units)
    stage = pkg + ".decomp-stage"
    shutil.rmtree(stage, ignore_errors=True)
    _, link, _, _ = build_link_recipe(dbuild, stage, linkonly)
    recipe, own = [], 0
    for a in link:
        if a == "{sdk}/obj/" + DECOMP_HOOKS_NONE:
            recipe.append("{decomp_glue}")
            continue
        for kind in ("obj", "lib"):
            if a.startswith("{sdk}/%s/" % kind):
                name = a[len("{sdk}/%s/" % kind):]
                mine, main = os.path.join(stage, "sdk", kind, name), os.path.join(pkg, "sdk", kind, name)
                # static libraries are built from the same sources with the same options in both builds
                # (WWHD_DECOMP only defines things for the wwhd target) but carry their own archive times
                if os.path.isfile(main) and (kind == "lib" or file_sha256(main) == file_sha256(mine)):
                    break
                copy(mine, os.path.join(pkg, "sdk", "decomp", kind, name))
                a = "{sdk}/decomp/%s/%s" % (kind, name)
                own += 1
                break
        recipe.append(a)
    shutil.rmtree(stage)
    if "{decomp_glue}" not in recipe:
        sys.exit("--decomp-build: %s is not on the decomp build's link line" % DECOMP_HOOKS_NONE)
    mapping = {os.path.join(ROOT, "runtime", "include"): "{sdk}/include",
               os.path.join(ROOT, "runtime", "src"): "{sdk}/decomp/include",
               os.path.join(ROOT, "tools", "decomp_build"): "{tools}/decomp_build",
               os.path.join(dbuild, "decomp"): "{out}",
               ddir: "{decomp}"}
    unit_flags = portable_flags(*compile_entry(dbuild, "tools/decomp_build/flags_probe.cpp"), mapping)
    hooks_flags = portable_flags(*compile_entry(dbuild, "tools/decomp_build/hooks_none.cpp"), mapping)
    # the decompilation's source: its commit and the SHA-256 of the files setup downloads (decomp_setup.py
    # SOURCE_PATHS as git tracks them), from a temporary copy; not shipped
    sys.path.insert(0, os.path.join(ROOT, "tools", "installer"))
    import decomp_setup
    paths = decomp_setup.SOURCE_PATHS
    tracked = subprocess.run(["git", "-C", ddir, "ls-files", "-z", "--"] + paths, capture_output=True,
                             check=True).stdout.decode("utf-8").split("\0")
    tracked = [t for t in tracked if t]
    if not tracked or subprocess.run(["git", "-C", ddir, "diff", "--quiet", "HEAD", "--"] + paths).returncode:
        sys.exit("--decomp-build: %s is not a clean checkout of the decompilation" % ddir)
    tree = pkg + ".decomp-tree"
    shutil.rmtree(tree, ignore_errors=True)
    for rel in tracked:
        copy(os.path.join(ddir, rel), os.path.join(tree, rel))
    digest = decomp_setup.tree_digest(tree)
    shutil.rmtree(tree)
    rev = subprocess.run(["git", "-C", ddir, "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    # about the size of GitHub's archive of the commit (shown before setup downloads it)
    size = len(subprocess.run(["git", "-C", ddir, "archive", "--format=tar.gz", "HEAD"], capture_output=True,
                              check=True).stdout)
    for rel in DECOMP_TOOLS:
        copy(os.path.join(ROOT, rel), os.path.join(pkg, rel))
    copy(os.path.join(ROOT, "runtime", "src", "decomp", "decomp.h"), os.path.join(pkg, "sdk", "decomp", "include", "decomp", "decomp.h"))
    print("decomp version: decompilation %s (%d files, setup downloads about %d MB), %d own runtime objects"
          % (rev[:7], len(tracked), size // 1000000, own))
    return {"revision": rev, "tree_sha256": digest, "url": decomp_setup.ARCHIVE_URL % rev, "download_bytes": size,
            "link": recipe, "unit_flags": unit_flags, "hooks_flags": hooks_flags}


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def build_link_recipe(build, pkg, linkonly):
    args = link_command(build)
    recipe, objs, libs = [], 0, 0
    os.makedirs(os.path.join(pkg, "sdk", "obj"), exist_ok=True)
    os.makedirs(os.path.join(pkg, "sdk", "lib"), exist_ok=True)
    skip = False
    seen_names = {}
    linkonly = {os.path.realpath(p) for p in linkonly}
    it = iter(enumerate(args))
    for i, a in it:
        if i == 0:
            continue  # the compiler driver: the installer supplies its own
        if skip:
            skip = False
            continue
        if a == "-o":
            recipe += ["-o", "{out}"]
            skip = True
            continue
        if a.startswith("-Wl,--out-implib") or a.startswith("-Wl,-rpath,"):
            continue  # build-machine paths; the installer sets its own rpath ($ORIGIN on Linux)
        if a == "-isysroot":
            skip = True
            continue
        path = a if os.path.isabs(a) else os.path.join(build, a)
        if not a.startswith("-") and os.path.isfile(path):
            name = os.path.basename(a)
            if name in ("libgamecode.a", "gamecode.lib"):
                recipe.append("{gamecode}")
                continue
            # the game's Windows resources (VERSIONINFO + manifest) from our own generated .rc
            # (cmake/WindowsResources.cmake; windres writes a COFF object named .rc.res)
            own_res = re.fullmatch(r"CMakeFiles/wwhd\.dir/generated/wwhd\.rc\.res",
                                   os.path.relpath(path, build).replace("\\", "/"))
            if a.lower().endswith(OBJ_EXT) or own_res:
                # flatten CMakeFiles/wwhd.dir/runtime/src/x.cpp.o -> obj/runtime_src_x.cpp.o
                rel = os.path.relpath(path, build).replace("\\", "/")
                rel = re.sub(r"^CMakeFiles/[^/]+\.dir/", "", rel)
                flat = re.sub(r"[^A-Za-z0-9_.+-]", "_", rel)
                shutil.copy2(path, os.path.join(pkg, "sdk", "obj", flat))
                recipe.append("{sdk}/obj/" + flat)
                objs += 1
                continue
            if is_system_path(path) and os.path.realpath(path) not in linkonly:
                # an SDK / system library: link by name so the player's own SDK provides it
                base = re.sub(r"^lib", "", name)
                base = re.sub(r"\.(tbd|dylib|so)(\.\d+)*$", "", base)
                recipe.append("-l" + base)
                continue
            if re.search(r"\.(a|lib|tbd|dylib|so)(\.\d+)*$", name.lower()) or name.lower().endswith(".dll.a"):
                real = os.path.realpath(path)
                if seen_names.get(name, real) != real:
                    sys.exit("two different libraries named %s on the link line" % name)
                if name not in seen_names:
                    seen_names[name] = real
                    shutil.copy2(real, os.path.join(pkg, "sdk", "lib", name))
                    libs += 1
                recipe.append("{sdk}/lib/" + name)  # repeated libraries keep their place (link order)
                continue
            sys.exit("unexpected file on the link line: " + a)
        recipe.append(a)
    if objs == 0 or "{gamecode}" not in recipe:
        sys.exit("link line not understood (no objects or no libgamecode.a): " + " ".join(args))
    return args[0], recipe, objs, libs


def add_windows_python(pkg, zip_path, platform="windows-x86_64"):
    """The official embeddable Python, unpacked unmodified into tools/python (its exe and DLLs keep the PSF
    signature). Checked against the pin in toolchains.json and every file against python-windows-files.json
    (python-windows-arm64-files.json and the python.windows-arm64 pin for windows-arm64), which
    tools/release/guard.py uses too."""
    arm64 = platform == "windows-arm64"
    with open(os.path.join(ROOT, "tools", "installer", "toolchains.json")) as f:
        pin = json.load(f)["python"]["windows-arm64" if arm64 else "windows"]
    with open(os.path.join(ROOT, "tools", "release", "python-windows-arm64-files.json" if arm64 else "python-windows-files.json")) as f:
        expected = json.load(f)
    with open(zip_path, "rb") as f:
        data = f.read()
    if hashlib.sha256(data).hexdigest() != pin["sha256"] or expected["sha256"] != pin["sha256"]:
        sys.exit("%s is not the pinned embeddable Python for %s (SHA-256 mismatch with toolchains.json / "
                 "python-windows*-files.json)" % (zip_path, platform))
    dest = os.path.join(pkg, "tools", "python")
    names = set()
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            if info.is_dir():
                continue
            body = z.read(info)
            if expected["files"].get(info.filename) != hashlib.sha256(body).hexdigest():
                sys.exit("unexpected file in the embeddable Python: " + info.filename)
            out = os.path.join(dest, *info.filename.split("/"))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, "wb") as f:
                f.write(body)
            names.add(info.filename)
    if names != set(expected["files"]):
        sys.exit("the embeddable Python lacks: " + ", ".join(sorted(set(expected["files"]) - names)))


def copy(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)


def make_zip(src_dir, zip_path):
    base = os.path.basename(src_dir)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dirpath, dirnames, filenames in os.walk(src_dir):
            dirnames.sort()
            for fn in sorted(filenames):
                full = os.path.join(dirpath, fn)
                arc = os.path.join(base, os.path.relpath(full, src_dir)).replace("\\", "/")
                info = zipfile.ZipInfo.from_file(full, arc)
                mode = os.stat(full).st_mode
                info.external_attr = (stat.S_IFREG | (0o755 if mode & 0o111 or fn.endswith((".command", ".sh")) else 0o644)) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(full, "rb") as f:
                    z.writestr(info, f.read(), compresslevel=9)


def copy_shader_template(source, destination):
    """Optional private payload: forks remain installable without it."""
    relative = "tools/shadercache/template.bin"
    path = os.path.join(source, relative)
    if os.path.isfile(path):
        copy(path, os.path.join(destination, relative))
        return True
    print("Shader template unavailable; shader preparation will be skipped")
    return False


def copy_sdk_headers(pkg):
    """Shared by every platform: public declarations use wwhd/, never a game/ tree."""
    shutil.copytree(os.path.join(ROOT, "runtime", "include"), os.path.join(pkg, "sdk", "include"))
    shutil.copytree(os.path.join(ROOT, "runtime", "guest", "include"),
                    os.path.join(pkg, "sdk", "guest", "include"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True)
    ap.add_argument("--platform", required=True, help="macos-arm64, linux-x86_64, linux-aarch64, windows-x86_64 or windows-arm64")
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--toolchain", required=True, help="toolchain id from tools/installer/toolchains.json")
    ap.add_argument("--license", action="append", default=[], help="NAME=PATH of a dependency's license")
    ap.add_argument("--runtime-file", action="append", default=[], help="file to install next to the executable")
    ap.add_argument("--linkonly-lib", action="append", default=[], help="system library to ship for linking only")
    ap.add_argument("--setup-gui", help="the built graphical installer (wwhd-setup) to include")
    ap.add_argument("--windows-python", help="windows: the pinned embeddable Python zip (toolchains.json python.windows, or python.windows-arm64), "
                    "shipped unmodified as tools/python")
    ap.add_argument("--decomp-build", help="macOS development builds: a build configured with -DWWHD_DECOMP=ON "
                    "-DWWHD_DECOMP_RUNTIME_ONLY=ON; the release then lets setup build the decomp version (experimental)")
    ap.add_argument("--no-zip", action="store_true")
    a = ap.parse_args()

    build = os.path.abspath(a.build)
    # the decomp build (CMake WWHD_DECOMP=ON, docs/decomp-build.md): its own package, with the function list
    # setup translates the game code with (sdk/decomp_hooks.txt)
    variant = decomp_variant(build)
    name = "WindWakerHD-%s-%s%s" % (a.version, a.platform, "-" + variant if variant else "")
    pkg = os.path.join(os.path.abspath(a.out), name)
    if os.path.exists(pkg):
        shutil.rmtree(pkg)
    os.makedirs(pkg)
    exe_suffix = ".exe" if a.platform.startswith("windows") else ""

    driver, link, nobj, nlib = build_link_recipe(build, pkg, a.linkonly_lib)
    cflags = gamecode_flags(build)
    copy_sdk_headers(pkg)
    runtime_files = []
    for f in a.runtime_file:
        copy(f, os.path.join(pkg, "sdk", "runtime", os.path.basename(f)))
        runtime_files.append(os.path.basename(f))
    with open(os.path.join(ROOT, "tools", "installer", "toolchains.json")) as f:
        toolchains = json.load(f)
    if a.toolchain not in toolchains["toolchains"]:
        sys.exit("unknown toolchain " + a.toolchain)
    manifest = {
        "format": 1,
        "version": a.version,
        "platform": a.platform,
        "toolchain": a.toolchain,
        "exe": "wwhd" + exe_suffix,
        "built_with": os.path.basename(driver),
        "gamecode_cflags": cflags,
        "link": link,
        "runtime_files": runtime_files,
    }
    if a.decomp_build:
        if variant or not a.platform.startswith("macos"):
            sys.exit("--decomp-build: macOS regular releases only")
        manifest["decomp"] = add_decomp(pkg, os.path.abspath(a.decomp_build), a.linkonly_lib)
    if variant:
        manifest["variant"] = variant
        copy(os.path.join(build, "decomp", "decomp_hooks.txt"), os.path.join(pkg, "sdk", "decomp_hooks.txt"))
    with open(os.path.join(pkg, "sdk", "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)

    for rel in TOOL_FILES + INSTALLER_FILES:
        copy(os.path.join(ROOT, rel), os.path.join(pkg, rel))
    copy_shader_template(ROOT, pkg)
    for hp in sorted(os.listdir(os.path.join(ROOT, "tools", "recomp"))):
        if re.match(r"hooks.*\.txt$", hp):
            copy(os.path.join(ROOT, "tools", "recomp", hp), os.path.join(pkg, "tools", "recomp", hp))
    # the address maps of the builds the port can be made from (tools/recomp/builds.py, docs/builds.md)
    for bp in sorted(os.listdir(os.path.join(ROOT, "tools", "recomp", "builds"))):
        if bp.endswith(".json"):
            copy(os.path.join(ROOT, "tools", "recomp", "builds", bp),
                 os.path.join(pkg, "tools", "recomp", "builds", bp))
    # the extractor must be self-contained: zstd from the pinned source, linked statically (cmake/Zstd.cmake)
    try:
        with open(os.path.join(build, "wwhd-zstd.txt")) as f:
            kind, _, zstd_license = f.read().strip().partition(" ")
    except OSError:
        kind, zstd_license = "", ""
    if kind != "bundled":
        sys.exit("wwhd-extract in %s uses a system zstd; configure release builds with -DWWHD_BUNDLED_ZSTD=ON "
                 "(on by default with -DWWHD_BUNDLED_DEPS=ON)" % build)
    copy(os.path.join(build, "wwhd-extract" + exe_suffix), os.path.join(pkg, "tools", "bin", "wwhd-extract" + exe_suffix))
    # the native Yaz0 decoder for tools/shaderprep.py (CMake target wwhd_yaz0); without it, preparing
    # shaders falls back to a pure-Python decoder that takes minutes
    yaz0_name = "libyaz0" + (".dll" if a.platform.startswith("windows") else ".dylib" if a.platform.startswith("macos") else ".so")
    if not os.path.isfile(os.path.join(build, yaz0_name)):
        sys.exit("%s missing in %s (CMake target wwhd_yaz0)" % (yaz0_name, build))
    copy(os.path.join(build, yaz0_name), os.path.join(pkg, "tools", "lib", yaz0_name))

    # the setup in a terminal (the fallback for the program above), in tools/
    inst = os.path.join(ROOT, "tools", "installer")
    if a.platform.startswith("macos"):
        copy(os.path.join(inst, "install-macos.command"), os.path.join(pkg, "tools", "Setup in Terminal.command"))
    elif a.platform.startswith("windows"):
        # runs "Wind Waker HD.exe --console-setup", which runs setup.py with the bundled Python
        if not a.setup_gui:
            sys.exit("windows: --setup-gui is required (tools/Setup in a console window.bat runs Wind Waker HD.exe)")
        if not a.windows_python:
            sys.exit("windows: --windows-python is required (the setup runs with the bundled Python)")
        add_windows_python(pkg, a.windows_python, a.platform)
        copy(os.path.join(inst, "install-windows.bat"), os.path.join(pkg, "tools", "Setup in a console window.bat"))
    else:
        copy(os.path.join(inst, "install-linux.sh"), os.path.join(pkg, "tools", "setup-in-terminal.sh"))
    # portable release: everything stays in this folder (setup.py and the game look for this file)
    with open(os.path.join(pkg, "portable.txt"), "w") as f:
        f.write(PORTABLE_TXT)

    if a.setup_gui:
        add_setup_gui(pkg, a.platform, a.setup_gui, a.version)

    copy(os.path.join(ROOT, "README.md"), os.path.join(pkg, "README.md"))
    copy(os.path.join(ROOT, "CHANGELOG.md"), os.path.join(pkg, "CHANGELOG.md"))
    lic = os.path.join(ROOT, "LICENSE")
    if os.path.isfile(lic):
        copy(lic, os.path.join(pkg, "LICENSE"))
    elif os.environ.get("CI"):
        sys.exit("LICENSE missing")
    licdir = os.path.join(pkg, "third-party-licenses")
    os.makedirs(licdir)
    entries = dict(VENDORED_LICENSES)
    # zstd: compiled into tools/bin/wwhd-extract on every platform (pinned source, cmake/Zstd.cmake)
    entries["Zstandard (BSD-3-Clause)"] = zstd_license
    if a.platform.startswith("windows"):
        entries["Python (PSF-2.0)"] = os.path.join(pkg, "tools", "python", "LICENSE.txt")
    for spec in a.license:
        k, _, v = spec.partition("=")
        entries[k] = v
    index = ["Third-party software in this package and its license files:\n"]
    for k, v in sorted(entries.items()):
        src = v if os.path.isabs(v) else os.path.join(ROOT, v)
        if not os.path.isfile(src):
            sys.exit("license file for %s not found: %s" % (k, src))
        fn = re.sub(r"[^A-Za-z0-9]+", "-", k.split(" (")[0]).strip("-") + ".txt"
        copy(src, os.path.join(licdir, fn))
        index.append("  %-28s %s\n" % (k, fn))
    with open(os.path.join(licdir, "README.txt"), "w") as f:
        f.writelines(index)
    with open(os.path.join(pkg, "VERSION.txt"), "w") as f:
        f.write("Wind Waker HD native PC port %s (%s)\n"
                "This package contains no game code, game data or keys; the installer builds the game\n"
                "from your own disc dump on your machine.\n" % (a.version, a.platform))

    print("packaged %s: %d runtime objects, %d libraries, %d gamecode flags" % (name, nobj, nlib, len(cflags)))
    if not a.no_zip:
        zp = pkg + ".zip"
        make_zip(pkg, zp)
        print("wrote", zp, os.path.getsize(zp) // (1 << 20), "MiB")


if __name__ == "__main__":
    main()
