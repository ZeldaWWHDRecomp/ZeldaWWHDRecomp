#!/usr/bin/env python3
"""Release guard: fails if a release artifact contains anything derived from the game or any key.

usage: guard.py [--allow-android-test-fixtures] ARTIFACT.zip|DIR [...]

Release packages may contain only this project's runtime, tools and installer (plus third-party
licenses). This check rejects, by name and by content:
  - generated/recompiled game code (code_NNN.c, funcs.h with guest functions, table.c, imports.c,
    imports.json, report.txt from tools/recomp), and real-looking generated code inside any file;
  - game files: *.rpx, *.rpl, *.wux, *.wud, disc keys (*.key), tickets/TMDs (title.tik, title.tmd),
    saves (cking*.sav, *.sav), anything under a game/ code/ content/ meta/ tree;
  - shader caches and head starts (shaders.bin, headstart.bin, template.bin, testcache.bin, *.metallib);
  - in every text file: a 32-hex-digit string (the shape of a Wii U key; SHA-256 sums are 64 digits
    and git hashes 40, which do not match).
The Windows release's tools/python/ (the official embeddable Python) must hold exactly the files listed
in tools/release/python-windows-files.json, each with its listed SHA-256.
Android Python source archives must not contain debug fixture registration or smoke
entry points. Only debug CI artifacts may opt in with --allow-android-test-fixtures.
Exit status 1 lists every problem.
"""
import io
import hashlib
import json
import os
import re
import sys
import zipfile

BAD_NAME = [
    (re.compile(r"(^|/)code_\d+\.c$"), "generated game code"),
    (re.compile(r"(^|/)(table|imports)\.c$"), "generated game code tables"),
    (re.compile(r"(^|/)imports\.json$"), "generated import table"),
    (re.compile(r"(^|/)funcs\.h$"), "generated function list"),
    (re.compile(r"\.(rpx|rpl|wux|wud|iso|wua|rvz|gci)$", re.I), "game executable or disc image"),
    (re.compile(r"\.key$", re.I), "key file"),
    (re.compile(r"(^|/)title\.(tik|tmd|cert)$", re.I), "disc ticket/metadata"),
    (re.compile(r"\.sav$", re.I), "save file"),
    (re.compile(r"(^|/)(shaders|headstart|template|testcache|mycache)\.bin$", re.I), "shader cache"),
    (re.compile(r"\.metallib$", re.I), "compiled shader cache"),
    (re.compile(r"(^|/)(game|content|meta)/"), "game file tree"),
    (re.compile(r"(^|/)build/gen"), "recompiler output"),
    (re.compile(r"(^|/)(lib)?gamecode\.(a|lib)$"), "compiled game code"),
    (re.compile(r"(^|/)common\.key$", re.I), "Wii U common key"),
]
TEXT_EXT = {".py", ".txt", ".md", ".json", ".sh", ".command", ".bat", ".ps1", ".h", ".hpp", ".c", ".cpp", ".inl",
            ".cfg", ".ini", ".xml", ".plist", ".toml", ".yml", ".yaml", ".rsp", ""}
KEYLIKE = re.compile(rb"(?<![0-9A-Fa-f])[0-9A-Fa-f]{32}(?![0-9A-Fa-f])")
# a function body as tools/recomp/recomp.py emits it (stubgen placeholders call ppc_unimplemented)
GEN_CODE = re.compile(rb"void f_[0-9A-F]{8}\(Cpu\* __restrict c\) \{\n")


PYTHON_FILES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "python-windows-files.json")
PYTHON_FILES_ARM64 = os.path.join(os.path.dirname(os.path.abspath(__file__)), "python-windows-arm64-files.json")
PYTHON_DIR = re.compile(r"(?:^|/)tools/python/(.+)$")


def python_files(path):
    """The file list of the embeddable Python this release must ship: the ARM64 one in a windows-arm64
    release (its name: WindWakerHD-<version>-windows-arm64[.zip])."""
    return PYTHON_FILES_ARM64 if "windows-arm64" in os.path.basename(os.path.normpath(path)) else PYTHON_FILES


def check_python(name, data, seen, problems, files=PYTHON_FILES):
    """tools/python/...: only the pinned embeddable Python's own files, unmodified. Returns True when it was one."""
    m = PYTHON_DIR.search(name.replace("\\", "/"))
    if not m:
        return False
    with open(files) as f:
        expected = json.load(f)["files"]
    rel = m.group(1)
    if rel not in expected:
        problems.append("%s: not a file of the pinned embeddable Python" % name)
    elif hashlib.sha256(data).hexdigest() != expected[rel]:
        problems.append("%s: differs from the pinned embeddable Python" % name)
    seen.add(rel)
    return True


def check_entry(name, data, problems):
    n = name.replace("\\", "/")
    for rx, why in BAD_NAME:
        if rx.search(n):
            problems.append("%s: %s" % (name, why))
    ext = os.path.splitext(n)[1].lower()
    if ext in TEXT_EXT and b"\0" not in data[:8192]:
        for m in KEYLIKE.finditer(data):
            line = data.count(b"\n", 0, m.start()) + 1
            problems.append("%s:%d: 32-hex-digit string (key-like)" % (name, line))
    if GEN_CODE.search(data):
        problems.append("%s: contains recompiled game functions" % name)


ANDROID_TEST_FILES = {
    "tools/android/debug_fixture_build.py", "tools/android/service_fixture.py",
    "tools/android/embedding_smoke.py", "tools/recomp/android_fixture.py",
    "tools/android/extraction-fixture.bin", "tools/android/runtime_fixture.c",
}


def scan(path, allow_android_test_fixtures=False):
    problems, count, python = [], 0, set()
    files = python_files(path)

    def entry(name, data):
        check_python(name, data, python, problems, files)
        check_entry(name, data, problems)
        if name.replace("\\", "/").endswith("python-source.zip"):
            try:
                with zipfile.ZipFile(io.BytesIO(data)) as source:
                    forbidden = ANDROID_TEST_FILES.intersection(source.namelist())
                    if forbidden and not allow_android_test_fixtures:
                        problems.append("%s: debug-only Android resources: %s" % (name, ", ".join(sorted(forbidden))))
            except zipfile.BadZipFile:
                problems.append("%s: invalid Android Python source archive" % name)

    if os.path.isdir(path):
        for dp, _, fns in os.walk(path):
            for fn in fns:
                full = os.path.join(dp, fn)
                with open(full, "rb") as f:
                    entry(os.path.relpath(full, path), f.read())
                count += 1
    else:
        with zipfile.ZipFile(path) as z:
            for info in z.infolist():
                if info.is_dir():
                    continue
                entry(info.filename, z.read(info))
                count += 1
    if python:
        with open(files) as f:
            missing = sorted(set(json.load(f)["files"]) - python)
        if missing:
            problems.append("tools/python/ lacks files of the pinned embeddable Python: " + ", ".join(missing))
    return problems, count


def main():
    args = sys.argv[1:]
    allow_tests = "--allow-android-test-fixtures" in args
    if allow_tests: args.remove("--allow-android-test-fixtures")
    if not args:
        sys.exit(__doc__)
    bad = False
    for p in args:
        problems, count = scan(p, allow_android_test_fixtures=allow_tests)
        if problems:
            bad = True
            print("REJECTED %s (%d files):" % (p, count))
            for pr in problems:
                print("  " + pr)
        else:
            print("ok %s (%d files checked)" % (p, count))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
