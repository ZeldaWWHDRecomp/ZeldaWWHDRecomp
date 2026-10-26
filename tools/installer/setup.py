#!/usr/bin/env python3
"""Wind Waker HD (native PC port): first-run setup, repair and update.

The release contains no game files. This program builds the game on your machine from your own
disc dump:

  1. asks for the disc image (.wux/.wud) and its keys, a Cemu Wii U archive (.wua; no keys), or an
     already extracted game folder;
  2. extracts the game files (tools/bin/wwhd-extract);
  3. translates the game's PowerPC code to C (tools/recomp/recomp.py);
  4. compiles that code with a C compiler (Apple's Command Line Tools on macOS, a pinned llvm-mingw
     on Windows, a pinned zig toolchain on Linux; downloaded and checked automatically);
  5. links it with the prebuilt runtime (sdk/) into the game executable and starts it.

Run it again to play, to repair the installation, or to update after downloading a new release.
Saves are never changed without asking.

Non-interactive use (tests, scripts):
  setup.py --yes --image DISC.wux [--disc-key FILE] --common-key FILE [--data-dir DIR] [--no-launch]
  setup.py --yes --archive GAME.wua [...]
  setup.py --yes --game-dir EXTRACTED_GAME [...]
  setup.py --yes --gen-dir GENERATED_C --no-launch   (build check with placeholder code, no game)
  setup.py --yes --language-source EUR_OR_JPN.wux|.wua|FOLDER [--language-disc-key FILE] --common-key FILE
                                                     (experimental: text in German, Italian, Japanese... from
                                                     your own European or Japanese game; see below)
  setup.py --remove-language-source EU|JP

Language sources (experimental, docs/language-packs.md): the USA game plays with the text, fonts and
2D layouts of a European or Japanese copy of the game you also own. Only those language files are
taken from it (content/Common/Pack/permanent_2d_*.pack, about 12 MB each) into data/game-lang/<EU|JP>;
the game itself is still built from the USA game. Its languages then appear in the game's settings
(Language). Tested with the European game; untested with the Japanese game so far.
Keys can also come from the WIIU_COMMON_KEY environment variable / IMAGE.key next to the image.
Keys are never printed, logged or stored.
"""
import argparse
import getpass
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import struct
import tarfile
import threading
import time
import urllib.request
import zipfile
import zlib
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
# Windows embeddable Python runs in isolated mode: it does not add the script
# directory to sys.path. Resolve shipped sibling modules explicitly.
sys.path.insert(0, HERE)
import code_mods  # noqa: E402
import decomp_setup  # noqa: E402
PKG = os.path.normpath(os.path.join(HERE, "..", ".."))
# Portable release (portable.txt in the release folder): everything setup and the game create stays
# in <release folder>/data. Without the marker: the per-user locations of earlier releases.
PORTABLE = os.path.isfile(os.path.join(PKG, "portable.txt"))
# no __pycache__ anywhere (Apple's Python would put it under ~/Library/Caches)
sys.dont_write_bytecode = True
os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
IS_MAC = sys.platform == "darwin"
IS_WIN = sys.platform.startswith("win")
IS_LINUX = not IS_MAC and not IS_WIN
EXE_SUFFIX = ".exe" if IS_WIN else ""


# ---------------------------------------------------------------------------------------------
# long paths on Windows (issue #144)
#
# Windows limits ordinary paths to MAX_PATH (260 UTF-16 units with the terminating NUL), unless a path
# carries the extended-length prefix \\?\ (an absolute, normalised path; up to about 32767 units).
# Windows' "Extract All" puts the release folder inside a folder of the same name, and the longest
# files of the pinned llvm-mingw archive are 117-118 characters deep, so unpacking the compiler from
# a release in Downloads can exceed MAX_PATH. Setup's own file operations (unpacking, moving and
# deleting the compiler, game files, generated code, caches) therefore use longpath(); messages and
# the log keep showing the normal path (plain_path / plain_text).

LONGPATH_PREFIX = "\\\\?\\"
WINDOWS_MAX_PATH = 259  # characters of a full path without the extended-length prefix (MAX_PATH - NUL)
# The deepest path the game, wwhd-extract and the compiler create, read or start below the data folder.
# These programs use ordinary Windows paths (no \\?\; the game declares longPathAware, which only helps
# where Windows has long paths enabled), so the data folder + this must stay within MAX_PATH. The deepest
# ones are names nobody here chooses:
#   game\content\Common\AudioRes\stream\Kugutu_Taraba_Intro_CK.w.32.dspadpcm.bfstm         (78, a game file)
#   toolchain\llvm-mingw-20260922-ucrt-aarch64\bin\aarch64-w64-mingw32-clang++.exe          (78, the compiler)
# Every name the game and setup choose themselves stays within 64 (test_setup.LongPaths), e.g.
#   user\shader-prebuild\live\vk-<12 hex>.bin.recipes.t<8 hex>                              (63)
#   user\shader-prebuild\generation-<8>\manifest.json.t<8>                                  (64)
# The compiler reads its headers through LLVM, which widens long paths itself; setup unpacks, moves and
# deletes the compiler with \\?\ paths (its unused Python/lldb files are 128 deep, 139 while unpacking).
DEEPEST_DATA_PATH = 78
MAX_DATA_DIR_CHARS = WINDOWS_MAX_PATH - 1 - DEEPEST_DATA_PATH  # 180 (the 1: the separator after it)


# Extended-length paths need a Python whose own path functions join with backslashes: shutil, os.walk
# and zipfile append names with os.path.join, and Windows takes a \\?\ path literally ("/" is no
# separator there). The release's Python is such a one; MSYS2's Python (used by some CI jobs) joins with
# "/", so there setup keeps ordinary paths.
NATIVE_WINDOWS_PATHS = IS_WIN and os.path.join("a", "b") == "a\\b"


def windows_path(p):
    """p as a normalised Windows path with backslashes, whatever separator the Python flavour uses (MSYS2's
    Python writes "/"): no "." or ".." components, no doubled separators; a drive root keeps its backslash."""
    p = os.fspath(p).replace("/", "\\")
    if p.startswith("\\\\"):  # \\server\share\...
        parts = p[2:].split("\\")
        root, rest = "\\\\" + "\\".join(parts[:2]) + "\\", parts[2:]
    elif len(p) >= 2 and p[1] == ":":
        root, rest = p[:2] + "\\", p[2:].split("\\")
    else:
        root, rest = "", p.split("\\")
    out = []
    for part in rest:
        if part in ("", "."):
            continue
        if part == "..":
            if out:
                out.pop()
            continue
        out.append(part)
    return (root + "\\".join(out)) if (root or out) else "."


def longpath(p, windows=None):
    """p for Windows file APIs without the MAX_PATH limit: the absolute, normalised path with backslashes
    and the extended-length prefix (\\\\?\\C:\\..., \\\\?\\UNC\\server\\share\\... for a network share).
    Unchanged on other platforms and with a Python that joins paths with "/" (NATIVE_WINDOWS_PATHS).
    windows=True: the string form only (tests simulate Windows anywhere). Use it only for the file operation
    itself, never in messages."""
    if not (NATIVE_WINDOWS_PATHS if windows is None else windows):
        return p
    p = os.fspath(p)
    if p.startswith(LONGPATH_PREFIX) or p.startswith("\\\\.\\"):
        return p
    p = windows_path(os.path.abspath(p) if windows is None else p)
    if p.startswith("\\\\"):  # \\server\share\...
        return LONGPATH_PREFIX + "UNC\\" + p[2:]
    return LONGPATH_PREFIX + p


def plain_path(p):
    """The normal form of a path longpath() made (for messages and the log)."""
    p = os.fspath(p)
    if p.startswith(LONGPATH_PREFIX + "UNC\\"):
        return "\\\\" + p[len(LONGPATH_PREFIX) + 4:]
    if p.startswith(LONGPATH_PREFIX):
        return p[len(LONGPATH_PREFIX):]
    return p


def plain_text(text):
    """text (an error message) with every extended-length path shown in its normal form."""
    text = str(text)
    # as repr() writes it (an OSError's message quotes its file name with doubled backslashes), then as it is
    for bs in ("\\\\", "\\"):
        prefix = bs * 2 + "?" + bs
        text = text.replace(prefix + "UNC" + bs, bs * 2).replace(prefix, "")
    return text


def path_length_problem(data_dir, package=None, windows=None):
    """None, or the message that stops setup before it downloads anything when the data folder is too
    deep for Windows: the game, wwhd-extract and the compiler would fail on their deepest files
    (DEEPEST_DATA_PATH). package: the release folder; when the data folder is inside it (a portable
    release), that is the folder the player moves."""
    if not (IS_WIN if windows is None else windows):
        return None
    data_dir = windows_path(plain_path(data_dir))
    if len(data_dir) <= MAX_DATA_DIR_CHARS:
        return None
    if package and data_dir.rsplit("\\", 1)[0].lower() == windows_path(package).lower():
        package = windows_path(package)
        limit = MAX_DATA_DIR_CHARS - (len(data_dir) - len(package))
        return ("This folder's path is too long for Windows (%d characters, at most %d work). Move the Wind "
                "Waker HD folder to a shorter place such as C:\\Games\\WindWakerHD and start it again."
                % (len(package), limit))
    return ("The install folder's path is too long for Windows (%d characters, at most %d work). Choose a "
            "shorter place such as C:\\Games\\WindWakerHD and start it again." % (len(data_dir), MAX_DATA_DIR_CHARS))


APP_NAME = "Wind Waker HD"
TITLE_IDS = {
    "0005000010143500": "USA",
    "0005000010143600": "Europe",
    "0005000010143400": "Japan",
}
# The builds of the game the port can be made from (tools/recomp/builds.py): the USA build is the
# canonical one, every other is translated through its own address map (tools/recomp/builds/*.json).
sys.path.insert(0, os.path.join(PKG, "tools", "recomp"))
import builds as game_builds  # noqa: E402
SUPPORTED_BUILDS = {b.title_id: b for b in game_builds.all_builds()}
SUPPORTED_TITLE = game_builds.canonical_build().title_id
# The translated code (tools/recomp, the hooks in tools/recomp/hooks*.txt, the runtime) is made for
# the code of version 0 of the game: the disc and eShop release. An update (0005000E-...) brings
# other code, and its data files go with that code.
SUPPORTED_VERSION = 0
# Every build is identified by the SHA-256 of its code/cking.rpx (builds.py). A checksum only: it
# identifies the file the port is built from and contains nothing of it (64 hex digits;
# tools/release/guard.py flags only 32-digit, key-shaped strings). Every source is checked before
# the code is translated (check_game_version).
SUPPORTED_RPX_SHA256 = game_builds.canonical_build().sha256


def supported_titles_text():
    return " or ".join("%s-%s (%s)" % (b.title_id[:8].upper(), b.title_id[8:].upper(), b.name)
                       for b in SUPPORTED_BUILDS.values())

EXTRACT_ERRORS = {
    3: "disc_key_bad",
    4: "disc_key_wrong",
    5: "common_key_bad",
    6: "common_key_wrong",
    7: "image_bad",
    8: "image_damaged",
    9: "write_failed",
    10: "wrong_title",
}


class SetupError(Exception):
    pass


# ---------------------------------------------------------------------------------------------
# output and logging


class Log:
    def __init__(self):
        self.f = None

    def open(self, path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.f = open(path, "a", encoding="utf-8")
        self.write("==== setup %s, %s, Python %s" % (time.strftime("%Y-%m-%d %H:%M:%S"), sys.platform,
                                                    sys.version.split()[0]))

    def write(self, text):
        if self.f:
            self.f.write(plain_text(text).rstrip("\n") + "\n")
            self.f.flush()


LOG = Log()


class Protocol:
    """--gui-protocol: one JSON object per line on stdout (events), requests as JSON lines on stdin.
    Requests are never logged (a pasted common key travels in one)."""

    def __init__(self):
        self.out = sys.stdout
        self.lock = threading.Lock()

    def emit(self, obj):
        line = json.dumps(obj)
        with self.lock:
            self.out.write(line + "\n")
            self.out.flush()


GUI = None  # Protocol when running under the graphical installer


def say(text=""):
    text = plain_text(text)  # never a \\?\ path in what the player reads (longpath)
    if GUI:
        GUI.emit({"event": "log", "text": text})
    else:
        print(text, flush=True)
    LOG.write(text)


def step(n, total, text, sid=""):
    if GUI:
        GUI.emit({"event": "step", "n": n, "total": total, "title": text, "id": sid})
        LOG.write("[%d/%d] %s" % (n, total, text))
        return
    say("")
    say("[%d/%d] %s" % (n, total, text))


class Progress:
    """One updating line: '  label  [#####.....]  42%  detail'."""

    def __init__(self, label):
        self.label = label
        self.last = 0.0
        self.tty = sys.stdout.isatty()
        self.lastpct = -1

    def update(self, done, total, detail="", force=False):
        now = time.time()
        pct = int(100 * done / total) if total else 100
        if not force and now - self.last < 0.2 and pct == self.lastpct:
            return
        self.last = now
        if GUI:
            GUI.emit({"event": "progress", "label": self.label, "done": done, "total": total, "detail": detail})
        elif self.tty:
            bar = "#" * (pct // 4) + "." * (25 - pct // 4)
            line = "  %s [%s] %3d%% %s" % (self.label, bar, pct, detail)
            sys.stdout.write("\r" + line[:110].ljust(110))
            sys.stdout.flush()
        elif pct // 10 != self.lastpct // 10 or force:
            print("  %s %d%% %s" % (self.label, pct, detail), flush=True)
        self.lastpct = pct

    def done(self, detail=""):
        self.update(1, 1, detail, force=True)
        if self.tty and not GUI:
            sys.stdout.write("\n")
            sys.stdout.flush()
        LOG.write("  %s done %s" % (self.label, detail))


def human(n):
    for unit in ("bytes", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return ("%d %s" % (n, unit)) if unit == "bytes" else ("%.1f %s" % (n, unit))
        n /= 1024.0


def run_logged(cmd, cwd=None, env=None, what="command"):
    """Runs a command with its output going to the log; raises SetupError with the output tail."""
    LOG.write("$ " + " ".join(cmd))
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = p.stdout.decode("utf-8", "replace")
    LOG.write(out)
    if p.returncode != 0:
        tail = "\n".join(out.strip().splitlines()[-25:])
        raise SetupError("%s failed (exit code %d):\n%s" % (what, p.returncode, tail))
    return out


# ---------------------------------------------------------------------------------------------
# user interaction


class UI:
    def __init__(self, interactive):
        self.interactive = interactive

    def need(self, what):
        if not self.interactive:
            raise SetupError("missing %s (non-interactive mode: pass it on the command line)" % what)

    def ask(self, prompt, default=""):
        self.need(prompt)
        try:
            ans = input(prompt + (" [%s] " % default if default else " ")).strip()
        except EOFError:
            raise SetupError("input ended")
        return ans or default

    def yesno(self, prompt, default=True, noninteractive=None):
        if not self.interactive:
            return default if noninteractive is None else noninteractive
        d = "Y/n" if default else "y/N"
        while True:
            a = self.ask("%s (%s)" % (prompt, d)).lower()
            if not a:
                return default
            if a in ("y", "yes", "j", "ja", "o", "oui", "s", "si"):
                return True
            if a in ("n", "no", "nein", "non"):
                return False

    def choose(self, prompt, options, default=1):
        self.need(prompt)
        say(prompt)
        for i, o in enumerate(options, 1):
            say("  %d) %s" % (i, o))
        while True:
            a = self.ask("Choose 1-%d:" % len(options), str(default))
            if a.isdigit() and 1 <= int(a) <= len(options):
                return int(a) - 1

    def secret(self, prompt):
        self.need(prompt)
        try:
            return getpass.getpass(prompt + " ")
        except EOFError:
            raise SetupError("input ended")

    def pick_path(self, title, folder=False, filetypes=None):
        """A native file/folder dialog where available, else a typed (or drag-and-dropped) path."""
        self.need(title)
        p = native_dialog(title, folder, filetypes)
        if p:
            say("  " + p)
            return p
        while True:
            a = self.ask("%s\n  Type the path (or drag the %s into this window) and press Enter:" %
                         (title, "folder" if folder else "file"))
            p = clean_path(a)
            if p and os.path.exists(p):
                return p
            say("  Not found: %s" % (p or "(nothing entered)"))


def clean_path(s):
    s = s.strip()
    if len(s) >= 2 and s[0] == s[-1] and s[0] in "'\"":
        s = s[1:-1]
    elif not IS_WIN:
        s = re.sub(r"\\(.)", r"\1", s)  # drag and drop in macOS/Linux terminals escapes spaces
    return os.path.abspath(os.path.expanduser(s)) if s else ""


def native_dialog(title, folder, filetypes):
    if os.environ.get("WWHD_SETUP_NO_DIALOGS"):
        return None
    try:
        if IS_MAC:
            what = "choose folder" if folder else "choose file"
            script = 'POSIX path of (%s with prompt "%s")' % (what, title.replace('"', "'"))
            p = subprocess.run(["osascript", "-e", script], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            out = p.stdout.decode("utf-8").strip()
            return out.rstrip("/") if p.returncode == 0 and out else None
        if IS_WIN:
            return win_folder_dialog(title) if folder else win_file_dialog(title, filetypes)
        if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
            return None
        if shutil.which("zenity"):
            cmd = ["zenity", "--file-selection", "--title=" + title] + (["--directory"] if folder else [])
        elif shutil.which("kdialog"):
            cmd = ["kdialog", "--title", title] + (["--getexistingdirectory", os.path.expanduser("~")] if folder
                                                    else ["--getopenfilename", os.path.expanduser("~")])
        else:
            return None
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        out = p.stdout.decode("utf-8", "replace").strip()
        return out if p.returncode == 0 and out else None
    except OSError:
        return None


# ---------------------------------------------------------------------------------------------
# Windows: dialogs and shortcuts through the Windows API (ctypes)


def _win_com():
    import ctypes
    ctypes.windll.ole32.CoInitializeEx(None, 2)  # COINIT_APARTMENTTHREADED (dialogs and shell objects need STA)
    return ctypes


def win_file_dialog(title, filetypes):
    """The standard Open dialog (GetOpenFileNameW). Returns the chosen file or None."""
    ctypes = _win_com()
    from ctypes import wintypes

    class OPENFILENAMEW(ctypes.Structure):
        _fields_ = [("lStructSize", wintypes.DWORD), ("hwndOwner", wintypes.HWND), ("hInstance", wintypes.HINSTANCE),
                    ("lpstrFilter", wintypes.LPCWSTR), ("lpstrCustomFilter", wintypes.LPWSTR),
                    ("nMaxCustFilter", wintypes.DWORD), ("nFilterIndex", wintypes.DWORD), ("lpstrFile", wintypes.LPWSTR),
                    ("nMaxFile", wintypes.DWORD), ("lpstrFileTitle", wintypes.LPWSTR), ("nMaxFileTitle", wintypes.DWORD),
                    ("lpstrInitialDir", wintypes.LPCWSTR), ("lpstrTitle", wintypes.LPCWSTR), ("Flags", wintypes.DWORD),
                    ("nFileOffset", wintypes.WORD), ("nFileExtension", wintypes.WORD), ("lpstrDefExt", wintypes.LPCWSTR),
                    ("lCustData", wintypes.LPARAM), ("lpfnHook", ctypes.c_void_p), ("lpTemplateName", wintypes.LPCWSTR),
                    ("pvReserved", ctypes.c_void_p), ("dwReserved", wintypes.DWORD), ("FlagsEx", wintypes.DWORD)]

    # "name\0pattern\0...\0\0", the same filters the dialog had before ("All files" last)
    flt = ctypes.create_unicode_buffer("".join("%s\0%s\0" % (n, p) for n, p in (filetypes or [])) +
                                       "All files (*.*)\0*.*\0\0")
    buf = ctypes.create_unicode_buffer(32768)
    ofn = OPENFILENAMEW()
    ofn.lStructSize = ctypes.sizeof(OPENFILENAMEW)
    ofn.lpstrFilter = ctypes.cast(flt, wintypes.LPCWSTR)
    ofn.nFilterIndex = 1
    ofn.lpstrFile = ctypes.cast(buf, wintypes.LPWSTR)
    ofn.nMaxFile = len(buf)
    ofn.lpstrTitle = title
    ofn.Flags = 0x00080000 | 0x00001000 | 0x00000800 | 0x00000008  # EXPLORER | FILEMUSTEXIST | PATHMUSTEXIST | NOCHANGEDIR
    get = ctypes.windll.comdlg32.GetOpenFileNameW
    get.argtypes = [ctypes.POINTER(OPENFILENAMEW)]
    get.restype = wintypes.BOOL
    return buf.value if get(ctypes.byref(ofn)) and buf.value else None


def win_folder_dialog(title):
    """The standard folder picker (SHBrowseForFolderW). Returns the chosen folder or None."""
    ctypes = _win_com()
    from ctypes import wintypes

    class BROWSEINFOW(ctypes.Structure):
        _fields_ = [("hwndOwner", wintypes.HWND), ("pidlRoot", ctypes.c_void_p), ("pszDisplayName", wintypes.LPWSTR),
                    ("lpszTitle", wintypes.LPCWSTR), ("ulFlags", wintypes.UINT), ("lpfn", ctypes.c_void_p),
                    ("lParam", wintypes.LPARAM), ("iImage", ctypes.c_int)]

    shell32 = ctypes.windll.shell32
    shell32.SHBrowseForFolderW.argtypes = [ctypes.POINTER(BROWSEINFOW)]
    shell32.SHBrowseForFolderW.restype = ctypes.c_void_p
    shell32.SHGetPathFromIDListW.argtypes = [ctypes.c_void_p, wintypes.LPWSTR]
    shell32.SHGetPathFromIDListW.restype = wintypes.BOOL
    ctypes.windll.ole32.CoTaskMemFree.argtypes = [ctypes.c_void_p]
    name = ctypes.create_unicode_buffer(260)
    bi = BROWSEINFOW()
    bi.pszDisplayName = ctypes.cast(name, wintypes.LPWSTR)
    bi.lpszTitle = title
    bi.ulFlags = 0x0001 | 0x0040  # BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE (resizable, "Make New Folder")
    pidl = shell32.SHBrowseForFolderW(ctypes.byref(bi))
    if not pidl:
        return None
    path = ctypes.create_unicode_buffer(32768)
    ok = shell32.SHGetPathFromIDListW(pidl, path)
    ctypes.windll.ole32.CoTaskMemFree(pidl)
    return path.value if ok and path.value else None


def win_known_folder(csidl):
    """A shell folder (SHGetFolderPathW): 0x02 the Start menu's Programs, 0x10 the Desktop."""
    import ctypes
    buf = ctypes.create_unicode_buffer(32768)
    if ctypes.windll.shell32.SHGetFolderPathW(None, csidl, None, 0, buf) != 0 or not buf.value:
        raise OSError("SHGetFolderPathW(0x%x) failed" % csidl)
    return buf.value


def win_shortcut(link, target, arguments="", workdir="", icon=""):
    """Writes a Windows shortcut (.lnk) with the shell's ShellLink object (IShellLinkW + IPersistFile)."""
    ctypes = _win_com()
    import uuid
    from ctypes import wintypes

    def guid(s):
        return (ctypes.c_ubyte * 16).from_buffer_copy(uuid.UUID(s).bytes_le)

    def method(obj, index, *argtypes):  # a COM method by its vtable slot; HRESULT failures raise OSError
        vtbl = ctypes.cast(ctypes.cast(obj, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
        return lambda *a: ctypes.WINFUNCTYPE(ctypes.HRESULT, ctypes.c_void_p, *argtypes)(vtbl[index])(obj, *a)

    def release(obj):
        vtbl = ctypes.cast(ctypes.cast(obj, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
        ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(vtbl[2])(obj)

    clsid_shell_link = guid("00021401-0000-0000-c000-000000000046")
    iid_shell_link_w = guid("000214f9-0000-0000-c000-000000000046")
    iid_persist_file = guid("0000010b-0000-0000-c000-000000000046")
    create = ctypes.windll.ole32.CoCreateInstance
    create.restype = ctypes.HRESULT
    sl, pf = ctypes.c_void_p(), ctypes.c_void_p()
    create(ctypes.byref(clsid_shell_link), None, 1, ctypes.byref(iid_shell_link_w), ctypes.byref(sl))  # INPROC_SERVER
    try:
        method(sl, 20, wintypes.LPCWSTR)(target)                     # IShellLinkW::SetPath
        method(sl, 11, wintypes.LPCWSTR)(arguments)                  # SetArguments
        method(sl, 9, wintypes.LPCWSTR)(workdir)                     # SetWorkingDirectory
        if icon:
            method(sl, 17, wintypes.LPCWSTR, ctypes.c_int)(icon, 0)  # SetIconLocation
        method(sl, 0, ctypes.c_void_p, ctypes.c_void_p)(ctypes.byref(iid_persist_file), ctypes.byref(pf))  # QueryInterface
        try:
            method(pf, 6, wintypes.LPCWSTR, wintypes.BOOL)(link, True)  # IPersistFile::Save
        finally:
            release(pf)
    finally:
        release(sl)
    return link


# ---------------------------------------------------------------------------------------------
# keys (kept in memory only; handed to wwhd-extract over stdin)


def parse_key(data):
    """16 raw bytes, or 32 hex digits (whitespace, an optional 0x and dashes ignored). None if malformed."""
    if isinstance(data, bytes) and len(data) == 16:
        return data
    if isinstance(data, bytes):
        try:
            data = data.decode("ascii")
        except UnicodeDecodeError:
            return None
    h = re.sub(r"[\s-]", "", data)
    if h[:2].lower() == "0x":
        h = h[2:]
    if not re.fullmatch(r"[0-9a-fA-F]{32}", h):
        return None
    return bytes.fromhex(h)


KEY_FILE_LIMIT = 1024 * 1024  # bounded, but large enough for a Cemu library


def parse_key_entries(data):
    """Keys in file order (entry numbers count valid keys, not comments/junk)."""
    single = parse_key(data)
    if single:
        return [single]
    if isinstance(data, bytes):
        data = data.decode("utf-8-sig", "replace")
    entries = []
    for line in data.splitlines():
        match = re.match(r"^\s*([0-9a-fA-F]{32})(?=$|\s|#|;|//)", line)
        if match:
            entries.append(bytes.fromhex(match[1]))
    return entries


def read_key_entries(path):
    try:
        with open(path, "rb") as f:
            data = f.read(KEY_FILE_LIMIT + 1)
        return parse_key_entries(data) if len(data) <= KEY_FILE_LIMIT else []
    except OSError:
        return []


def read_key_file(path):
    entries = read_key_entries(path)
    return entries[0] if len(entries) == 1 else None


class Keys:
    def __init__(self):
        self.disc = None
        self.common = None

    def stdin_blob(self):
        return ("disc %s\ncommon %s\n" % (self.disc.hex(), self.common.hex())).encode()


def ask_key(ui, what, hint):
    """Asks for a key file or a pasted key; returns 16 bytes."""
    while True:
        i = ui.choose("The %s is needed (%s). How do you want to provide it?" % (what, hint),
                      ["choose a key file", "paste it (32 hex digits; the input is hidden)"])
        if i == 0:
            p = ui.pick_path("Choose the %s file" % what)
            k = read_key_file(p)
            if k:
                return k
            say("  That file does not contain a key: it must hold 16 raw bytes or one line of 32 hex digits.")
        else:
            k = parse_key(ui.secret("Paste the %s and press Enter:" % what))
            if k:
                return k
            say("  That is not a key: it must be 32 hex digits (0-9, a-f).")


# ---------------------------------------------------------------------------------------------
# locations


def default_data_dir():
    if PORTABLE:
        return os.path.join(PKG, "data")
    return legacy_data_dir()


def legacy_data_dir():
    """Where releases before 0.2 installed (and where a non-portable setup still does)."""
    if IS_MAC:
        return os.path.expanduser("~/Library/Application Support/wwhd")
    if IS_WIN:
        return os.path.join(os.environ.get("LOCALAPPDATA") or os.path.expanduser("~\\AppData\\Local"), "WWHD")
    base = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(base, "wwhd")


def free_space(path):
    while not os.path.exists(path):
        path = os.path.dirname(path)
    return shutil.disk_usage(path).free


# ---------------------------------------------------------------------------------------------
# toolchains


class Toolchain:
    def __init__(self, cc, cxx, ar, env=None, rsp=False, desc=""):
        self.cc, self.cxx, self.ar, self.env, self.rsp, self.desc = cc, cxx, ar, env, rsp, desc


def load_toolchains():
    with open(os.path.join(HERE, "toolchains.json")) as f:
        return json.load(f)


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def local_copy(name, sha256):
    """A copy of a download the player saved themselves (issue #113: when setup cannot download it):
    the file name from the URL, in the release folder or the setup folder, with the expected SHA-256."""
    for folder in (PKG, HERE):
        path = os.path.join(folder, name)
        if os.path.isfile(path):
            if file_sha256(path) == sha256:
                return path
            say("  Ignoring %s: it is not the expected file (SHA-256 differs)." % path)
    return None


def download_error(url, e):
    text = str(e)
    if "CERTIFICATE_VERIFY_FAILED" in text or "certificate verify failed" in text:
        why = ("the secure connection's certificate could not be checked. Either Windows lacks a root certificate "
               "that it normally fetches on demand, or an antivirus or security program or a proxy intercepts "
               "HTTPS connections")
    else:
        why = "%s." % text.rstrip(".")
    name = os.path.basename(url.split("?")[0])
    return SetupError("download failed: %s\nRun setup again (it resumes where it stopped). If it keeps failing, "
                      "download %s in your browser, put %s in the Wind Waker HD folder and run setup again."
                      % (why, url, name))


DOWNLOAD_ATTEMPTS = 6


def windows_curl():
    """Windows' own curl.exe (Windows 10 1803 and later). It verifies certificates through Windows,
    which fetches missing root certificates on demand; Python's ssl module only reads the store as it
    is, so a fresh Windows can fail there with "unable to get local issuer certificate" (issue #113)."""
    if os.name != "nt":
        return None
    path = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "curl.exe")
    return path if os.path.isfile(path) else None


def curl_download(curl, url, tmp):
    """Downloads url to tmp with curl.exe (resuming tmp); True when curl reports success."""
    say("  Python could not verify the certificate; downloading with Windows' curl instead.")
    cmd = [curl, "--location", "--fail", "--silent", "--show-error", "--retry", "5", "--continue-at", "-",
           "--output", tmp, url]
    return subprocess.run(cmd).returncode == 0


def download(url, dst, sha256, size_hint, label, attempts=DOWNLOAD_ATTEMPTS, wait=2.0):
    """Downloads url to dst with a progress bar and checks the SHA-256 before keeping it. Interrupted
    downloads are retried and resume where they stopped (HTTP range requests); a copy the player
    downloaded themselves is used instead when present (local_copy). sha256 None: the caller checks the
    content itself (the decomp source: its unpacked tree, decomp_setup.py)."""
    name = os.path.basename(url.split("?")[0])
    own = local_copy(name, sha256) if sha256 else None
    if own:
        say("  Using %s" % own)
        shutil.copyfile(own, dst + ".part")
        os.replace(dst + ".part", dst)
        return
    tmp = dst + ".part"
    say("  Downloading %s" % url)
    pr = Progress(label)
    last = None
    for attempt in range(attempts):
        have = os.path.getsize(tmp) if os.path.isfile(tmp) else 0
        headers = {"User-Agent": "wwhd-setup"}
        if have:
            headers["Range"] = "bytes=%d-" % have
        req = urllib.request.Request(url, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                resumed = have and getattr(r, "status", 200) == 206
                mode = "ab" if resumed else "wb"
                done = have if resumed else 0
                length = int(r.headers.get("Content-Length") or 0)
                total = (done + length) if length else int(size_hint or 0)
                with open(tmp, mode) as f:
                    while True:
                        chunk = r.read(1 << 20)
                        if not chunk:
                            break
                        f.write(chunk)
                        done += len(chunk)
                        pr.update(done, total or done, human(done))
                if total and done < total:
                    raise OSError("the connection closed after %s of %s" % (human(done), human(total)))
            break
        except OSError as e:  # urllib's errors, timeouts and resets are all OSErrors
            last = e
            if getattr(e, "code", None) == 416 and os.path.isfile(tmp):  # a stale or oversized partial file
                os.remove(tmp)
            if "CERTIFICATE_VERIFY_FAILED" in str(e):
                curl = windows_curl()
                if curl and curl_download(curl, url, tmp):
                    break
                raise download_error(url, e)
            if attempt == attempts - 1:
                raise download_error(url, e)
            say("  Download interrupted (%s); retrying..." % e)
            time.sleep(wait * (2 ** attempt))
    pr.done(human(os.path.getsize(tmp)))
    if sha256 and file_sha256(tmp) != sha256:
        os.remove(tmp)
        raise SetupError("the download of %s is corrupt or was changed (SHA-256 mismatch); nothing was installed. "
                         "Run setup again; if this repeats, report it." % url)
    os.replace(tmp, dst)


def python_setup_capable(executable):
    """Probe the interpreter itself, including optional compiled stdlib codec support."""
    try:
        result = subprocess.run([executable, "-I", "-c",
                                 "import sys; from compression import zstd; "
                                 "assert sys.version_info >= (3, 14); "
                                 "assert zstd.decompress(zstd.compress(b'wwhd')) == b'wwhd'"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
        return result.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def ensure_setup_python(data_dir, prefer_system=True):
    """Select/provision trusted host Python for guest builds and local setup services.

    The installer owns downloads; guest setup scripts receive no additional capabilities.
    Windows always uses the release's audited embeddable distribution (issue #58).
    """
    if IS_WIN:
        executable = os.path.join(PKG, "tools", "python", "python.exe")
        if not python_setup_capable(executable):
            raise SetupError("The bundled Python lacks Python 3.14 compression.zstd support. "
                             "Download the current complete Windows release; no Python will be downloaded by setup.")
        return executable
    if prefer_system and python_setup_capable(sys.executable):
        return sys.executable
    arch = host_arch()
    if IS_MAC:
        key = {"aarch64": "macos", "x86_64": "macos-x86_64"}.get(arch)
    elif IS_LINUX:
        key = {"aarch64": "linux-aarch64", "x86_64": "linux"}.get(arch)
    else:
        key = None
    if key is None:
        raise SetupError("No private setup Python is available for this platform/architecture")
    pin = load_toolchains()["python"][key]
    root = os.path.join(data_dir, "setup-python")
    executable = os.path.join(root, "python", "bin", "python3")
    marker = os.path.join(root, ".wwhd-python")
    try:
        with open(marker) as f:
            current = f.read().strip() == pin["sha256"]
    except OSError:
        current = False
    if current and python_setup_capable(executable):
        return executable
    os.makedirs(root, exist_ok=True)
    archive = os.path.join(root, "download.tar.gz")
    stage = os.path.join(root, "unpack.tmp")
    say("  Installing private Python 3.14 for local mod setup (system Python is unchanged).")
    download(pin["url"], archive, pin["sha256"], pin.get("size"), "setup Python")
    shutil.rmtree(longpath(stage), ignore_errors=True)
    os.makedirs(stage)
    try:
        # Only the SHA-verified, maintainer-pinned standalone archive reaches extraction.
        if shutil.which("tar"):
            run_logged(["tar", "-xf", archive, "-C", stage], what="unpacking setup Python")
        else:
            raise SetupError("tar is required to unpack the pinned setup Python")
        staged = os.path.join(stage, "python", "bin", "python3")
        if not python_setup_capable(staged):
            raise SetupError("The pinned Python cannot run compression.zstd on this computer")
        shutil.rmtree(longpath(os.path.join(root, "python")), ignore_errors=True)
        os.replace(os.path.join(stage, "python"), os.path.join(root, "python"))
        with open(marker + ".tmp", "w") as f:
            f.write(pin["sha256"] + "\n")
        os.replace(marker + ".tmp", marker)
    finally:
        shutil.rmtree(longpath(stage), ignore_errors=True)
        if os.path.exists(archive):
            os.remove(archive)
    return executable


def repair_guest_python(data_dir):
    """Upgrade an existing guest bridge without rebuilding game code or changing its compiler."""
    path = os.path.join(data_dir, "guest-sdk.json")
    try:
        with open(path, encoding="utf-8") as f:
            config = json.load(f)
    except (OSError, ValueError) as e:
        raise SetupError("No valid guest SDK configuration to repair: %s" % e)
    if not isinstance(config, dict) or config.get("format_version") not in (1, 2, 3):
        raise SetupError("Unsupported guest SDK configuration version")
    executable = ensure_setup_python(data_dir)
    if config["format_version"] >= 2:
        executable = guest_stored_path(executable, data_dir)
        if appimage_release():
            # issue #230: an AppImage setup up to format 2 stored the paths of that start's mount
            config.update(guest_release_paths(data_dir), format_version=3)
    config["python"] = [executable]
    with open(path + ".tmp", "w", encoding="utf-8") as f:
        json.dump(config, f)
    os.replace(path + ".tmp", path)
    say("Local mod setup Python is ready (compression.zstd verified).")


CLT_DIR = "/Library/Developer/CommandLineTools"


def use_installed_clt(ok):
    """Point xcrun at the installed Command Line Tools (DEVELOPER_DIR, for setup and what it starts) when the
    developer folder macOS uses doesn't work. True when the tools are usable afterwards."""
    if ok():
        return True
    if os.environ.get("DEVELOPER_DIR") or not os.path.exists(os.path.join(CLT_DIR, "usr", "bin", "clang")):
        return False
    os.environ["DEVELOPER_DIR"] = CLT_DIR
    if ok():
        say("  Using the Command Line Tools in %s (macOS points to another developer folder)." % CLT_DIR)
        return True
    del os.environ["DEVELOPER_DIR"]
    return False


def ensure_xcode_clt(ui):
    def ok():
        try:
            return (subprocess.run(["xcode-select", "-p"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
                    and subprocess.run(["xcrun", "--find", "clang"], stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL).returncode == 0)
        except OSError:
            return False

    if ok():
        return
    # macOS can point to another developer folder than the installed Command Line Tools: a removed Xcode, or one
    # whose license hasn't been accepted (issue #219). The tools themselves work, so use them directly then.
    if use_installed_clt(ok):
        return
    if os.path.exists(os.path.join(CLT_DIR, "usr", "bin", "clang")):
        raise SetupError("Apple's Command Line Tools are installed, but macOS can't use them. Open Terminal and run: "
                         "sudo xcode-select --switch " + CLT_DIR + "  (or, if you use Xcode: sudo xcodebuild -license "
                         "accept), then run setup again")
    if not ui.interactive:
        raise SetupError("Apple's Command Line Tools are not installed (run: xcode-select --install)")
    say("  The game code is compiled with Apple's free Command Line Tools, which are not installed yet.")
    say("  A system dialog opens now: click \"Install\" and accept the license. This takes a few minutes.")
    subprocess.run(["xcode-select", "--install"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    while not ok():
        if time.time() - t0 > 3 * 3600:
            raise SetupError("the Command Line Tools installation did not finish; run setup again afterwards")
        time.sleep(5)
    use_installed_clt(ok)
    say("  Command Line Tools installed.")


def get_toolchain(name, data_dir, ui):
    tcs = load_toolchains()["toolchains"]
    if name not in tcs:
        raise SetupError("this release needs an unknown toolchain %r (damaged download?)" % name)
    tc = tcs[name]
    kind = tc["kind"]
    if kind == "xcode-clt":
        ensure_xcode_clt(ui)
        return Toolchain(["xcrun", "clang"], ["xcrun", "clang++"], ["xcrun", "ar"], desc="Apple Command Line Tools")
    root = os.path.join(data_dir, "toolchain")
    os.makedirs(root, exist_ok=True)
    tdir = os.path.join(root, tc["dir"])
    marker = os.path.join(tdir, ".wwhd-toolchain")
    if os.environ.get("WWHD_TOOLCHAIN_DIR"):  # CI: a toolchain already unpacked from the same pinned archive
        tdir = os.environ["WWHD_TOOLCHAIN_DIR"]
    elif not (os.path.isfile(marker) and open(marker).read().strip() == tc["sha256"]):
        need = tc.get("size", 0) * 6
        if free_space(root) < need:
            raise SetupError("not enough free disk space for the compiler (%s needed)" % human(need))
        say("  Getting the compiler (%s, %s download, once)." % (tc["dir"], human(tc.get("size", 0))))
        arc = os.path.join(root, os.path.basename(tc["url"].split("?")[0]))
        download(tc["url"], arc, tc["sha256"], tc.get("size"), "compiler")
        tmp = os.path.join(root, "unpack.tmp")
        shutil.rmtree(longpath(tmp), ignore_errors=True)
        os.makedirs(longpath(tmp))
        say("  Unpacking...")
        if arc.endswith(".zip"):
            # \\?\ paths: the deepest files of llvm-mingw exceed MAX_PATH below a deep release folder (#144)
            with zipfile.ZipFile(arc) as z:
                z.extractall(longpath(tmp))
        elif shutil.which("tar"):
            run_logged(["tar", "-xf", arc, "-C", tmp], what="unpacking the compiler")
        else:
            with tarfile.open(arc) as t:
                t.extractall(longpath(tmp))
        src = os.path.join(tmp, tc["dir"])
        if not os.path.isdir(longpath(src)):
            raise SetupError("unexpected compiler archive layout")
        shutil.rmtree(longpath(tdir), ignore_errors=True)
        os.replace(longpath(src), longpath(tdir))
        shutil.rmtree(longpath(tmp), ignore_errors=True)
        os.remove(arc)
        with open(marker, "w") as f:
            f.write(tc["sha256"] + "\n")
    if kind == "llvm-mingw":
        b = os.path.join(tdir, "bin")
        triple = tc.get("triple", "x86_64-w64-mingw32")  # aarch64-w64-mingw32: the Windows on ARM64 release
        return Toolchain([os.path.join(b, triple + "-clang.exe")], [os.path.join(b, triple + "-clang++.exe")],
                         [os.path.join(b, "llvm-ar.exe")], rsp=True, desc="llvm-mingw " + tc["dir"])
    if kind == "zig":
        z = os.path.join(tdir, "zig")
        env = dict(os.environ)
        env.setdefault("ZIG_GLOBAL_CACHE_DIR", os.path.join(root, "zig-cache"))
        t = ["-target", tc["target"]]
        return Toolchain([z, "cc"] + t, [z, "c++"] + t, [z, "ar"], env=env, desc="zig " + tc["dir"])
    raise SetupError("unsupported toolchain kind %r" % kind)


# ---------------------------------------------------------------------------------------------
# game files


def extractor():
    p = os.path.join(PKG, "tools", "bin", "wwhd-extract" + EXE_SUFFIX)
    if not os.path.isfile(p):
        raise SetupError("tools/bin/wwhd-extract%s is missing from this release folder (incomplete download?)" % EXE_SUFFIX)
    return p


def disc_info(image, keys):
    p = subprocess.run([extractor(), "--keys-stdin", "info", image], input=keys.stdin_blob(), stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    err = p.stderr.decode("utf-8", "replace").strip()
    LOG.write("wwhd-extract info: exit %d %s" % (p.returncode, err))
    if p.returncode != 0:
        return EXTRACT_ERRORS.get(p.returncode, "image_bad"), err, None
    info = {}
    for line in p.stdout.decode().splitlines():
        k, _, v = line.partition(" ")
        info[k] = v
    return None, "", info


def check_title(title_id):
    """Raises SetupError unless this is a version of the game the port can be built from."""
    if title_id in SUPPORTED_BUILDS:
        return
    region = TITLE_IDS.get(title_id)
    if region:
        raise SetupError("this is the %s version of The Wind Waker HD (title %s). The port can be built from %s."
                         % (region, title_id, supported_titles_text()))
    raise SetupError("this disc is not The Wind Waker HD (title id %s)" % title_id)


def title_desc(tid, version=None):
    """'The Wind Waker HD (USA), version 0', 'the update for The Wind Waker HD (USA), version 16', ..."""
    tid = tid.lower()
    region = TITLE_IDS.get("00050000" + tid[8:])
    name = "The Wind Waker HD (%s)" % region if region else "title %s-%s" % (tid[:8].upper(), tid[8:].upper())
    kind = tid[:8]
    v = "" if version is None else ", version %d" % version
    if kind == "0005000e":
        return "the update for %s%s" % (name, v)
    if kind == "0005000c":
        return "downloadable content for %s%s" % (name, v)
    return name + v


_ANY_SUPPORTED = object()   # archive_info: "whichever supported build the archive holds"


def archive_info(path, title=_ANY_SUPPORTED):
    """wwhd-extract info on a Cemu archive, asking for a title (by default: whichever supported build
    the archive holds; None: just list them).
    Returns (problem, message, info);
    info: {"titles": [{id, version, folder, files, bytes}], "selected", "title_id", "version", "files", "bytes"}
    (also for problem "wrong_title": what the archive does contain)."""
    if title is _ANY_SUPPORTED:
        # which build is in there (one pass that only lists), then ask for that one
        _, _, listed = _archive_info_one(path, None)
        title = next((t["id"] for t in listed.get("titles", []) if t["id"] in SUPPORTED_BUILDS), SUPPORTED_TITLE)
    return _archive_info_one(path, title)


def _archive_info_one(path, title):
    p = subprocess.run([extractor()] + (["--title", title] if title else []) + ["info", path], stdin=subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    err = p.stderr.decode("utf-8", "replace").strip()
    LOG.write("wwhd-extract info (archive): exit %d %s" % (p.returncode, err))
    info = {"titles": []}
    for line in p.stdout.decode("utf-8", "replace").splitlines():
        k, _, v = line.partition(" ")
        if k == "title":
            f = v.split(" ")
            if len(f) == 5 and f[1].isdigit():
                info["titles"].append({"id": f[0], "version": int(f[1]), "folder": f[2], "files": int(f[3]),
                                       "bytes": int(f[4])})
        else:
            info[k] = v
    if p.returncode != 0:
        return EXTRACT_ERRORS.get(p.returncode, "image_bad"), err.replace("error: ", "", 1), info
    if info.get("format") != "wua":
        return "image_bad", "not a Cemu Wii U archive", info
    return None, "", info


def archive_choice(info):
    """Which title of a Cemu archive the port uses, and why. Returns (folder, notes) or raises SetupError
    with the reason the archive cannot be used."""
    titles = info.get("titles", [])
    found = ", ".join("%s (%s)" % (title_desc(t["id"], t["version"]), t["folder"]) for t in titles) or "no Wii U titles"
    if not info.get("selected"):
        games = [t for t in titles if t["id"][:8] == "00050000"]
        for t in games:
            if t["id"] in TITLE_IDS:
                try:
                    check_title(t["id"])
                except SetupError as e:
                    raise SetupError("this archive contains %s" % str(e)[len("this is "):])
        updates = ["0005000e" + t[8:] for t in SUPPORTED_BUILDS]
        if any(t["id"] in updates for t in titles):
            raise SetupError("this archive contains only the update for The Wind Waker HD, not the game itself "
                             "(%s). In Cemu, make the archive with the game included (it contains: %s)."
                             % (supported_titles_text(), found))
        raise SetupError("this archive does not contain The Wind Waker HD, %s (it contains: %s)."
                         % (supported_titles_text(), found))
    version = int(info.get("version", -1))
    if version != SUPPORTED_VERSION:
        raise SetupError("the game in this archive is version %d; the port is built for version %d of The Wind Waker "
                         "HD, the disc and eShop release (folder %s)." % (version, SUPPORTED_VERSION, info["selected"]))
    notes = []
    for t in titles:
        if t["folder"] == info["selected"]:
            continue
        if t["id"] in ["0005000e" + x[8:] for x in SUPPORTED_BUILDS]:
            notes.append("Not used: %s (%s). The port is built for the game's own code (version %d); the update "
                         "replaces that code, and its data files belong to the updated code, so the game is set up "
                         "from the base game alone." % (title_desc(t["id"], t["version"]), t["folder"], SUPPORTED_VERSION))
        else:
            notes.append("Not used: %s (%s), not needed for this game." % (title_desc(t["id"], t["version"]), t["folder"]))
    return info["selected"], notes


def game_folder_title(path):
    meta = os.path.join(path, "meta", "meta.xml")
    try:
        with open(meta, "rb") as f:
            m = re.search(rb"<title_id[^>]*>\s*([0-9A-Fa-f]{16})\s*<", f.read())
        return m.group(1).decode().lower() if m else None
    except OSError:
        return None


def code_title_version(path):
    """(title id, title version) from code/app.xml (the code's own metadata), else from meta/meta.xml;
    None where it cannot be read."""
    for rel, base in ((("code", "app.xml"), 16), (("meta", "meta.xml"), 10)):
        try:
            with open(os.path.join(path, *rel), "rb") as f:
                d = f.read(65536)
        except OSError:
            continue
        tid = re.search(rb"<title_id[^>]*>\s*([0-9A-Fa-f]{16})\s*<", d)
        ver = re.search(rb"<title_version[^>]*>\s*([0-9A-Fa-f]+)\s*<", d)
        if tid or ver:
            try:
                v = int(ver.group(1), base) if ver else None
            except ValueError:
                v = None
            return (tid.group(1).decode().lower() if tid else None), v
    return None, None


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


GAME_VERSION_FIX = ("Use the game's own files: a disc image (.wud/.wux), a Cemu archive (.wua; setup takes the game from it "
                    "and leaves an update out), or the game's folder exactly as dumped (in Cemu: "
                    "mlc01/usr/title/00050000/10143500 for the USA game, 10143600 for the European one; not the "
                    "update in 0005000e/...), without update files copied over it.")


def check_game_version(path):
    """The build of the game in `path` (tools/recomp/builds.py), or SetupError if it is not one the
    port can be built from. The translated code and its hooks are made for exactly those files
    (version 0 of each region), so this runs before translating."""
    rpx = os.path.join(path, "code", "cking.rpx")
    try:
        digest = file_sha256(rpx)
    except OSError as e:
        raise SetupError("cannot read %s: %s" % (rpx, e))
    build = game_builds.by_sha256(digest)
    if build:
        return build
    tid, ver = code_title_version(path)
    updates = ["0005000e" + t[8:] for t in SUPPORTED_BUILDS]
    if tid and tid not in SUPPORTED_BUILDS and tid not in updates:
        found = "the game code (code/cking.rpx) is from %s (title %s-%s)" % (title_desc(tid, ver), tid[:8].upper(),
                                                                            tid[8:].upper())
    elif tid in updates or ver:
        found = ("the game code (code/cking.rpx) is %s (per the folder's app.xml/meta.xml): this looks like the "
                 "game with an update merged in" % ("version %d of the game" % ver if ver else "from the update"))
    else:
        found = ("the game code (code/cking.rpx) is not a file the port knows (SHA-256 %s...): not version 0 of "
                 "the game, or a modified or damaged copy" % digest[:16])
    LOG.write("cking.rpx SHA-256 %s, known: %s" % (
        digest, ", ".join("%s %s" % (b.name, b.sha256[:16]) for b in SUPPORTED_BUILDS.values())))
    raise SetupError("%s. The port needs The Wind Waker HD, %s, version 0 (the disc or eShop release, without the "
                     "update). %s" % (found, supported_titles_text(), GAME_VERSION_FIX))


def valid_game_folder(path):
    return (os.path.isfile(os.path.join(path, "code", "cking.rpx")) and os.path.isdir(os.path.join(path, "content"))
            and os.path.isfile(os.path.join(path, "meta", "meta.xml")))


def folder_size(path):
    total = 0
    for dp, _, fns in os.walk(longpath(path)):
        for fn in fns:
            try:
                total += os.path.getsize(os.path.join(dp, fn))
            except OSError:
                pass
    return total


def get_disc_keys(image, ui, args):
    req = {"disc_key_file": args.disc_key, "common_key_file": args.common_key}
    while True:
        keys, info, why, notices = resolve_disc_keys(image, req)
        if not why:
            for notice in notices:
                say("  " + notice)
            return keys, info
        problem, message = why
        if not ui.interactive or not problem.startswith(("disc_key", "common_key")):
            raise SetupError(message)
        say("  " + message)
        name = "disc" if problem.startswith("disc_key") else "common"
        choice = ui.choose("Provide the %s key (Cemu keeps both keys in keys.txt)." % name,
                           ["choose a key file / keys.txt", "paste it (32 hex digits; the input is hidden)"])
        req.pop(name + "_key_file", None)
        req.pop(name + "_key_hex", None)
        if choice == 0:
            req[name + "_key_file"] = ui.pick_path("Choose the key file / keys.txt")
        else:
            req[name + "_key_hex"] = ui.secret("Paste the %s key and press Enter:" % name)


def game_staging_dir(data_dir):
    """The empty folder the game files are extracted or copied into before they replace data/game. As
    short as "game" (issue #144): the game's deepest file names then stay as deep below the data folder
    as in data/game (DEEPEST_DATA_PATH). Releases up to v0.2.12 used game.partial."""
    for old in ("game.partial", "gnew"):
        shutil.rmtree(longpath(os.path.join(data_dir, old)), ignore_errors=True)
    return os.path.join(data_dir, "gnew")


def extract_game(image, keys, info, data_dir, title=None):
    """Extracts a disc image (keys) or one title folder of a Cemu archive (title, no keys) into data_dir/game."""
    dst = os.path.join(data_dir, "game")
    tmp = game_staging_dir(data_dir)
    total = int(info.get("bytes", 0))
    need = total + (1 << 30)
    if free_space(data_dir) < need:
        raise SetupError("not enough free disk space in %s: %s needed" % (data_dir, human(need)))
    run_extract(image, keys, tmp, title)
    if not valid_game_folder(tmp):
        raise SetupError("the extracted files are incomplete (no code/cking.rpx)")
    try:
        check_game_version(tmp)
    except SetupError:
        shutil.rmtree(longpath(tmp), ignore_errors=True)  # keeps the game files of an earlier setup
        raise
    replace_dir(tmp, dst)


def run_extract(image, keys, out, title=None, only=None):
    """wwhd-extract into out: a disc image (keys) or one title of a Cemu archive (title, no keys);
    only: path patterns (wwhd-extract --only) to take just those files. Removes out on failure."""
    pr = Progress("extracting")
    opts = []
    for pattern in only or []:
        opts += ["--only", pattern]
    if title:
        cmd = [extractor(), "--title", title, "--progress"] + opts + ["extract", image, out]
    else:
        cmd = [extractor(), "--keys-stdin", "--progress"] + opts + ["extract", image, out]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    errbuf = []
    t = threading.Thread(target=lambda: errbuf.append(p.stderr.read()))
    t.start()
    if keys:
        p.stdin.write(keys.stdin_blob())
    p.stdin.close()
    what, verifying = "", False
    for line in p.stdout:
        parts = line.decode().split()
        if parts == ["phase", "verify"]:  # an archive: its SHA-256 is checked before anything is written
            pr, verifying = Progress("checking the archive"), True
            what = "checking the archive: " if GUI else ""  # the window shows only the detail text
        elif parts == ["phase", "extract"]:
            if verifying:
                pr.done()
            pr, what, verifying = Progress("extracting"), "", False
        elif len(parts) == 3 and parts[0] == "progress":
            pr.update(int(parts[1]), int(parts[2]) or 1, "%s%s of %s" % (what, human(int(parts[1])), human(int(parts[2]))))
    rc = p.wait()
    t.join()
    err = b"".join(errbuf).decode("utf-8", "replace").strip()
    LOG.write(err)
    if rc != 0:
        shutil.rmtree(longpath(out), ignore_errors=True)
        raise SetupError("extracting the game failed: %s" % err)
    pr.done()


def copy_game_folder(src, data_dir):
    dst = os.path.join(data_dir, "game")
    if os.path.realpath(src) == os.path.realpath(dst):
        return
    total = folder_size(src)
    if free_space(data_dir) < total + (1 << 30):
        raise SetupError("not enough free disk space in %s: %s needed" % (data_dir, human(total + (1 << 30))))
    tmp = game_staging_dir(data_dir)
    pr = Progress("copying")
    done = 0
    long_src, long_tmp = longpath(src), longpath(tmp)
    for dp, dns, fns in os.walk(long_src):
        rel = os.path.relpath(dp, long_src)
        os.makedirs(os.path.join(long_tmp, rel), exist_ok=True)
        for fn in fns:
            s = os.path.join(dp, fn)
            shutil.copyfile(s, os.path.join(long_tmp, rel, fn))
            done += os.path.getsize(s)
            pr.update(done, total or 1, "%s of %s" % (human(done), human(total)))
    pr.done()
    replace_dir(tmp, dst)


def replace_dir(new, dst):
    old = dst + ".old"
    shutil.rmtree(longpath(old), ignore_errors=True)
    if os.path.exists(dst):
        os.replace(longpath(dst), longpath(old))
    os.replace(longpath(new), longpath(dst))
    shutil.rmtree(longpath(old), ignore_errors=True)


# ---------------------------------------------------------------------------------------------
# language sources (experimental; docs/language-packs.md)
#
# The USA game code with the text, fonts and localised 2D layouts of the player's own European or
# Japanese game: the game picks its 2D pack (content/Common/Pack/permanent_2d_<Region><Language>.pack:
# every message, the fonts and the layouts) by the console language and region, and the runtime can
# give it the European or Japanese ones (runtime/src/game_languages.h). Only those packs (and the
# disc's meta.xml, for its title id) are taken from the second game, into data/game-lang/<EU|JP>.
# The second game's code is never used. Tested with the European game; untested with the Japanese one.

LANGUAGE_SOURCE_TITLES = {"0005000010143600": "EU", "0005000010143400": "JP"}
LANGUAGE_SOURCE_FILES = ["content/Common/Pack/permanent_2d_*.pack", "meta/meta.xml"]
# the packs the game knows per region (cking.rpx, 0x1048DD4C) and their languages
LANGUAGE_PACKS = {
    "EU": {"permanent_2d_euenglish.pack": "English", "permanent_2d_eufrench.pack": "French",
           "permanent_2d_eugerman.pack": "German", "permanent_2d_euitalian.pack": "Italian",
           "permanent_2d_euspanish.pack": "Spanish"},
    "JP": {"permanent_2d_jpjapanese.pack": "Japanese"},
}
LANGUAGE_REGION_NAMES = {"EU": "Europe", "JP": "Japan"}
LANGUAGE_SOURCE_MANIFEST = "language-source.json"


def language_root(data_dir):
    return os.path.join(data_dir, "game-lang")


def language_source_region(title_id):
    """EU or JP for the title id of a European or Japanese game; SetupError (why it can't be used) otherwise."""
    tid = (title_id or "").lower()
    if tid in LANGUAGE_SOURCE_TITLES:
        return LANGUAGE_SOURCE_TITLES[tid]
    if tid == SUPPORTED_TITLE:
        raise SetupError("this is the USA game, whose languages (English, French, Spanish) the port already has. A "
                         "language source is the European (00050000-10143600) or Japanese (00050000-10143400) game")
    if tid[:8] in ("0005000e", "0005000c") and ("00050000" + tid[8:]) in TITLE_IDS:
        raise SetupError("this is %s, not the game itself: a language source is the European or Japanese game "
                         "(title 00050000-10143600 or 00050000-10143400)" % title_desc(tid))
    raise SetupError("this is not the European or Japanese version of The Wind Waker HD (title %s); a language source "
                     "is title 00050000-10143600 (Europe) or 00050000-10143400 (Japan)" % (tid or "unknown"))


def find_dir_nocase(base, *parts):
    """base/part/... matched without case (a disc's spelling on any host); None if a part is missing."""
    at = base
    for part in parts:
        try:
            names = os.listdir(at)
        except OSError:
            return None
        hit = [n for n in names if n.lower() == part.lower() and os.path.isdir(os.path.join(at, n))]
        if not hit:
            return None
        at = os.path.join(at, sorted(hit)[0])
    return at


def language_files_in(folder):
    """The language packs in an extracted game folder: [(file name, path)], any case."""
    pack = find_dir_nocase(folder, "content", "Common", "Pack")
    if not pack:
        return []
    return sorted((n, os.path.join(pack, n)) for n in os.listdir(pack)
                  if re.match(r"permanent_2d_.*\.pack$", n, re.I) and os.path.isfile(os.path.join(pack, n)))


def finish_language_source(tmp, region, title_id, source_name, data_dir):
    """Checks the packs taken into tmp (an extracted folder), writes its manifest and moves it to
    data/game-lang/<region>. Returns the manifest."""
    known = LANGUAGE_PACKS[region]
    packs, ignored = [], []
    for name, path in language_files_in(tmp):
        if name.lower() not in known:
            ignored.append(name)
            os.remove(path)  # only the packs the game can load are kept
            continue
        with open(path, "rb") as f:
            head = f.read(4)
        if head != b"SARC":
            shutil.rmtree(longpath(tmp), ignore_errors=True)
            raise SetupError("%s is not a language pack (damaged or not from the game)" % name)
        packs.append({"file": name, "language": known[name.lower()], "bytes": os.path.getsize(path),
                      "sha256": file_sha256(path)})
    if not packs:
        shutil.rmtree(longpath(tmp), ignore_errors=True)
        raise SetupError("no language packs (content/Common/Pack/permanent_2d_%s*.pack) were found in this %s game"
                         % ("Eu" if region == "EU" else "Jp", LANGUAGE_REGION_NAMES[region]))
    for name in ignored:
        say("  Not used: %s (the game does not know this pack)" % name)
    manifest = {"format_version": 1, "region": region, "title_id": title_id, "source": source_name,
                "packs": packs,
                "created": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    with open(os.path.join(tmp, LANGUAGE_SOURCE_MANIFEST), "w") as f:
        json.dump(manifest, f, indent=1)
    dst = os.path.join(language_root(data_dir), region)
    replace_dir(tmp, dst)
    say("  %s game: %s" % (LANGUAGE_REGION_NAMES[region], ", ".join(p["language"] for p in packs)))
    LOG.write("language source %s from %s: %s" % (region, source_name, ", ".join(p["file"] for p in packs)))
    return manifest


def language_sources(data_dir):
    """The installed language sources: [manifest] (with "dir")."""
    out = []
    root = language_root(data_dir)
    for region in sorted(LANGUAGE_PACKS):
        d = os.path.join(root, region)
        try:
            with open(os.path.join(d, LANGUAGE_SOURCE_MANIFEST)) as f:
                m = json.load(f)
        except (OSError, ValueError):
            continue
        m["dir"] = d
        out.append(m)
    return out


def remove_language_source(data_dir, region):
    region = (region or "").upper()
    if region not in LANGUAGE_PACKS:
        raise SetupError("unknown language source %r (EU or JP)" % region)
    d = os.path.join(language_root(data_dir), region)
    if not os.path.isdir(d):
        raise SetupError("there is no %s language source" % LANGUAGE_REGION_NAMES[region])
    shutil.rmtree(longpath(d))
    return d


def installed_build(game_dir):
    """The build the installed game files are, or None when nothing is installed yet."""
    try:
        return game_builds.by_sha256(file_sha256(os.path.join(game_dir, "code", "cking.rpx")))
    except OSError:
        return None


def check_language_source_allowed(game_dir):
    """A language source lends the text of a European or Japanese game to the USA game's code
    (docs/language-packs.md). Only the USA build needs and can use one: the European build has those
    five languages itself, and the port's region patch (hooks_language.txt) is for the USA code."""
    build = installed_build(game_dir)
    if build is None or build.canonical:
        return
    raise SetupError("the installed game is the %s build, which has its own languages; a language source is only "
                     "for the USA build (docs/language-packs.md). Choose the language in the game's settings "
                     "(F1) > Language instead." % build.name)


def language_staging_dir(root, region):
    """The empty folder a language source is extracted into first: game-lang/<region>.tmp (short, issue
    #144; releases up to v0.2.12 used <region>.partial)."""
    for name in (region + ".partial", region + ".tmp"):
        shutil.rmtree(longpath(os.path.join(root, name)), ignore_errors=True)
    return os.path.join(root, region + ".tmp")


def add_language_source(source, data_dir, keys=None, info=None, game_dir=None):
    """Takes the language packs of a European or Japanese game: source ("image", path) with keys and
    disc info (wwhd-extract info), ("archive", path) or ("folder", path). Returns [manifest] (an archive
    can hold both)."""
    kind, path = source
    check_language_source_allowed(game_dir or os.path.join(data_dir, "game"))
    root = language_root(data_dir)
    os.makedirs(root, exist_ok=True)
    if free_space(root) < (200 << 20):
        raise SetupError("not enough free disk space in %s: about 200 MB needed" % root)
    name = os.path.basename(path.rstrip("/\\"))
    if kind == "image":
        region = language_source_region(info.get("title_id"))
        tmp = language_staging_dir(root, region)
        run_extract(path, keys, tmp, only=LANGUAGE_SOURCE_FILES)
        return [finish_language_source(tmp, region, info["title_id"].lower(), name, data_dir)]
    if kind == "archive":
        problem, err, ainfo = archive_info(path, title=None)
        if problem and problem != "wrong_title":
            raise SetupError("cannot read the archive: %s" % err)
        titles = [t for t in ainfo.get("titles", []) if t["id"] in LANGUAGE_SOURCE_TITLES]
        if not titles:
            found = ", ".join("%s (%s)" % (title_desc(t["id"], t["version"]), t["folder"]) for t in ainfo.get("titles", []))
            for t in ainfo.get("titles", []):
                language_source_region(t["id"])  # raises with the reason (the USA game, an update...)
            raise SetupError("this archive contains no European or Japanese Wind Waker HD (it contains: %s)"
                             % (found or "no Wii U titles"))
        out = []
        for t in titles:
            region = LANGUAGE_SOURCE_TITLES[t["id"]]
            tmp = language_staging_dir(root, region)
            run_extract(path, None, tmp, title=t["folder"], only=LANGUAGE_SOURCE_FILES)
            out.append(finish_language_source(tmp, region, t["id"], "%s (%s)" % (name, t["folder"]), data_dir))
        return out
    if kind == "folder":
        folder = path
        if not os.path.isfile(os.path.join(folder, "meta", "meta.xml")) and \
                os.path.isfile(os.path.join(os.path.dirname(folder), "meta", "meta.xml")):
            folder = os.path.dirname(folder)
        tid = game_folder_title(folder) or code_title_version(folder)[0]
        region = language_source_region(tid)
        files = language_files_in(folder)
        tmp = language_staging_dir(root, region)
        dst = os.path.join(tmp, "content", "Common", "Pack")
        os.makedirs(dst)
        os.makedirs(os.path.join(tmp, "meta"))
        shutil.copyfile(os.path.join(folder, "meta", "meta.xml"), os.path.join(tmp, "meta", "meta.xml"))
        total = sum(os.path.getsize(p) for _, p in files) or 1
        pr, done = Progress("copying"), 0
        for n, p in files:
            shutil.copyfile(p, os.path.join(dst, n))
            done += os.path.getsize(p)
            pr.update(done, total, "%s of %s" % (human(done), human(total)))
        pr.done()
        return [finish_language_source(tmp, region, tid, name, data_dir)]
    raise SetupError("unknown language source kind %r" % kind)


def language_source_kind(path):
    if os.path.isdir(path):
        return "folder"
    if path.lower().endswith(".wua"):
        return "archive"
    if path.lower().endswith((".wux", ".wud")):
        return "image"
    raise SetupError("choose a .wux or .wud disc image, a .wua Cemu archive or an extracted game folder")


# ---------------------------------------------------------------------------------------------
# build


def decomp_hooks_file():
    """The decomp release (docs/decomp-build.md; tools/release/package.py): its runtime runs the decompiled
    game functions listed in sdk/decomp_hooks.txt, so the game code is translated with that list
    (recomp.py --decomp-hooks). None for the regular release."""
    p = os.path.join(PKG, "sdk", "decomp_hooks.txt")
    return p if os.path.isfile(p) else None


def recompile(game_dir, gen_dir, hooks=False):
    shutil.rmtree(longpath(gen_dir), ignore_errors=True)
    rpx = os.path.join(game_dir, "code", "cking.rpx")
    decomp = decomp_hooks_file()
    if decomp:
        build = game_builds.identify(rpx)
        if build is not None and not build.canonical:
            raise SetupError("this is the decomp build of the port, which works with the USA game only (yours is the %s "
                             "build); use the regular release" % build.name)
    out = run_logged([sys.executable, os.path.join(PKG, "tools", "recomp", "recomp.py"), rpx, gen_dir] +
                     (["--mod-hooks"] if hooks else []) + (["--decomp-hooks", decomp] if decomp else []),
                     env=dict(os.environ, WWHD_RECOMP_MOD_HOOKS="0"), what="translating the game code")
    n = len(glob.glob(os.path.join(glob.escape(gen_dir), "code_*.c")))
    if n == 0:
        raise SetupError("the recompiler wrote no code")
    return n


def default_jobs():
    n = os.cpu_count() or 2
    mem = None
    try:
        if IS_MAC:
            mem = int(subprocess.check_output(["sysctl", "-n", "hw.memsize"]))
        elif IS_LINUX:
            with open("/proc/meminfo") as f:
                mem = int(re.search(r"MemTotal:\s+(\d+)", f.read()).group(1)) * 1024
        elif IS_WIN:
            import ctypes

            class MS(ctypes.Structure):
                _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                            ("ullTotalPhys", ctypes.c_ulonglong), ("a", ctypes.c_ulonglong * 6)]
            ms = MS()
            ms.dwLength = ctypes.sizeof(MS)
            ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(ms))
            mem = ms.ullTotalPhys
    except Exception:
        pass
    if mem:
        n = min(n, max(1, int(mem / (1.5 * (1 << 30)))))  # about 1.5 GB per compiler process
    return max(1, n)


def sub(arg, m):
    for k, v in m.items():
        arg = arg.replace("{%s}" % k, v)
    return arg


def fwd(p):
    return p.replace("\\", "/")


def compile_gamecode(tc, manifest, gen_dir, obj_dir, jobs, cancel=None, progress=None):
    os.makedirs(obj_dir, exist_ok=True)
    srcs = sorted(glob.glob(os.path.join(glob.escape(gen_dir), "code_*.c")))
    srcs += [os.path.join(gen_dir, f) for f in ("table.c", "imports.c")]
    srcs.sort(key=lambda s: -os.path.getsize(s))  # big files first: better use of the cores
    m = {"sdk": fwd(os.path.join(PKG, "sdk")), "gen": fwd(gen_dir)}
    flags = [sub(a, m) for a in manifest["gamecode_cflags"]]
    objs = []
    pr = Progress("compiling")
    total = len(srcs)
    done = [0]
    lock = threading.Lock()
    failures = []

    def one(src):
        if cancel:
            cancel()
        obj = os.path.join(obj_dir, os.path.basename(src) + ".o")
        cmd = tc.cc + flags + ["-c", fwd(src), "-o", fwd(obj)]
        p = subprocess.run(cmd, env=tc.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = p.stdout.decode("utf-8", "replace")
        with lock:
            if p.returncode != 0:
                failures.append((src, out))
                LOG.write("$ " + " ".join(cmd) + "\n" + out)
            done[0] += 1
            if progress:
                progress(done[0], total)
            pr.update(done[0], total, "%d of %d files" % (done[0], total))
        return obj

    LOG.write("compile flags: " + " ".join(tc.cc + flags))
    pr.update(0, total, "0 of %d files (%d at a time)" % (total, jobs), force=True)
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        for f in as_completed([ex.submit(one, s) for s in srcs]):
            objs.append(f.result())
    if failures:
        src, out = failures[0]
        raise SetupError("compiling %s failed:\n%s" % (os.path.basename(src), "\n".join(out.strip().splitlines()[-20:])))
    pr.done("%d files" % total)
    return sorted(objs)


# A macOS SDK newer than the Command Line Tools' linker (e.g. a beta SDK installed by a partial update):
# its .tbd stubs name architectures the linker doesn't know ("unknown architecture arm64e.x1-macos",
# "tapi error: malformed file"), so every system library fails to link.
MAC_SDK_MISMATCH = re.compile(r"tapi error: malformed file|\.tbd:\d+:\d+: error: unknown architecture")


def mac_sdk_version(path):
    m = re.search(r"MacOSX(\d+)(?:\.(\d+))?\.sdk$", os.path.basename(os.path.normpath(path)))
    return (int(m.group(1)), int(m.group(2) or 0)) if m else None


def mac_sdks():
    """The installed macOS SDKs (Command Line Tools and Xcode), newest first, as (version, real path)."""
    found = {}
    for pattern in ("/Library/Developer/CommandLineTools/SDKs/MacOSX*.sdk",
                    "/Applications/Xcode*.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX*.sdk"):
        for path in glob.glob(pattern):
            real = os.path.realpath(path)
            version = mac_sdk_version(real) or mac_sdk_version(path)
            if version and os.path.isdir(real):
                found.setdefault(real, version)
    return sorted(((v, p) for p, v in found.items()), reverse=True)


def mac_current_sdk():
    try:
        out = subprocess.run(["xcrun", "--sdk", "macosx", "--show-sdk-path"], stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL).stdout.decode("utf-8", "replace").strip()
    except OSError:
        return None
    return os.path.realpath(out) if out else None


def mac_fallback_sdks(current, sdks, minimum=(14, 0)):
    """SDKs to retry the link with when `current` doesn't match the linker: older ones first by version
    (newest of them first), never below the game's deployment target."""
    # the current SDK's real path can be unversioned (Xcode's MacOSX.sdk): take its version from the list,
    # where the versioned name pointing at it was found
    cur = next((v for v, p in sdks if p == current), None) or (mac_sdk_version(current) if current else None)
    return [p for v, p in sdks if p != current and v >= minimum and (cur is None or v < cur)]


def link_with_sdk_fallback(cmd, env):
    """Links; on macOS, a linker that rejects the SDK's library stubs is retried with an older installed SDK."""
    try:
        run_logged(cmd, env=env, what="linking the game")
        return
    except SetupError as e:
        if not (IS_MAC and MAC_SDK_MISMATCH.search(str(e))) or (env or os.environ).get("SDKROOT"):
            raise
        failed = e
    current = mac_current_sdk()
    say("  The macOS SDK (%s) is newer than the linker of the Command Line Tools; trying an older installed SDK."
        % os.path.basename(current or "?"))
    for sdk in mac_fallback_sdks(current, mac_sdks()):
        say("  Linking with %s" % os.path.basename(sdk))
        try:
            run_logged(cmd, env=dict(env or os.environ, SDKROOT=sdk), what="linking the game")
            say("  Linked with %s. Updating the Command Line Tools (Software Update) fixes the mismatch for good."
                % os.path.basename(sdk))
            return
        except SetupError as e:
            if not MAC_SDK_MISMATCH.search(str(e)):
                raise
    raise SetupError("linking the game failed: the installed macOS SDK (%s) doesn't match the linker of the "
                     "Command Line Tools, and no older SDK works. Update the Command Line Tools (System Settings "
                     "> General > Software Update, or run: xcode-select --install), then run setup again.\n\n%s"
                     % (os.path.basename(current or "?"), failed))


def link_game(tc, manifest, objs, work, out_exe, recipe=None, glue=()):
    """recipe: another link line than manifest["link"] (the decomp version: decomp_setup.py), whose
    {decomp_glue} stands for the objects `glue`"""
    lib = os.path.join(work, "libgamecode.a")
    if os.path.exists(lib):
        os.remove(lib)
    if tc.rsp:
        rsp = os.path.join(work, "ar.rsp")
        with open(rsp, "w", encoding="utf-8") as f:  # LLVM reads response files as UTF-8; the folder may hold any character
            f.write("\n".join('"%s"' % fwd(o) for o in objs))
        run_logged(tc.ar + ["rcs", fwd(lib), "@" + fwd(rsp)], env=tc.env, what="archiving the game code")
    else:
        run_logged(tc.ar + ["rcs", lib] + objs, env=tc.env, what="archiving the game code")
    m = {"sdk": fwd(os.path.join(PKG, "sdk")), "gamecode": fwd(lib), "out": fwd(out_exe)}
    args = []
    for a in (recipe or manifest["link"]):
        args += [fwd(g) for g in glue] if a == "{decomp_glue}" else [sub(a, m)]
    if IS_LINUX:
        args += ["-Wl,-rpath,$ORIGIN,--strip-debug"]
    elif IS_MAC:
        # the runtime libraries the release installs next to the game (manifest runtime_files: the Vulkan
        # loader and MoltenVK) are found there, never in Homebrew or elsewhere on the Mac
        args += ["-Wl,-rpath,@executable_path"]
    if tc.rsp:
        rsp = os.path.join(work, "link.rsp")
        with open(rsp, "w", encoding="utf-8") as f:
            f.write("\n".join('"%s"' % a.replace('"', '\\"') for a in args))
        cmd = tc.cxx + ["@" + fwd(rsp)]
    else:
        cmd = tc.cxx + args
    link_with_sdk_fallback(cmd, tc.env)
    if not os.path.isfile(out_exe):
        raise SetupError("the linker produced no executable")


# ---------------------------------------------------------------------------------------------
# launchers


def mac_icns(png):
    """An .icns holding one PNG image (128x128 'ic07'; macOS scales it for larger sizes)."""
    entry = b"ic07" + struct.pack(">I", 8 + len(png)) + png
    return b"icns" + struct.pack(">I", 8 + len(entry)) + entry


def mac_app(app_path, exe_src, data_dir, version, runtime_files=()):
    """~/Applications/Wind Waker HD.app: the game binary plus a launcher that points it at the data folder.
    runtime_files (next to exe_src) go next to the binary as well: its rpath is @executable_path."""
    tmp = app_path + ".tmp"
    shutil.rmtree(longpath(tmp), ignore_errors=True)
    macos = os.path.join(tmp, "Contents", "MacOS")
    os.makedirs(macos)
    shutil.copy2(exe_src, os.path.join(macos, "wwhd"))
    for rf in runtime_files:
        shutil.copy2(os.path.join(os.path.dirname(exe_src), rf), os.path.join(macos, rf))
    launcher = os.path.join(macos, "launch")
    with open(launcher, "w") as f:
        f.write('#!/bin/sh\n# written by the Wind Waker HD setup\ncd "%s" || exit 1\nexec "$(dirname "$0")/wwhd" '
                '--game game --save save "$@"\n' % data_dir.replace('"', '\\"'))
    os.chmod(launcher, 0o755)
    plist = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>%s</string>
  <key>CFBundleDisplayName</key><string>%s</string>
  <key>CFBundleIdentifier</key><string>io.github.zeldawwhdrecomp.wwhd</string>
  <key>CFBundleExecutable</key><string>launch</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>%s</string>
  <key>CFBundleVersion</key><string>%s</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>%s
</dict></plist>
"""
    # the game's own icon (made here from the player's game files; the release ships none)
    png = game_icon_png(data_dir)
    icon_key = ""
    if png:
        resources = os.path.join(tmp, "Contents", "Resources")
        os.makedirs(resources)
        with open(os.path.join(resources, "AppIcon.icns"), "wb") as f:
            f.write(mac_icns(png))
        icon_key = "\n  <key>CFBundleIconFile</key><string>AppIcon</string>"
    plist = plist % (APP_NAME, APP_NAME, version.lstrip("v"), version.lstrip("v"), icon_key)
    with open(os.path.join(tmp, "Contents", "Info.plist"), "w") as f:
        f.write(plist)
    subprocess.run(["codesign", "--force", "--deep", "-s", "-", tmp], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.makedirs(os.path.dirname(app_path), exist_ok=True)
    replace_dir(tmp, app_path)


# guest-sdk.json (runtime/src/mods/guest_build.h): format 2 adds paths relative to its folder (portable
# releases), format 3 "$RELEASE/<path>" for paths inside an AppImage (issue #230)
GUEST_RELEASE_MARKER = "$RELEASE/"


def path_inside(root, path):
    try:
        return os.path.commonpath([os.path.abspath(root), os.path.abspath(path)]) == os.path.abspath(root)
    except ValueError:  # another Windows drive
        return False


def appimage_release():
    """The AppImage's folder ($APPDIR) when setup runs from one, else None. Its mount point changes on
    every start (/tmp/.mount_XXXXXX) and the game runs from the data folder, so paths inside it are stored
    relative to $APPDIR, which the runtime reads again (guestmods::release_root)."""
    image, appdir = os.environ.get("APPIMAGE"), os.environ.get("APPDIR")
    if not image or not appdir or not os.path.isdir(appdir):
        return None
    appdir = os.path.realpath(appdir)
    return appdir if path_inside(appdir, os.path.realpath(PKG)) else None


def guest_stored_path(path, data_dir):
    """How guest-sdk.json stores an absolute path: "$RELEASE/..." inside an AppImage, relative to the data
    folder inside a portable release, else as it is."""
    if not os.path.isabs(path):
        return path
    release = appimage_release()
    if release and path_inside(release, os.path.realpath(path)):
        return GUEST_RELEASE_MARKER + os.path.relpath(os.path.realpath(path), release).replace(os.sep, "/")
    if PORTABLE:
        for root in (PKG, data_dir):
            if path_inside(root, path):
                relative = os.path.relpath(path, data_dir)
                return relative if relative.startswith(".") else "." + os.sep + relative
    return path


def guest_release_paths(data_dir):
    """The guest-sdk.json entries for this release's own tools."""
    return {"builder": guest_stored_path(os.path.join(PKG, "tools", "guestmod", "build_guest_mod.py"), data_dir),
            "include": guest_stored_path(os.path.join(PKG, "sdk", "include"), data_dir),
            "setup": guest_stored_path(os.path.join(PKG, "tools", "installer", "setup.py"), data_dir)}


def write_guest_build_config(data_dir, tc):
    """Remember setup's real local toolchain for runtime guest builds (no shell command strings)."""
    def stored_path(path):
        return guest_stored_path(path, data_dir)

    config = {"format_version": 3, "python": [stored_path(ensure_setup_python(data_dir))],
              "compiler": [stored_path(tc.cc[0])] + tc.cc[1:],
              "data_dir": stored_path(os.path.abspath(data_dir))}
    config.update(guest_release_paths(data_dir))
    if tc.env and tc.env.get("ZIG_GLOBAL_CACHE_DIR"):
        config["zig_cache"] = stored_path(tc.env["ZIG_GLOBAL_CACHE_DIR"])
    path = os.path.join(data_dir, "guest-sdk.json")
    with open(path + ".tmp", "w", encoding="utf-8") as f:
        json.dump(config, f)
    os.replace(path + ".tmp", path)


def game_icon_png(data_dir):
    """The game's own icon (game/meta/iconTex.tga, uncompressed 32-bit) as PNG bytes, or None."""
    try:
        with open(os.path.join(data_dir, "game", "meta", "iconTex.tga"), "rb") as f:
            d = f.read()
    except OSError:
        return None
    if len(d) < 18 or d[1] != 0 or d[2] != 2 or d[16] != 32:
        return None
    w, h = struct.unpack("<HH", d[12:16])
    start = 18 + d[0]
    if not w or not h or len(d) < start + w * h * 4:
        return None
    rows = []
    for y in range(h):  # BGRA, bottom row first unless the descriptor says top-down
        src = y if d[17] & 0x20 else h - 1 - y
        row = bytearray(d[start + src * w * 4:start + (src + 1) * w * 4])
        row[0::4], row[2::4] = row[2::4], row[0::4]  # -> RGBA
        rows.append(b"\0" + bytes(row))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


def write_game_icon(data_dir, ico):
    """wwhd.ico (a PNG-compressed icon) or wwhd.png in the data folder, from the game's own icon."""
    png = game_icon_png(data_dir)
    if not png:
        return None
    path = os.path.join(data_dir, "wwhd.ico" if ico else "wwhd.png")
    with open(path, "wb") as f:
        if ico:
            w, h = struct.unpack(">II", png[16:24])
            f.write(struct.pack("<HHH", 0, 1, 1)
                    + struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(png), 22) + png)
        else:
            f.write(png)
    return path


def linux_launchers(data_dir, exe):
    play = os.path.join(data_dir, "play.sh")
    with open(play, "w") as f:
        f.write('#!/bin/sh\n# written by the Wind Waker HD setup\ncd "%s" || exit 1\nexec "%s" --game game --save save "$@"\n'
                % (data_dir, exe))
    os.chmod(play, 0o755)
    apps = os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share"), "applications")
    os.makedirs(apps, exist_ok=True)
    with open(os.path.join(apps, "wwhd.desktop"), "w") as f:
        f.write("[Desktop Entry]\nType=Application\nName=%s\nComment=The Wind Waker HD, native PC port\n"
                "Exec=\"%s\"\nPath=%s\nTerminal=false\nCategories=Game;\n" % (APP_NAME, play, data_dir))
        icon = write_game_icon(data_dir, ico=False)
        if icon:
            f.write("Icon=%s\n" % icon)
    return play


def windows_shortcuts(data_dir, exe):
    """Start menu and Desktop shortcuts to the built game (a failure is logged, not fatal)."""
    icon = write_game_icon(data_dir, ico=True)
    for csidl in (0x02, 0x10):  # the Start menu's Programs, the Desktop
        try:
            win_shortcut(os.path.join(win_known_folder(csidl), APP_NAME + ".lnk"), exe, "--game game --save save",
                         data_dir, icon or "")
        except (OSError, AttributeError, ValueError) as e:
            LOG.write("shortcut not created (folder 0x%x): %s" % (csidl, e))


def launch(state, data_dir, decomp=None):
    """decomp: start the decomp version (docs/decomp-build.md); None: as chosen in setup (start_decomp)"""
    say("")
    if decomp is None:
        decomp = bool(state.get("start_decomp"))
    exe = state["exe"]
    if decomp and state.get("decomp_exe") and os.path.isfile(state["decomp_exe"]):
        exe = state["decomp_exe"]
        say("Starting the decomp version of the game...")
    else:
        say("Starting the game...")
        if IS_MAC and state.get("app") and os.path.isdir(state["app"]):
            subprocess.Popen(["open", state["app"]])
            return
    args = [exe, "--game", state.get("game_dir") or os.path.join(data_dir, "game"), "--save", os.path.join(data_dir, "save")]
    if IS_WIN:
        subprocess.Popen(args, cwd=data_dir, creationflags=0x00000008 | 0x00000200)  # DETACHED_PROCESS | NEW_GROUP
    else:
        subprocess.Popen(args, cwd=data_dir, start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# ---------------------------------------------------------------------------------------------
# portable release: the launcher, an optional shortcut, the compiler download


LAUNCHER = {"darwin": "Wind Waker HD.app", "win32": "Wind Waker HD.exe"}.get(sys.platform, "wind-waker-hd")


def create_shortcut():
    """Optional (portable release): a shortcut to the release folder's launcher. Returns its path."""
    target = os.path.join(PKG, LAUNCHER)
    if IS_MAC:
        apps = os.path.expanduser("~/Applications")
        os.makedirs(apps, exist_ok=True)
        link = os.path.join(apps, APP_NAME + ".app")
        if os.path.islink(link):
            os.remove(link)
        if os.path.exists(link):
            raise SetupError("%s already exists" % link)
        os.symlink(target, link)
    elif IS_LINUX:
        apps = os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share"), "applications")
        os.makedirs(apps, exist_ok=True)
        link = os.path.join(apps, "wwhd.desktop")
        with open(link, "w") as f:
            f.write("[Desktop Entry]\nType=Application\nName=%s\nExec=\"%s\"\nPath=%s\nTerminal=false\nCategories=Game;\n"
                    "Actions=setup;\n\n[Desktop Action setup]\nName=Setup (repair, update, change game)\n"
                    "Exec=\"%s\" --setup\n" % (APP_NAME, target, PKG, target))
    else:
        link = os.path.join(os.environ.get("APPDATA", ""), "Microsoft", "Windows", "Start Menu", "Programs", APP_NAME + ".lnk")
        try:
            win_shortcut(link, target, workdir=PKG)
        except (OSError, AttributeError, ValueError) as e:
            LOG.write("shortcut not created: %s" % e)
    say("  Shortcut: %s" % link)
    return link


def toolchain_dir(data_dir):
    return os.path.join(data_dir, "toolchain")


def remove_toolchain(data_dir):
    """Deletes the downloaded compiler (repair and guest mod builds need setup to restore it)."""
    d = toolchain_dir(data_dir)
    n = folder_size(d) if os.path.isdir(d) else 0
    shutil.rmtree(longpath(d), ignore_errors=True)
    say("  Removed the downloaded compiler (%s)" % human(n))
    return n


# ---------------------------------------------------------------------------------------------
# saves


def save_user_dir(data_dir):
    return os.path.join(data_dir, "save", "user")


def have_save(data_dir):
    user = save_user_dir(data_dir)
    return os.path.isfile(os.path.join(user, "cking.sav"))


def import_save(kind, path, data_dir, replace=False):
    """Copies an HD save (a folder with cking.sav, or cking.sav itself) or converts a GameCube save
    (.gci, tools/savegame/gc2hd.py) into save/user/. An existing save is only replaced when asked,
    and then first moved to save/user.backup-<time>. Returns a message."""
    user = save_user_dir(data_dir)
    if have_save(data_dir) and not replace:
        raise SetupError("a save already exists in %s" % user)
    tmp = os.path.join(data_dir, "save-import.tmp")
    shutil.rmtree(longpath(tmp), ignore_errors=True)
    os.makedirs(tmp)
    try:
        if kind == "gc":
            if not os.path.isfile(path) or not path.lower().endswith(".gci"):
                raise SetupError("choose a GameCube save file (.gci)")
            out = run_logged([sys.executable, os.path.join(PKG, "tools", "savegame", "gc2hd.py"), path, "-o", tmp],
                             what="converting the GameCube save")
            if not os.path.isfile(os.path.join(tmp, "cking.sav")):
                raise SetupError("the GameCube save could not be converted:\n" + out.strip()[-500:])
        else:
            src = os.path.dirname(path) if os.path.isfile(path) else path
            savs = [f for f in os.listdir(src) if f.endswith(".sav")] if os.path.isdir(src) else []
            if "cking.sav" not in savs:
                raise SetupError("no cking.sav in %s" % src)
            for f in savs:
                shutil.copy2(os.path.join(src, f), os.path.join(tmp, f))
        backup = None
        if os.path.isdir(user) and os.listdir(user):
            backup = user + ".backup-" + time.strftime("%Y%m%d-%H%M%S")
            os.replace(longpath(user), longpath(backup))
        os.makedirs(os.path.dirname(user), exist_ok=True)
        os.replace(longpath(tmp), longpath(user))
    finally:
        shutil.rmtree(longpath(tmp), ignore_errors=True)
    n = len([f for f in os.listdir(user) if f.endswith(".sav")])
    msg = "Copied %d save file%s to %s" % (n, "" if n == 1 else "s", user)
    if kind == "gc":
        msg = "Converted the GameCube save to %s" % os.path.join(user, "cking.sav")
    if backup:
        msg += " (the previous save is in %s)" % backup
    say("  " + msg)
    return msg


def legacy_appimage_data_notice(image=None):
    """v0.2.13's AppImage kept a portable setup in "<image>.data"; later ones use the runtime's standard
    "<image>.home" and never read ".data" (issue #55). The hint when one is still there, else "".
    Same text as AppRun and runtime/src/platform/appimage_notice.h."""
    image = os.environ.get("APPIMAGE") if image is None else image
    if not image or not os.path.isdir(image + ".data"):
        return ""
    name = os.path.basename(image)
    return ("Found %s.data: this folder is no longer used. Move its contents to %s.home (see the changelog) "
            "to keep your saves and settings." % (name, name))


def legacy_config_dirs():
    """Settings folders the game used before portable releases (and still uses in source builds)."""
    if IS_MAC:
        return [os.path.expanduser("~/Library/Application Support/WWHD"),
                os.path.expanduser("~/Library/Application Support/wwhd")]
    if IS_WIN:
        return [os.path.join(os.environ.get("APPDATA", ""), "WWHD")]
    return [os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config"), "wwhd")]


SETTINGS_ITEMS = ["controls.json", "settings.ini", "display.plist", "states", "shadercache"]


def import_sources(path=None):
    """(save folder or None, [(source, name)] settings items) of an earlier installation (path=None)
    or of another release folder."""
    items = []
    if path:
        root = os.path.join(path, "data") if os.path.isdir(os.path.join(path, "data")) else path
        save = os.path.join(root, "save", "user")
        cfg = [os.path.join(root, "user")]
    else:
        save = os.path.join(legacy_data_dir(), "save", "user")
        cfg = legacy_config_dirs()
    seen = set()
    for d in cfg:
        for name in SETTINGS_ITEMS + ["shaders.bin", "graphics.plist"]:
            src = os.path.join(d, name)
            if name not in seen and os.path.exists(src):
                seen.add(name)
                items.append((src, name))
    if not path and IS_MAC and "shaders.bin" not in seen and os.path.isfile(os.path.expanduser("~/Library/Caches/wwhd/shaders.bin")):
        items.append((os.path.expanduser("~/Library/Caches/wwhd/shaders.bin"), "shaders.bin"))
    has_save = os.path.isfile(os.path.join(save, "cking.sav"))
    return (save if has_save else None), items


def import_existing(data_dir, path=None, replace=False):
    """Copies (never moves) saves and settings of an earlier installation or another release folder."""
    if path and os.path.realpath(path) in (os.path.realpath(PKG), os.path.realpath(data_dir)):
        raise SetupError("that is this folder")
    save, items = import_sources(path)
    if not save and not items:
        raise SetupError("no saves or settings found there")
    msgs = []
    if save:
        msgs.append(import_save("hd", save, data_dir, replace))
    user = os.path.join(data_dir, "user")
    os.makedirs(user, exist_ok=True)
    copied = []
    for src, name in items:
        dst = os.path.join(user, name)
        if os.path.exists(dst):
            continue  # this folder's own settings win
        if os.path.isdir(src):
            shutil.copytree(src, dst)
        else:
            shutil.copy2(src, dst)
        copied.append(name)
    if copied:
        msgs.append("Copied settings: %s" % ", ".join(copied))
        say("  Copied settings to %s: %s" % (user, ", ".join(copied)))
    return "; ".join(msgs)


def maybe_import_save(ui, data_dir):
    """Offers to copy an existing save into the port's save folder; never overwrites without asking."""
    user = save_user_dir(data_dir)
    if not ui.interactive:
        return
    if have_save(data_dir):
        say("  Your existing save in %s is kept." % user)
        return
    options = ["no, start with a new save",
               "a Wind Waker HD save (a folder with cking.sav, e.g. from Cemu or a Wii U)",
               "a GameCube Wind Waker save (.gci, converted to HD)",
               "copy saves and settings from another Wind Waker HD folder"]
    legacy = PORTABLE and any(import_sources())
    if legacy:
        options.append("copy saves and settings from my earlier installation (copied, not moved)")
    i = ui.choose("Do you want to use an existing save?", options)
    if i == 0:
        return
    try:
        if i == 3:
            import_existing(data_dir, ui.pick_path("Choose the other Wind Waker HD folder", folder=True))
        elif i == 4:
            import_existing(data_dir)
        elif i == 1:
            import_save("hd", ui.pick_path("Choose the folder that contains cking.sav", folder=True), data_dir)
        else:
            import_save("gc", ui.pick_path("Choose the GameCube save (.gci)",
                                           filetypes=[("GameCube save (*.gci)", "*.gci")]), data_dir)
    except SetupError as e:
        say("  Save not imported: %s (you can copy it to %s later)" % (e, user))


# ---------------------------------------------------------------------------------------------
# main flow


def load_manifest():
    p = os.path.join(PKG, "sdk", "manifest.json")
    if not os.path.isfile(p):
        raise SetupError("sdk/manifest.json is missing: run setup from the unpacked release folder")
    with open(p) as f:
        m = json.load(f)
    plat = {"darwin": "macos", "win32": "windows"}.get(sys.platform, "linux")
    if not m["platform"].startswith(plat):
        raise SetupError("this release is for %s; download the one for your system" % m["platform"])
    arch = m["platform"].split("-", 1)[1] if "-" in m["platform"] else ""
    if IS_LINUX and arch and normalize_arch(arch) != host_arch():
        raise SetupError("this release is for %s, but this computer is %s: download the %s release instead"
                         % (m["platform"], host_arch(), "linux-" + host_arch()))
    # Windows: the x86-64 release also runs on ARM64 (emulated); the ARM64 one runs only there
    if plat == "windows" and arch and normalize_arch(arch) == "aarch64" and host_arch() != "aarch64":
        raise SetupError("this release is for Windows on ARM64, but this computer is %s: download the "
                         "windows-x86_64 release instead" % host_arch())
    if (m.get("variant") == "decomp") != bool(decomp_hooks_file()):
        raise SetupError("sdk/manifest.json and sdk/decomp_hooks.txt do not match (decomp build): unpack the release again")
    return m


def normalize_arch(a):
    a = a.lower()
    return {"amd64": "x86_64", "x64": "x86_64", "arm64": "aarch64"}.get(a, a)


def host_arch():
    """x86_64 or aarch64 (uname -m), which picks the Linux release, its pinned zig and Python."""
    import platform
    return normalize_arch(platform.machine())


def read_state(data_dir):
    try:
        with open(os.path.join(data_dir, "install.json")) as f:
            return resolved_state(json.load(f), data_dir)
    except (OSError, ValueError):
        return {}


def rel_to_data(path, data_dir):
    """Portable release: paths inside the data folder are stored relative to it, so the release folder
    can be moved or renamed; paths outside (an extracted game folder used in place) stay absolute."""
    if not PORTABLE or not path:
        return path
    try:
        rel = os.path.relpath(path, data_dir)
    except ValueError:  # another drive (Windows)
        return path
    return path if rel.startswith("..") else rel.replace(os.sep, "/")


def abs_from_data(path, data_dir):
    return os.path.normpath(path if not path or os.path.isabs(path) else os.path.join(data_dir, path))


def resolved_state(state, data_dir):
    st = dict(state)
    for k in ("exe", "game_dir", "decomp_exe"):
        if st.get(k):
            st[k] = abs_from_data(st[k], data_dir)
    return st


def write_state(data_dir, state):
    st = dict(state)
    for k in ("exe", "game_dir", "data_dir", "decomp_exe"):
        if st.get(k):
            st[k] = rel_to_data(st[k], data_dir)
    if PORTABLE:
        st.pop("data_dir", None)  # it is where install.json is
    with open(os.path.join(data_dir, "install.json"), "w") as f:
        json.dump(st, f, indent=1)


class Ctx:
    """What one setup run works with (paths, the release, the options)."""

    def __init__(self, args):
        self.args = args
        self.manifest = load_manifest()
        self.version = self.manifest["version"]
        self.data_dir = os.path.abspath(args.data_dir or default_data_dir())
        self.app_dir = os.path.abspath(args.app_dir or os.path.expanduser("~/Applications"))
        self.exe_dir = os.path.join(self.data_dir, "bin")
        self.exe = os.path.join(self.exe_dir, self.manifest["exe"])
        os.makedirs(self.data_dir, exist_ok=True)
        LOG.open(os.path.join(self.data_dir, "setup.log"))
        problem = path_length_problem(self.data_dir, PKG)
        if problem:  # before anything is downloaded or built (issue #144)
            LOG.write("data folder %s: %s" % (self.data_dir, problem))
            raise SetupError(problem)
        self.game_dir = self.state().get("game_dir") or os.path.join(self.data_dir, "game")

    def state(self):
        return read_state(self.data_dir)

    def installed(self):
        st = self.state()
        return bool(st) and os.path.isfile(st.get("exe", "")) and valid_game_folder(self.game_dir)


STEP_TITLES = {
    "keys": "Checking the disc image and keys",
    "archive": "Checking the Cemu archive",
    "folder": "Checking the game folder",
    "compiler": "Getting the compiler",
    "extract": "Extracting the game files",
    "copy": "Copying the game files",
    "translate": "Translating the game code to C",
    "compile": "Compiling the game code (the longest step: several minutes)",
    "app": "Building the game",
    "shaders": "Preparing shaders",
    "decomp": "Building the decomp version (experimental)",
}


def plan_steps(kind, decomp=False):
    result = {"image": ["keys", "compiler", "extract", "translate", "compile", "app"],
            "archive": ["archive", "compiler", "extract", "translate", "compile", "app"],
            "folder": ["folder", "compiler"] + ([] if PORTABLE else ["copy"]) + ["translate", "compile", "app"],
            "installed": ["compiler", "translate", "compile", "app"],
            "gen": ["compiler", "compile", "app"]}[kind]
    return result + ([] if kind == "gen" else ["shaders"]) + (["decomp"] if decomp else [])


def decomp_offered(manifest):
    return decomp_setup.offered(manifest, IS_MAC)


def decomp_wanted(ctx):
    """The decomp version (decomp_setup.py): --decomp / --no-decomp (or the setup window's choice), else
    what the last setup did. Never where it is not offered."""
    if not decomp_offered(ctx.manifest):
        return False
    choice = getattr(ctx.args, "decomp", None)
    return bool(ctx.state().get("decomp", False) if choice is None else choice)


CODE_MODS_TEXT = (
    "For mods in the catalogue or Mods tab marked as code mods. Built-in mods, graphics packs and "
    "content mods do not need this. The game code gets a small check in every function, which can "
    "cost some performance, and the build takes a bit longer. You can switch this later in "
    "Settings > Mods; that requires another rebuild.")


def setup_code_mods(ctx, ui=None):
    explicit = getattr(ctx.args, "code_mods", None)
    if explicit is not None or "WWHD_CODE_MODS" in os.environ:
        return code_mods.hooks_option(explicit)
    try:
        active = json.loads(Path(ctx.data_dir, "code-mods-active.json").read_text(encoding="utf-8"))
        remembered = active["hooks"] if active.get("state") == "ready" else None
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        remembered = None
    default = remembered if isinstance(remembered, bool) else bool(ctx.state().get("code_mods", False))
    if ui and ui.interactive:
        say(CODE_MODS_TEXT)
        return ui.yesno("Build with code-mod support?", default)
    return default


def write_code_mods_setting(data_dir, hooks):
    """Use the same stores and overrides as the AppKit and SDL hosts."""
    import plistlib
    portable = Path(data_dir, "user")
    if IS_MAC:
        default = portable / "display.plist" if PORTABLE else Path(os.path.expanduser(
            "~/Library/Application Support/wwhd/display.plist"))
        path = Path(os.environ.get("WWHD_DISPLAY_SETTINGS", str(default)))
        values = plistlib.loads(path.read_bytes()) if path.exists() else {}
        values["code-mods"] = "1" if hooks else "0"
        payload = plistlib.dumps(values)
    else:
        default = (portable if PORTABLE else Path(legacy_config_dirs()[0])) / "settings.ini"
        path = Path(os.environ.get("WWHD_SETTINGS", str(default)))
        lines = path.read_text(encoding="utf-8").splitlines() if path.exists() else []
        lines = [line for line in lines if not line.startswith("code-mods=")]
        payload = ("\n".join(lines + ["code-mods=" + ("1" if hooks else "0")]) + "\n").encode()
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(payload)
    os.replace(tmp, path)


def shader_progress_callback(progress):
    """progress(text[, fraction]) for shader_prebuild.prepare, shown on a Progress line.

    fraction covers the whole preparation (0..1); None until the helpers report one, so the bar
    is indeterminate until then. The log gets each phase once, not every update."""
    last = {"phase": None}
    def message(text, fraction=None):
        if fraction is None:
            progress.update(0, 0, text, force=True)
        else:
            done = int(round(1000 * fraction))
            if GUI:
                # the bar shows this text instead of its percentage; keep it short enough for
                # the bar: "<phase>: 1,234 of 5,678 (42%)" (the terminal line keeps the units)
                counted = re.match(r"(.*?: [\d,]+ of [\d,]+)( |$)", text)
                progress.update(done, 1000, "%s (%d%%)" % (counted.group(1) if counted else text, done // 10))
            else:
                progress.update(done, 1000, text)
        phase = text.split(":", 1)[0]
        if phase != last["phase"]:
            last["phase"] = phase
            LOG.write(text)
    return message


def prepare_shaders_nonfatal(*args, **kwargs):
    """Shader preparation is optional, including unexpected helper failures."""
    try:
        import shader_prebuild
        return shader_prebuild.prepare(*args, **kwargs)
    except Exception as error:
        message = "Shader preparation unavailable: %s. The game can still run." % error
        try:
            LOG.write(message)
        except Exception:
            pass  # A broken log must not turn optional preparation into setup failure.
        return dict(status="failed", message=str(error))


def install(ctx, source, keys=None, info=None, ui=None, check_keys=None):
    """The whole build for one source: ("image", path) | ("archive", path) | ("folder", path) |
    ("installed", game_dir) | ("gen", generated C). check_keys(image) -> (keys, info) is called for an image without keys.
    Returns the new install state."""
    ui = ui or UI(False)
    args, manifest, data_dir = ctx.args, ctx.manifest, ctx.data_dir
    args.code_mods = "1" if setup_code_mods(ctx, ui) else "0"
    kind = source[0]
    want_decomp = kind != "gen" and decomp_wanted(ctx)
    old_state = ctx.state()
    steps = plan_steps(kind, want_decomp)
    total = len(steps)
    counter = iter(range(1, total + 1))

    def begin(sid):
        step(next(counter), total, STEP_TITLES[sid], sid)

    archive_title = None

    if kind == "image":
        begin("keys")
        if not os.path.isfile(source[1]):
            raise SetupError("disc image not found: %s" % source[1])
        if keys is None or info is None:
            keys, info = check_keys(source[1])
        check_title(info.get("title_id", ""))
        say("  OK: %s, %s files, %s" % (title_desc(info.get("title_id", "")), info.get("files"),
                                        human(int(info.get("bytes", 0)))))
    elif kind == "archive":
        begin("archive")
        if not os.path.isfile(source[1]):
            raise SetupError("Cemu archive not found: %s" % source[1])
        problem, err, info = archive_info(source[1])
        if problem and problem != "wrong_title":
            raise SetupError("cannot read the Cemu archive: %s" % err)
        archive_title, notes = archive_choice(info)
        say("  Using %s from the archive (folder %s): %s files, %s. A Cemu archive needs no keys." %
            (title_desc(info["title_id"], int(info["version"])), archive_title, info.get("files"),
             human(int(info.get("bytes", 0)))))
        for n in notes:
            say("  " + n)
    elif kind == "folder":
        begin("folder")
        if not valid_game_folder(source[1]):
            raise SetupError("%s is not an extracted game folder (needs code/cking.rpx, content/, meta/meta.xml)" % source[1])
        tid = game_folder_title(source[1])
        if tid:
            check_title(tid)
        build = check_game_version(source[1])
        say("  OK: %s (%s, version 0)" % (source[1], build.name))
    elif kind == "installed":
        if not valid_game_folder(ctx.game_dir):
            raise SetupError("no installed game files in %s; run setup with your disc image" % ctx.game_dir)
        tid = game_folder_title(ctx.game_dir)
        if tid:
            check_title(tid)
        check_game_version(ctx.game_dir)

    begin("compiler")
    tc = get_toolchain(manifest["toolchain"], data_dir, ui)
    say("  Using %s." % tc.desc)

    game_dir = ctx.game_dir
    if kind in ("image", "archive"):
        begin("extract")
        t0 = time.time()
        extract_game(source[1], keys, info, data_dir, archive_title if kind == "archive" else None)
        game_dir = os.path.join(data_dir, "game")
        say("  Game files are in %s (%d s)" % (game_dir, time.time() - t0))
    elif kind == "folder":
        if PORTABLE:  # use the extracted game where it is: no copy
            game_dir = os.path.abspath(source[1])
            say("  Using the game files in %s (not copied)" % game_dir)
        else:
            begin("copy")
            t0 = time.time()
            copy_game_folder(source[1], data_dir)
            game_dir = os.path.join(data_dir, "game")
            say("  Game files are in %s (%d s)" % (game_dir, time.time() - t0))
    ctx.game_dir = game_dir
    keys = None

    work = os.path.join(data_dir, "work")
    shutil.rmtree(longpath(work), ignore_errors=True)
    os.makedirs(work)
    if free_space(data_dir) < (2 << 30):
        raise SetupError("not enough free disk space in %s for building (2 GB needed)" % data_dir)
    if kind == "gen":
        gen_dir = source[1]
    else:
        begin("translate")
        t0 = time.time()
        gen_dir = os.path.join(work, "gen")
        nfiles = recompile(ctx.game_dir, gen_dir, hooks=code_mods.hooks_option(getattr(args, "code_mods", None)))
        say("  %d source files (%d s)" % (nfiles, time.time() - t0))

    begin("compile")
    jobs = args.jobs or default_jobs()
    t0 = time.time()
    objs = compile_gamecode(tc, manifest, gen_dir, os.path.join(work, "obj"), jobs)
    say("  compiled in %d s" % (time.time() - t0))

    begin("app")
    say("  Linking...")
    exe, exe_dir = ctx.exe, ctx.exe_dir
    os.makedirs(exe_dir, exist_ok=True)
    new_exe = exe + ".new" + EXE_SUFFIX
    link_game(tc, manifest, objs, work, new_exe)
    for rf in manifest.get("runtime_files", []):
        # the data only (copy2 would also copy a downloaded file's "mark of the web" on Windows)
        shutil.copyfile(os.path.join(PKG, "sdk", "runtime", rf), os.path.join(exe_dir, rf))
        shutil.copymode(os.path.join(PKG, "sdk", "runtime", rf), os.path.join(exe_dir, rf))
    os.replace(new_exe, exe)
    say("  Built %s" % exe)

    state = {"version": ctx.version, "platform": manifest["platform"], "exe": exe, "data_dir": data_dir,
             "game_dir": ctx.game_dir, "portable": PORTABLE,
             "installed": time.strftime("%Y-%m-%d %H:%M:%S"), "toolchain": manifest["toolchain"],
             "placeholder_code": kind == "gen",
             "code_mods": code_mods.hooks_option(getattr(args, "code_mods", None))}
    if manifest.get("variant"):
        state["variant"] = manifest["variant"]  # "decomp": the decomp build (docs/decomp-build.md)
    if os.environ.get("APPIMAGE"):
        # an AppImage (issue #55): its mount is read-only, so nothing is written beside it and
        # portable.txt is not created. Record which image this was installed from: install.json is
        # the "what was prepared" file and the setup window reports it back in its hello message.
        state["appimage"] = os.environ["APPIMAGE"]
    if PORTABLE:
        # the game keeps its settings, save states and caches in data/user (runtime: host::portable_user_dir)
        with open(os.path.join(exe_dir, "portable.txt"), "w") as f:
            f.write("Portable mode: this game keeps its settings, controls, save states and shader caches in\n"
                    "../user (next to this folder) instead of your user folders. Delete this file to use those.\n")
        os.makedirs(os.path.join(data_dir, "user"), exist_ok=True)
        if args.shortcuts and kind != "gen":
            state["shortcut"] = create_shortcut()
    elif not args.no_shortcuts and kind != "gen":
        if IS_MAC:
            app = os.path.join(ctx.app_dir, APP_NAME + ".app")
            mac_app(app, exe, data_dir, ctx.version, manifest.get("runtime_files", []))
            os.remove(exe)  # the app holds the game binary (and its runtime files)
            for rf in manifest.get("runtime_files", []):
                os.remove(os.path.join(exe_dir, rf))
            if not os.listdir(exe_dir):
                os.rmdir(exe_dir)
            state["app"] = app
            state["exe"] = os.path.join(app, "Contents", "MacOS", "wwhd")
            say("  App: %s" % app)
        elif IS_LINUX:
            state["launcher"] = linux_launchers(data_dir, exe)
            say("  Launcher: %s (also in your applications menu)" % state["launcher"])
        else:
            windows_shortcuts(data_dir, exe)
            say("  Shortcuts: Start menu and desktop (\"%s\")" % APP_NAME)
    if kind != "gen":
        try:
            code_mods.remember_installed(sys.modules[__name__], ctx, tc, state["code_mods"], state["exe"], gen_dir, objs)
        except (OSError, SetupError) as e:
            LOG.write("Initial code-mod cache skipped: " + str(e))
            # Never leave an older variant selected after a successful setup build.
            Path(data_dir, "code-mods-active.json").unlink(missing_ok=True)
        write_code_mods_setting(data_dir, state["code_mods"])
    if kind != "gen":
        begin("shaders")
        import shader_prebuild
        shader_template = Path(getattr(args, "shader_template", None) or os.environ.get("WWHD_SHADER_TEMPLATE") or Path(PKG) / "tools/shadercache/template.bin").expanduser().resolve()
        shader_root = shader_prebuild.default_cache_root(data_dir, PORTABLE)
        try:
            shader_root.mkdir(parents=True, exist_ok=True)
            shader_prebuild.atomic_json(shader_root / "launcher.json", {
                "schema": 1, "python": sys.executable, "package": str(PKG),
                "helper": str(Path(PKG) / "tools/installer/shader_prebuild.py"),
                "template": str(shader_template)})
        except OSError as error:
            say("Shader preparation launcher unavailable: %s" % error)
        shader_progress = Progress("Preparing shaders")
        state["shader_prebuild"] = prepare_shaders_nonfatal(
            ctx.game_dir, shader_template, state["exe"],
            shader_root, PKG,
            renderer="metal" if IS_MAC else "vulkan", progress=shader_progress_callback(shader_progress), log=LOG.write,
            # macOS: the player chooses Metal or Vulkan (MoltenVK) in Graphics > Renderer: prepare both
            renderers=["metal", "vulkan"] if IS_MAC else ["vulkan"],
            skip=bool(getattr(args, "skip_shader_prebuild", False)),
            cancel=lambda: bool(getattr(args, "shader_prebuild_cancel", None) and
                                Path(args.shader_prebuild_cancel).exists()))
        prepared = state["shader_prebuild"]
        states = prepared.get("renderers") or {}
        ready = [name.capitalize() for name, value in sorted(states.items()) if value == "ready"]
        # macOS: "ready (Metal, Vulkan)", or "ready (Metal)" when MoltenVK is unavailable
        shader_progress.done(prepared["status"] + (" (%s)" % ", ".join(ready) if len(states) > 1 and ready else ""))
    if not args.keep_work:
        shutil.rmtree(longpath(work), ignore_errors=True)
    if kind != "gen":
        os.makedirs(os.path.join(data_dir, "save"), exist_ok=True)
    write_guest_build_config(data_dir, tc)
    if decomp_offered(manifest) and kind != "gen":
        state["decomp"] = want_decomp  # remembered: a repair or update builds it again
        state["start_decomp"] = bool(old_state.get("start_decomp")) and want_decomp
    write_state(data_dir, state)  # the normal game is ready, whatever happens to the decomp version
    if want_decomp:
        begin("decomp")
        try:
            exe, rev, nunits = decomp_setup.build(sys.modules[__name__], ctx, tc, jobs)
            state.update(decomp_exe=exe, decomp_revision=rev)
            state.pop("decomp_error", None)
        except SetupError as e:
            # the normal game stays as it is: the decomp version is optional
            say("")
            say("  The decomp version could not be built (the normal game is ready and unaffected): %s" % e)
            LOG.write("decomp version failed: %s" % e)
            state["decomp_error"] = str(e).splitlines()[0][:300]
            state.pop("decomp_exe", None)
            stale = decomp_setup.exe_path(data_dir, PORTABLE)  # an earlier release's decomp version
            if os.path.isfile(stale):
                os.remove(stale)
            state["start_decomp"] = False
        write_state(data_dir, state)
    elif decomp_offered(manifest) and old_state.get("decomp_exe"):
        # switched off: the decomp version this setup built earlier goes
        old = old_state["decomp_exe"]
        if os.path.isfile(old) and old != state.get("exe"):
            os.remove(old)
    if kind != "gen" and state.get("code_mods") and state.get("exe"):
        install_story_notebook(state, data_dir)
    return state


def install_story_notebook(state, data_dir, runner=subprocess.run):
    """The story system's required package (runtime/src/mods/required.h): when the story system is on (the
    default), the game downloads the Story Notebook from the mod catalogue once, here. Offline is fine: the
    setup finishes and the Mods menu can install it later. Returns True when it is installed."""
    say("")
    say("Installing the story notebook (the story system's quest package) from the mod catalogue...")
    args = [state["exe"], "--game", state.get("game_dir") or os.path.join(data_dir, "game"), "--save", os.path.join(data_dir, "save"),
            "--install-story-notebook"]
    try:
        done = runner(args, cwd=data_dir, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=600)
        lines = [l for l in (done.stdout or "").splitlines() if l.startswith("{")]
        result = json.loads(lines[-1]) if lines else {"ok": False, "message": "no answer from the game"}
    except (OSError, ValueError, subprocess.TimeoutExpired) as e:
        result = {"ok": False, "message": str(e)}
    LOG.write("story notebook: %s" % result)
    if result.get("ok"):
        say("  The story notebook is installed.")
        return True
    say("  The story notebook couldn't be installed now (%s). The game works without it; install it later from the "
        "Mods menu (it needs the internet once)." % result.get("message", ""))
    return False


def run_language_source(args, ui, data_dir, path):
    """setup --language-source: the language packs of the player's European or Japanese game."""
    say("")
    say("Language source (experimental): %s" % path)
    kind = language_source_kind(path)
    keys = info = None
    if kind == "image":
        side_args = argparse.Namespace(disc_key=args.language_disc_key, common_key=args.common_key)
        keys, info = get_disc_keys(path, ui, side_args)
    game_dir = read_state(data_dir).get("game_dir") or os.path.join(data_dir, "game")
    manifests = add_language_source((kind, path), data_dir, keys, info, game_dir)
    for m in manifests:
        say("Added the %s languages: %s. Choose one in the game's settings (F1, Language); it applies on the next "
            "start.%s" % (LANGUAGE_REGION_NAMES[m["region"]], ", ".join(p["language"] for p in m["packs"]),
                          " This is untested with the Japanese game so far: please report what looks wrong."
                          if m["region"] == "JP" else ""))


def main():
    ap = argparse.ArgumentParser(description="Wind Waker HD setup", formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__)
    ap.add_argument("--image", help="disc image (.wux or .wud; a .wua is taken as --archive)")
    ap.add_argument("--archive", help="Cemu Wii U archive (.wua): no keys needed")
    ap.add_argument("--disc-key", help="disc key file or Cemu keys.txt (default: IMAGE.key)")
    ap.add_argument("--common-key", help="Wii U common key file or Cemu keys.txt (or WIIU_COMMON_KEY)")
    ap.add_argument("--game-dir", help="an already extracted game folder (code/, content/, meta/) instead of an image")
    ap.add_argument("--gen-dir", help="use this generated code instead of recompiling (build checks)")
    ap.add_argument("--data-dir", help="where the game is installed (default: %s)" % default_data_dir())
    ap.add_argument("--app-dir", help="macOS: where the app goes (default: ~/Applications)")
    ap.add_argument("--code-mods", choices=("0", "1"), help="build PowerPC mod support (new installs default off; skips the question; overrides WWHD_CODE_MODS)")
    ap.add_argument("--rebuild-code-mods", action="store_true", help="stage a cached game-code variant for the next restart")
    ap.add_argument("--code-mods-status", help="atomic rebuild progress/result JSON")
    ap.add_argument("--code-mods-cancel", help="cancel rebuild when this file exists")
    ap.add_argument("--decomp", dest="decomp", action="store_const", const=True, default=None,
                    help="development builds, macOS: also build the decomp version of the game (experimental; "
                         "docs/decomp-build.md); remembered for repairs and updates")
    ap.add_argument("--no-decomp", dest="decomp", action="store_const", const=False,
                    help="do not build the decomp version (any more)")
    ap.add_argument("--start-decomp", choices=("0", "1"),
                    help="start the decomp version instead of the normal game from now on (1) or not (0)")
    ap.add_argument("--repair-guest-python", action="store_true", help="upgrade local mod setup Python without rebuilding game code")
    ap.add_argument("--shader-template", help="local shader template (or WWHD_SHADER_TEMPLATE); optional")
    ap.add_argument("--skip-shader-prebuild", action="store_true", help="skip optional shader preparation; compile during play")
    ap.add_argument("--shader-prebuild-cancel", help="skip shader preparation when this file exists")
    ap.add_argument("--repair", action="store_true", help="rebuild the game code from the installed game files")
    ap.add_argument("--jobs", type=int, help="parallel compiler processes")
    ap.add_argument("--yes", action="store_true", help="non-interactive (also WWHD_SETUP_NONINTERACTIVE=1)")
    ap.add_argument("--no-launch", action="store_true", help="do not start the game at the end")
    ap.add_argument("--no-shortcuts", action="store_true", help="no app bundle / menu entries")
    ap.add_argument("--shortcuts", action="store_true",
                    help="portable release: also add a shortcut (Applications / Start menu / applications menu)")
    ap.add_argument("--keep-work", action="store_true", help="keep the generated code and objects")
    ap.add_argument("--language-source", metavar="PATH",
                    help="experimental: take the language files (text, fonts, 2D layouts) of your European or "
                         "Japanese game (.wux/.wud, .wua or extracted folder) into the installation; the game is "
                         "still built from the USA game")
    ap.add_argument("--language-disc-key", metavar="FILE", help="the disc key of the --language-source disc image "
                    "(default: its IMAGE.key)")
    ap.add_argument("--remove-language-source", metavar="EU|JP", help="remove a language source")
    ap.add_argument("--gui-protocol", action="store_true",
                    help="machine interface for the graphical installer (JSON lines; see tools/installer/README.md)")
    args = ap.parse_args()
    try:
        sys.stdout.reconfigure(errors="replace")
    except AttributeError:
        pass
    if args.gui_protocol:
        return gui_main(args)
    interactive = not (args.yes or os.environ.get("WWHD_SETUP_NONINTERACTIVE")) and sys.stdin.isatty()
    ui = UI(interactive)
    try:
        rc = run(args, ui)
    except SetupError as e:
        say("")
        say("Setup stopped: %s" % e)
        if LOG.f:
            say("Details are in %s" % LOG.f.name)
        rc = 1
    except KeyboardInterrupt:
        say("\nSetup cancelled. Run it again to continue; nothing that was finished is lost.")
        rc = 130
    if interactive and (IS_WIN or IS_MAC):
        try:
            input("\nPress Enter to close this window.")
        except (EOFError, KeyboardInterrupt):
            pass
    return rc


def run(args, ui):
    ctx = Ctx(args)
    if getattr(args, "repair_guest_python", False):
        repair_guest_python(ctx.data_dir)
        return 0
    if getattr(args, "rebuild_code_mods", False):
        if not valid_game_folder(ctx.game_dir):
            raise SetupError("No installed game files to rebuild from; run setup first")
        code_mods.rebuild(sys.modules[__name__], ctx, code_mods.hooks_option(args.code_mods),
                          args.code_mods_status, args.code_mods_cancel)
        return 0
    version, data_dir, game_dir = ctx.version, ctx.data_dir, ctx.game_dir
    say("The Legend of Zelda: The Wind Waker HD - native PC port, setup %s" % version)
    hint = "" if GUI else legacy_appimage_data_notice()  # the setup window shows it itself
    if hint:
        say("")
        say(hint)
        say("")
    say("This release contains no game files. Setup builds the game from your own dump of the game.")
    say("Install folder: %s" % data_dir)
    state = ctx.state()
    installed = ctx.installed()
    if getattr(args, "decomp", None) and not decomp_offered(ctx.manifest):
        raise SetupError("the decomp version is offered on macOS in development builds only (docs/decomp-build.md)")
    if getattr(args, "start_decomp", None) is not None:
        on = args.start_decomp == "1"
        if on and not (state.get("decomp_exe") and os.path.isfile(state["decomp_exe"])):
            raise SetupError("the decomp version is not built: run setup with --decomp first")
        state["start_decomp"] = on
        write_state(ctx.data_dir, state)
        say("From now on the game starts %s." % ("the decomp version" if on else "the normal version"))
        if args.decomp is None:
            return 0
    # --decomp / --no-decomp on an installed game: a repair that builds (or drops) the decomp version
    decomp_change = (installed and getattr(args, "decomp", None) is not None
                     and bool(args.decomp) != bool(state.get("decomp_exe")))
    if args.remove_language_source:
        say("Removed %s" % remove_language_source(data_dir, args.remove_language_source))
        return 0
    if args.language_source:
        run_language_source(args, ui, data_dir, os.path.abspath(clean_path(args.language_source)))
        return 0

    source = None  # ("image", path) | ("archive", path) | ("folder", path) | ("installed", game_dir) | ("gen", dir)
    if args.gen_dir:
        source = ("gen", os.path.abspath(args.gen_dir))
    elif args.archive or (args.image and args.image.lower().endswith(".wua")):
        source = ("archive", os.path.abspath(args.archive or args.image))
    elif args.image:
        source = ("image", os.path.abspath(args.image))
    elif args.game_dir:
        source = ("folder", os.path.abspath(args.game_dir))
    elif args.repair or decomp_change:
        if not valid_game_folder(game_dir):
            raise SetupError("no installed game files to repair from; run setup with your disc image")
        source = ("installed", game_dir)
    elif installed and ui.interactive:
        say("")
        if state.get("version") != version:
            say("Installed: %s. This release: %s." % (state.get("version"), version))
            i = ui.choose("What do you want to do?", ["update to %s (keeps your game files and saves)" % version,
                                                      "play the installed version", "quit"])
            if i == 1:
                launch(state, data_dir)
                return 0
            if i == 2:
                return 0
            source = ("installed", game_dir)
        else:
            i = ui.choose("The game is installed (%s). What do you want to do?" % version,
                          ["play", "repair (rebuild the game code; keeps game files and saves)",
                           "reinstall from a disc image, Cemu archive or game folder",
                           "add the languages of your European or Japanese game (experimental)", "quit"])
            if i == 0:
                launch(state, data_dir)
                return 0
            if i == 4:
                return 0
            if i == 3:
                p = ui.pick_path("Choose your European or Japanese Wind Waker HD (.wux/.wud disc image, .wua Cemu "
                                 "archive or extracted folder)")
                run_language_source(args, ui, data_dir, p)
                return 0
            if i == 1:
                source = ("installed", game_dir)
    elif installed and not ui.interactive and state.get("version") == version:
        say("Already installed (%s)." % version)
        if not args.no_launch:
            launch(state, data_dir)
        return 0
    elif valid_game_folder(game_dir) and not ui.interactive:
        source = ("installed", game_dir)
    if source is None:
        if valid_game_folder(game_dir) and ui.yesno("Game files from an earlier setup were found in %s. Use them?" % game_dir):
            source = ("installed", game_dir)
        else:
            say("")
            i = ui.choose("What do you have?", ["a disc image (.wux or .wud) with its keys",
                                                 "a Cemu Wii U archive (.wua; no keys needed)",
                                                 "an already extracted game folder (with code, content and meta folders)"])
            if i in (0, 1):
                p = ui.pick_path("Choose your Wind Waker HD disc image (.wux or .wud)" if i == 0 else
                                 "Choose your Wind Waker HD Cemu archive (.wua)",
                                 filetypes=[("Wii U disc image (*.wux;*.wud)", "*.wux;*.wud")] if i == 0 else
                                 [("Cemu Wii U archive (*.wua)", "*.wua")])
                # the file decides: a .wua chosen as a disc image is still an archive
                source = ("archive" if p.lower().endswith(".wua") else "image", p)
            else:
                while True:
                    p = ui.pick_path("Choose the extracted game folder (it contains code, content and meta)", folder=True)
                    if valid_game_folder(p):
                        break
                    if valid_game_folder(os.path.dirname(p)):
                        p = os.path.dirname(p)
                        break
                    say("  That folder does not contain code/cking.rpx, content and meta/meta.xml.")
                source = ("folder", p)

    state = install(ctx, source, ui=ui, check_keys=lambda image: get_disc_keys(image, ui, args))
    if source[0] != "gen":
        maybe_import_save(ui, data_dir)
        write_code_mods_setting(data_dir, state["code_mods"])
    if PORTABLE and ui.interactive and source[0] != "gen":
        if not args.shortcuts and ui.yesno("Add a shortcut to %s?" % {"darwin": "your Applications folder", "win32": "the Start menu"}
                                           .get(sys.platform, "your applications menu"), False):
            state["shortcut"] = create_shortcut()
            write_state(data_dir, state)
        tdir = toolchain_dir(data_dir)
        if os.path.isdir(tdir) and ui.yesno("Remove the downloaded compiler (%s)? Repair and guest mod builds need it; "
                                            "run setup again to restore it." % human(folder_size(tdir)), False):
            remove_toolchain(data_dir)
    say("")
    say("Done. Saves are in %s" % os.path.join(data_dir, "save"))
    if source[0] == "gen":
        say("(built with placeholder code: this executable cannot run the game)")
        return 0
    if not args.no_launch and (not ui.interactive or ui.yesno("Start the game now?", True)):
        launch(state, data_dir)
    return 0


# ---------------------------------------------------------------------------------------------
# graphical installer interface (--gui-protocol)
#
# The graphical installer (tools/installer/gui) runs this script as a child process and talks to it
# in JSON lines. Events (stdout) all carry "event"; replies to a request also carry its "id".
#   hello      {version, platform, data_dir, app_dir, log, installed: {...}|null, game_files: bool,
#               save_exists: bool, toolchain}
#   log        {text}                       progress  {label, done, total, detail}
#   step       {n, total, title, id}        (ids: keys archive folder compiler extract copy translate compile app)
#   reply      {id, ok, ...} for each request below; ok=false carries "problem" and "message"
# Requests (stdin):
#   probe        {path}                  -> kind image|archive|folder|invalid, disc_key, common_key, title;
#                                           archive: title, folder, bytes, message (which title, why)
#   check_keys   {image, disc_key_file?, disc_key_hex?, common_key_file?, common_key_hex?}
#                                        -> title_id, files, bytes  (keys stay in memory for install)
#   install      {source: image|archive|folder|installed, path?, jobs?}  -> streams step/progress/log, then
#                                           reply {app, exe, data_dir}
#   import_save  {kind: hd|gc, path, replace?}  -> message; problem "exists" if a save is there
#   language_sources {}                  -> sources: [{region, title_id, source, packs: [{file, language, bytes}]}]
#   add_language_source {path, disc_key_file?, disc_key_hex?, common_key_file?, common_key_hex?}
#                                        -> sources (the added ones); experimental (docs/language-packs.md)
#   remove_language_source {region}      -> removed (the folder)
#   launch       {}                      -> starts the installed game (the decomp version when start_decomp)
#   start_decomp {on}                    -> start the decomp version from now on (development builds, macOS;
#                                           install {decomp: true} builds it; hello: decomp_offered, decomp)
#   quit         {}
# Keys never leave this process: they are not logged, not echoed, not written to disk.


def find_sidecar_disc_key(image):
    side = os.path.splitext(image)[0] + ".key"
    return side if os.path.isfile(side) and read_key_file(side) else None


def find_common_key(image):
    if os.environ.get("WIIU_COMMON_KEY") and parse_key(os.environ["WIIU_COMMON_KEY"]):
        return "WIIU_COMMON_KEY", parse_key(os.environ["WIIU_COMMON_KEY"])
    for d in (os.path.dirname(image), PKG):
        p = os.path.join(d, "common.key")
        k = read_key_file(p) if os.path.isfile(p) else None
        if k:
            return p, k
    return None, None


KEY_MESSAGES = {
    "disc_key_bad": "No valid disc key was supplied. Paste 32 hex digits or choose a key file / Cemu keys.txt.",
    "disc_key_is_common": "You entered the Wii U common key as the disc key. The disc key is a different key that belongs to your disc dump (Cemu keeps it in keys.txt).",
    "disc_key_wrong": "The disc key does not match this disc image. It must be the key dumped together with this disc.",
    "common_key_bad": "That is not a Wii U common key: it must be 16 raw bytes or 32 hex digits.",
    "common_key_wrong": "The Wii U common key is not correct (it is the same 16 bytes on every Wii U).",
    "image_bad": "This file is not a readable Wii U disc image (.wux or .wud).",
    "archive_bad": "This file is not a readable Cemu Wii U archive (.wua).",
    "image_damaged": "The disc image is damaged.",
}


def resolve_disc_keys(image, req, info_reader=None, file_reader=None):
    """Shared CLI/GUI/Android resolver. Returns keys, info, error, value-free notices.

    A common-key error proves the disc partition check passed. Find that disc
    candidate once, then scan common candidates: O(disc + common), not O(n²).
    Explicit paste/single-key inputs take priority; multi-entry files can supply
    either missing key. Never return candidate values in diagnostics.
    """
    info_reader = info_reader or disc_info
    file_reader = file_reader or read_key_entries
    candidates = {"disc": [], "common": []}
    shared = []
    for name in candidates:
        text = req.get(name + "_key_hex")
        path = req.get(name + "_key_file")
        if text is not None:
            key = parse_key(text)
            if not key:
                return None, None, (name + "_key_bad", KEY_MESSAGES[name + "_key_bad"]), []
            candidates[name] = [(key, None)]
        elif path:
            entries = file_reader(path)
            if not entries:
                return None, None, (name + "_key_bad", KEY_MESSAGES[name + "_key_bad"]), []
            label = req.get(name + "_key_label") or os.path.basename(path)
            values = [(key, "%s key: found in %s (entry %d)" %
                       ("Disc" if name == "disc" else "Common", label, i))
                      for i, key in enumerate(entries, 1)]
            candidates[name] = values
            if len(entries) > 1:
                shared.extend((key, label, i) for i, key in enumerate(entries, 1))
    for name in candidates:
        if not candidates[name]:
            candidates[name] = [(key, "%s key: found in %s (entry %d)" %
                                 ("Disc" if name == "disc" else "Common", path, i)) for key, path, i in shared]
        if not candidates[name]:
            if name == "disc":
                side = find_sidecar_disc_key(image)
                key = read_key_file(side) if side else None
            else:
                _, key = find_common_key(image)
            if key:
                candidates[name] = [(key, None)]
    disc = candidates["disc"]
    common = candidates["common"]
    if not disc:
        return None, None, ("disc_key_missing", "The disc key is needed: paste it or choose a key file / Cemu keys.txt."), []
    keys = Keys()
    keys.common = common[0][0] if common else bytes(16)
    disc_notice = None
    for key, notice in disc:
        keys.disc = key
        problem, error, info = info_reader(image, keys)
        if problem not in ("disc_key_bad", "disc_key_wrong"):
            if problem not in (None, "common_key_bad", "common_key_wrong"):
                return None, None, (problem, KEY_MESSAGES.get(problem, error)), []
            disc_notice = notice
            break
    else:
        same = len(disc) == 1 and any(disc[0][0] == key for key, _ in common)
        problem = "disc_key_is_common" if same else "disc_key_wrong"
        return None, None, (problem, KEY_MESSAGES[problem]), []
    if not common:
        return None, None, ("common_key_missing", "The Wii U common key is needed: paste it or choose a key file / Cemu keys.txt."), []
    for key, notice in common:
        keys.common = key
        problem, error, info = info_reader(image, keys)
        if not problem:
            return keys, info, None, [n for n in (disc_notice, notice) if n]
        if problem not in ("common_key_bad", "common_key_wrong"):
            return None, None, (problem, KEY_MESSAGES.get(problem, error)), []
    return None, None, ("common_key_wrong", KEY_MESSAGES["common_key_wrong"]), []


def gui_main(args):
    global GUI
    GUI = Protocol()
    try:
        ctx = Ctx(args)
    except SetupError as e:
        GUI.emit({"event": "fatal", "message": plain_text(e)})
        return 1
    session = {"keys": None, "info": None, "image": None}

    def hello():
        st = ctx.state()
        legacy_save, legacy_items = import_sources() if PORTABLE else (None, [])
        GUI.emit({"event": "hello", "version": ctx.version, "platform": ctx.manifest["platform"],
                  "data_dir": ctx.data_dir, "app_dir": ctx.app_dir if IS_MAC else "",
                  "log": LOG.f.name if LOG.f else "", "portable": PORTABLE, "package": PKG,
                  "installed": st if ctx.installed() else None, "game_files": valid_game_folder(ctx.game_dir),
                  "game_dir": ctx.game_dir, "save_exists": have_save(ctx.data_dir),
                  "legacy": bool(legacy_save or legacy_items), "free_bytes": free_space(ctx.data_dir),
                  "language_sources": sources_reply(),
                  "code_mods": setup_code_mods(ctx),
                  # the decomp version (decomp_setup.py): offered on macOS in development builds only
                  "decomp_offered": decomp_offered(ctx.manifest), "decomp": decomp_wanted(ctx),
                  "decomp_text": decomp_setup.DECOMP_TEXT, "decomp_docs": decomp_setup.DOCS_URL,
                  "toolchain": ctx.manifest["toolchain"]})

    def reply(req, ok=True, **kw):
        kw.update({"event": "reply", "id": req.get("id"), "cmd": req.get("cmd"), "ok": ok})
        GUI.emit(kw)

    def fail(req, problem, message):
        reply(req, False, problem=problem, message=plain_text(message))

    def probe(req):
        p = req.get("path") or ""
        if os.path.isdir(p):
            folder = p if valid_game_folder(p) else (os.path.dirname(p) if valid_game_folder(os.path.dirname(p)) else None)
            if not folder:
                return fail(req, "invalid", "This folder does not contain an extracted game (code, content and meta).")
            tid = game_folder_title(folder)
            if tid and tid not in SUPPORTED_BUILDS:
                try:
                    check_title(tid)
                except SetupError as e:
                    return fail(req, "wrong_title", str(e))
            try:
                check_game_version(folder)
            except SetupError as e:
                return fail(req, "wrong_version", str(e)[0].upper() + str(e)[1:])
            return reply(req, kind="folder", path=folder, title=title_desc(tid) if tid else "The Wind Waker HD",
                         in_place=PORTABLE, bytes=folder_size(folder))
        if not os.path.isfile(p):
            return fail(req, "invalid", "File not found.")
        if p.lower().endswith(".wua"):
            problem, err, info = archive_info(p)
            if problem and problem != "wrong_title":
                return fail(req, problem, "%s %s" % (KEY_MESSAGES["archive_bad"], err))
            try:
                folder, notes = archive_choice(info)
            except SetupError as e:
                return fail(req, "wrong_title", str(e)[0].upper() + str(e)[1:])
            msg = ("Cemu Wii U archive: %s (folder %s), no keys needed. The game files are extracted from it into "
                   "this folder (about %s)." % (title_desc(info["title_id"], int(info["version"])), folder,
                                                human(int(info.get("bytes", 0)))))
            return reply(req, kind="archive", path=p, title=title_desc(info["title_id"], int(info["version"])),
                         folder=folder, bytes=int(info.get("bytes", 0)), message=" ".join([msg] + notes))
        if not p.lower().endswith((".wux", ".wud")):
            return fail(req, "invalid", "Choose a .wux or .wud disc image, a .wua Cemu archive, or an extracted game "
                        "folder.")
        src, _ = find_common_key(p)
        reply(req, kind="image", path=p, disc_key=find_sidecar_disc_key(p), common_key=src)

    def request_keys(req, image):
        """(keys, None) or (None, (problem, message)) from a request's key fields (check_keys, add_language_source)."""
        keys, _, why, _ = resolve_disc_keys(image, req)
        return keys, why

    def sources_reply():
        return [{k: m.get(k) for k in ("region", "title_id", "source", "packs")} for m in language_sources(ctx.data_dir)]

    def do_language_sources(req):
        reply(req, sources=sources_reply())

    def do_add_language_source(req):
        path = req.get("path") or ""
        try:
            kind = language_source_kind(path)
            keys = info = None
            if kind == "image":
                keys, why = request_keys(req, path)
                if why:
                    return fail(req, *why)
                problem, err, info = disc_info(path, keys)
                if problem:
                    return fail(req, problem, KEY_MESSAGES.get(problem, err))
                language_source_region(info.get("title_id"))
            added = add_language_source((kind, path), ctx.data_dir, keys, info, ctx.game_dir)
        except SetupError as e:
            return fail(req, "language_source", str(e)[0].upper() + str(e)[1:])
        reply(req, sources=[{k: m.get(k) for k in ("region", "title_id", "source", "packs")} for m in added])

    def do_remove_language_source(req):
        try:
            reply(req, removed=remove_language_source(ctx.data_dir, req.get("region")))
        except SetupError as e:
            fail(req, "language_source", str(e))

    def check(req):
        image = req.get("image") or ""
        session.update(keys=None, info=None, image=None)
        keys, info, why, notices = resolve_disc_keys(image, req)
        if why:
            return fail(req, *why)
        try:
            check_title(info.get("title_id", ""))
        except SetupError as e:
            return fail(req, "wrong_title", str(e))
        session.update(keys=keys, info=info, image=image)
        reply(req, title_id=info.get("title_id"), files=int(info.get("files", 0)), bytes=int(info.get("bytes", 0)),
              message="\n".join(notices))

    def do_install(req):
        kind = req.get("source")
        if "code_mods" in req:
            if not isinstance(req["code_mods"], bool):
                return fail(req, "bad_request", "code_mods must be a boolean")
            ctx.args.code_mods = "1" if req["code_mods"] else "0"
        if "decomp" in req:
            if not isinstance(req["decomp"], bool):
                return fail(req, "bad_request", "decomp must be a boolean")
            ctx.args.decomp = req["decomp"]
        if "skip_shader_prebuild" in req:
            ctx.args.skip_shader_prebuild = bool(req["skip_shader_prebuild"])
        if req.get("shader_prebuild_cancel"):
            # GUI marker is confined to this installation, not an arbitrary path.
            ctx.args.shader_prebuild_cancel = str(Path(ctx.data_dir) / "shader-prebuild-cancel")
            Path(ctx.args.shader_prebuild_cancel).unlink(missing_ok=True)
        if req.get("jobs"):
            ctx.args.jobs = int(req["jobs"])
        if kind == "image":
            if not session["keys"] or session["image"] != req.get("path"):
                return fail(req, "keys", "Check the keys first.")
            source = ("image", req["path"])
        elif kind == "archive":
            source = ("archive", req.get("path") or "")
        elif kind == "folder":
            source = ("folder", req.get("path") or "")
        elif kind == "installed":
            source = ("installed", ctx.game_dir)
        else:
            return fail(req, "bad_request", "unknown source")
        steps = plan_steps(kind, decomp_wanted(ctx))
        GUI.emit({"event": "plan", "id": req.get("id"), "steps": [{"id": s, "title": STEP_TITLES[s]} for s in steps]})
        try:
            state = install(ctx, source, session["keys"], session["info"])
        except SetupError as e:
            LOG.write("setup stopped: %s" % e)
            return fail(req, "install", str(e))
        finally:
            session.update(keys=None, info=None)
        tdir = toolchain_dir(ctx.data_dir)
        reply(req, app=state.get("app", ""), exe=state.get("exe", ""), data_dir=ctx.data_dir,
              decomp_exe=state.get("decomp_exe", ""), decomp_error=state.get("decomp_error", ""),
              game_dir=state.get("game_dir", ""), save_exists=have_save(ctx.data_dir),
              toolchain_bytes=folder_size(tdir) if os.path.isdir(tdir) else 0)

    def save(req):
        try:
            msg = import_save(req.get("kind", "hd"), req.get("path") or "", ctx.data_dir, bool(req.get("replace")))
        except SetupError as e:
            return fail(req, "exists" if have_save(ctx.data_dir) and not req.get("replace") else "save", str(e))
        reply(req, message=msg)

    def do_import_existing(req):
        try:
            msg = import_existing(ctx.data_dir, req.get("path") or None, bool(req.get("replace")))
            st = ctx.state()
            if "code_mods" in st:
                write_code_mods_setting(ctx.data_dir, st["code_mods"])
        except SetupError as e:
            if have_save(ctx.data_dir) and not req.get("replace") and "already exists" in str(e):
                return fail(req, "exists", str(e))
            return fail(req, "import", str(e))
        reply(req, message=msg)

    def do_remove_toolchain(req):
        reply(req, freed=remove_toolchain(ctx.data_dir))

    def do_shortcut(req):
        try:
            reply(req, path=create_shortcut())
        except (SetupError, OSError) as e:
            fail(req, "shortcut", str(e))

    def do_launch(req):
        st = ctx.state()
        if not st:
            return fail(req, "not_installed", "The game is not installed.")
        launch(st, ctx.data_dir)
        reply(req)

    def do_start_decomp(req):
        """{on: bool}: start the decomp version instead of the normal game from now on"""
        st = ctx.state()
        on = req.get("on")
        if not isinstance(on, bool):
            return fail(req, "bad_request", "on must be a boolean")
        if on and not (st.get("decomp_exe") and os.path.isfile(st["decomp_exe"])):
            return fail(req, "not_built", "The decomp version is not built.")
        st["start_decomp"] = on
        write_state(ctx.data_dir, st)
        reply(req, start_decomp=on)

    handlers = {"probe": probe, "check_keys": check, "install": do_install, "import_save": save,
                "import_existing": do_import_existing, "remove_toolchain": do_remove_toolchain,
                "shortcut": do_shortcut, "launch": do_launch, "hello": lambda req: hello(),
                "start_decomp": do_start_decomp,
                "language_sources": do_language_sources, "add_language_source": do_add_language_source,
                "remove_language_source": do_remove_language_source}
    hello()
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except ValueError:
            GUI.emit({"event": "error", "message": "malformed request"})
            continue
        if req.get("cmd") == "quit":
            break
        h = handlers.get(req.get("cmd"))
        if not h:
            fail(req, "bad_request", "unknown command")
            continue
        try:
            h(req)
        except Exception as e:  # report, keep serving
            LOG.write("internal error in %s: %r" % (req.get("cmd"), e))
            fail(req, "internal", "%s: %s" % (type(e).__name__, e))
        del req, line
    return 0


if __name__ == "__main__":
    sys.exit(main())
