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

HERE = os.path.dirname(os.path.abspath(__file__))
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
            self.f.write(text.rstrip("\n") + "\n")
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


def read_key_file(path):
    try:
        if os.path.getsize(path) > 4096:
            return None
        with open(path, "rb") as f:
            return parse_key(f.read())
    except OSError:
        return None


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


def download(url, dst, sha256, size_hint, label):
    """Downloads url to dst with a progress bar and checks the SHA-256 before keeping it."""
    tmp = dst + ".part"
    say("  Downloading %s" % url)
    h = hashlib.sha256()
    req = urllib.request.Request(url, headers={"User-Agent": "wwhd-setup"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r, open(tmp, "wb") as f:
            total = int(r.headers.get("Content-Length") or size_hint or 0)
            pr = Progress(label)
            done = 0
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                f.write(chunk)
                h.update(chunk)
                done += len(chunk)
                pr.update(done, total or done, human(done))
            pr.done(human(done))
    except OSError as e:
        raise SetupError("download failed: %s\nCheck your internet connection and run setup again." % e)
    if h.hexdigest() != sha256:
        os.remove(tmp)
        raise SetupError("the download of %s is corrupt or was changed (SHA-256 mismatch); nothing was installed. "
                         "Run setup again; if this repeats, report it." % url)
    os.replace(tmp, dst)


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
        shutil.rmtree(tmp, ignore_errors=True)
        os.makedirs(tmp)
        say("  Unpacking...")
        if arc.endswith(".zip"):
            with zipfile.ZipFile(arc) as z:
                z.extractall(tmp)
        elif shutil.which("tar"):
            run_logged(["tar", "-xf", arc, "-C", tmp], what="unpacking the compiler")
        else:
            with tarfile.open(arc) as t:
                t.extractall(tmp)
        src = os.path.join(tmp, tc["dir"])
        if not os.path.isdir(src):
            raise SetupError("unexpected compiler archive layout")
        shutil.rmtree(tdir, ignore_errors=True)
        os.replace(src, tdir)
        shutil.rmtree(tmp, ignore_errors=True)
        os.remove(arc)
        with open(marker, "w") as f:
            f.write(tc["sha256"] + "\n")
    if kind == "llvm-mingw":
        b = os.path.join(tdir, "bin")
        return Toolchain([os.path.join(b, "x86_64-w64-mingw32-clang.exe")], [os.path.join(b, "x86_64-w64-mingw32-clang++.exe")],
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
    for dp, _, fns in os.walk(path):
        for fn in fns:
            try:
                total += os.path.getsize(os.path.join(dp, fn))
            except OSError:
                pass
    return total


def get_disc_keys(image, ui, args):
    keys = Keys()
    if args.disc_key:
        keys.disc = read_key_file(args.disc_key)
        if not keys.disc:
            raise SetupError("%s does not contain a disc key (16 raw bytes or 32 hex digits)" % args.disc_key)
    else:
        side = os.path.splitext(image)[0] + ".key"
        if os.path.isfile(side):
            keys.disc = read_key_file(side)
            if keys.disc:
                say("  Disc key: found %s" % os.path.basename(side))
    if args.common_key:
        keys.common = read_key_file(args.common_key)
        if not keys.common:
            raise SetupError("%s does not contain the Wii U common key (16 raw bytes or 32 hex digits)" % args.common_key)
    elif os.environ.get("WIIU_COMMON_KEY"):
        keys.common = parse_key(os.environ["WIIU_COMMON_KEY"])
        if not keys.common:
            raise SetupError("WIIU_COMMON_KEY is not 32 hex digits")
        say("  Common key: from WIIU_COMMON_KEY")
    else:
        for d in (os.path.dirname(image), PKG):
            p = os.path.join(d, "common.key")
            if os.path.isfile(p) and read_key_file(p):
                keys.common = read_key_file(p)
                say("  Common key: found %s" % p)
                break
    if not keys.disc:
        keys.disc = ask_key(ui, "disc key", "16 bytes, dumped together with this disc, usually a .key file next to the image")
    if not keys.common:
        keys.common = ask_key(ui, "Wii U common key", "16 bytes, the same for every Wii U, dumped from your console")
    while True:
        problem, err, info = disc_info(image, keys)
        if not problem:
            return keys, info
        if problem in ("disc_key_bad", "disc_key_wrong"):
            say("  The disc key does not match this disc image.")
            if not ui.interactive or args.disc_key:
                raise SetupError(err)
            keys.disc = ask_key(ui, "disc key", "it must be the key dumped together with this disc")
        elif problem in ("common_key_bad", "common_key_wrong"):
            say("  The Wii U common key is not correct.")
            if not ui.interactive or args.common_key:
                raise SetupError(err)
            keys.common = ask_key(ui, "Wii U common key", "the same 16 bytes for every Wii U console")
        else:
            raise SetupError("cannot read the disc image: %s" % err)


def extract_game(image, keys, info, data_dir, title=None):
    """Extracts a disc image (keys) or one title folder of a Cemu archive (title, no keys) into data_dir/game."""
    dst = os.path.join(data_dir, "game")
    tmp = os.path.join(data_dir, "game.partial")
    shutil.rmtree(tmp, ignore_errors=True)
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
        shutil.rmtree(tmp, ignore_errors=True)  # keeps the game files of an earlier setup
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
        shutil.rmtree(out, ignore_errors=True)
        raise SetupError("extracting the game failed: %s" % err)
    pr.done()


def copy_game_folder(src, data_dir):
    dst = os.path.join(data_dir, "game")
    if os.path.realpath(src) == os.path.realpath(dst):
        return
    total = folder_size(src)
    if free_space(data_dir) < total + (1 << 30):
        raise SetupError("not enough free disk space in %s: %s needed" % (data_dir, human(total + (1 << 30))))
    tmp = os.path.join(data_dir, "game.partial")
    shutil.rmtree(tmp, ignore_errors=True)
    pr = Progress("copying")
    done = 0
    for dp, dns, fns in os.walk(src):
        rel = os.path.relpath(dp, src)
        os.makedirs(os.path.join(tmp, rel), exist_ok=True)
        for fn in fns:
            s = os.path.join(dp, fn)
            shutil.copyfile(s, os.path.join(tmp, rel, fn))
            done += os.path.getsize(s)
            pr.update(done, total or 1, "%s of %s" % (human(done), human(total)))
    pr.done()
    replace_dir(tmp, dst)


def replace_dir(new, dst):
    old = dst + ".old"
    shutil.rmtree(old, ignore_errors=True)
    if os.path.exists(dst):
        os.replace(dst, old)
    os.replace(new, dst)
    shutil.rmtree(old, ignore_errors=True)


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
            shutil.rmtree(tmp, ignore_errors=True)
            raise SetupError("%s is not a language pack (damaged or not from the game)" % name)
        packs.append({"file": name, "language": known[name.lower()], "bytes": os.path.getsize(path),
                      "sha256": file_sha256(path)})
    if not packs:
        shutil.rmtree(tmp, ignore_errors=True)
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
    shutil.rmtree(d)
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
        tmp = os.path.join(root, region + ".partial")
        shutil.rmtree(tmp, ignore_errors=True)
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
            tmp = os.path.join(root, region + ".partial")
            shutil.rmtree(tmp, ignore_errors=True)
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
        tmp = os.path.join(root, region + ".partial")
        shutil.rmtree(tmp, ignore_errors=True)
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


def recompile(game_dir, gen_dir):
    shutil.rmtree(gen_dir, ignore_errors=True)
    rpx = os.path.join(game_dir, "code", "cking.rpx")
    out = run_logged([sys.executable, os.path.join(PKG, "tools", "recomp", "recomp.py"), rpx, gen_dir],
                     what="translating the game code")
    n = len(glob.glob(os.path.join(gen_dir, "code_*.c")))
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


def compile_gamecode(tc, manifest, gen_dir, obj_dir, jobs):
    os.makedirs(obj_dir, exist_ok=True)
    srcs = sorted(glob.glob(os.path.join(gen_dir, "code_*.c")))
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
        obj = os.path.join(obj_dir, os.path.basename(src) + ".o")
        cmd = tc.cc + flags + ["-c", fwd(src), "-o", fwd(obj)]
        p = subprocess.run(cmd, env=tc.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = p.stdout.decode("utf-8", "replace")
        with lock:
            if p.returncode != 0:
                failures.append((src, out))
                LOG.write("$ " + " ".join(cmd) + "\n" + out)
            done[0] += 1
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


def link_game(tc, manifest, objs, work, out_exe):
    lib = os.path.join(work, "libgamecode.a")
    if os.path.exists(lib):
        os.remove(lib)
    if tc.rsp:
        rsp = os.path.join(work, "ar.rsp")
        with open(rsp, "w") as f:
            f.write("\n".join('"%s"' % fwd(o) for o in objs))
        run_logged(tc.ar + ["rcs", fwd(lib), "@" + fwd(rsp)], env=tc.env, what="archiving the game code")
    else:
        run_logged(tc.ar + ["rcs", lib] + objs, env=tc.env, what="archiving the game code")
    m = {"sdk": fwd(os.path.join(PKG, "sdk")), "gamecode": fwd(lib), "out": fwd(out_exe)}
    args = [sub(a, m) for a in manifest["link"]]
    if IS_LINUX:
        args += ["-Wl,-rpath,$ORIGIN"]
    if tc.rsp:
        rsp = os.path.join(work, "link.rsp")
        with open(rsp, "w") as f:
            f.write("\n".join('"%s"' % a.replace('"', '\\"') for a in args))
        cmd = tc.cxx + ["@" + fwd(rsp)]
    else:
        cmd = tc.cxx + args
    run_logged(cmd, env=tc.env, what="linking the game")
    if not os.path.isfile(out_exe):
        raise SetupError("the linker produced no executable")


# ---------------------------------------------------------------------------------------------
# launchers


def mac_app(app_path, exe_src, data_dir, version):
    """~/Applications/Wind Waker HD.app: the game binary plus a launcher that points it at the data folder."""
    tmp = app_path + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    macos = os.path.join(tmp, "Contents", "MacOS")
    os.makedirs(macos)
    shutil.copy2(exe_src, os.path.join(macos, "wwhd"))
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
  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
</dict></plist>
""" % (APP_NAME, APP_NAME, version.lstrip("v"), version.lstrip("v"))
    with open(os.path.join(tmp, "Contents", "Info.plist"), "w") as f:
        f.write(plist)
    subprocess.run(["codesign", "--force", "--deep", "-s", "-", tmp], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.makedirs(os.path.dirname(app_path), exist_ok=True)
    replace_dir(tmp, app_path)


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


def launch(state, data_dir):
    say("")
    say("Starting the game...")
    if IS_MAC and state.get("app") and os.path.isdir(state["app"]):
        subprocess.Popen(["open", state["app"]])
        return
    exe = state["exe"]
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
    """Deletes the downloaded compiler (it is needed again only to repair; then it is downloaded again)."""
    d = toolchain_dir(data_dir)
    n = folder_size(d) if os.path.isdir(d) else 0
    shutil.rmtree(d, ignore_errors=True)
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
    shutil.rmtree(tmp, ignore_errors=True)
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
            os.replace(user, backup)
        os.makedirs(os.path.dirname(user), exist_ok=True)
        os.replace(tmp, user)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    n = len([f for f in os.listdir(user) if f.endswith(".sav")])
    msg = "Copied %d save file%s to %s" % (n, "" if n == 1 else "s", user)
    if kind == "gc":
        msg = "Converted the GameCube save to %s" % os.path.join(user, "cking.sav")
    if backup:
        msg += " (the previous save is in %s)" % backup
    say("  " + msg)
    return msg


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
    for k in ("exe", "game_dir"):
        if st.get(k):
            st[k] = abs_from_data(st[k], data_dir)
    return st


def write_state(data_dir, state):
    st = dict(state)
    for k in ("exe", "game_dir", "data_dir"):
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
}


def plan_steps(kind):
    return {"image": ["keys", "compiler", "extract", "translate", "compile", "app"],
            "archive": ["archive", "compiler", "extract", "translate", "compile", "app"],
            "folder": ["folder", "compiler"] + ([] if PORTABLE else ["copy"]) + ["translate", "compile", "app"],
            "installed": ["compiler", "translate", "compile", "app"],
            "gen": ["compiler", "compile", "app"]}[kind]


def install(ctx, source, keys=None, info=None, ui=None, check_keys=None):
    """The whole build for one source: ("image", path) | ("archive", path) | ("folder", path) |
    ("installed", game_dir) | ("gen", generated C). check_keys(image) -> (keys, info) is called for an image without keys.
    Returns the new install state."""
    ui = ui or UI(False)
    args, manifest, data_dir = ctx.args, ctx.manifest, ctx.data_dir
    kind = source[0]
    steps = plan_steps(kind)
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
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    if free_space(data_dir) < (2 << 30):
        raise SetupError("not enough free disk space in %s for building (2 GB needed)" % data_dir)
    if kind == "gen":
        gen_dir = source[1]
    else:
        begin("translate")
        t0 = time.time()
        gen_dir = os.path.join(work, "gen")
        nfiles = recompile(ctx.game_dir, gen_dir)
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
             "placeholder_code": kind == "gen"}
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
            mac_app(app, exe, data_dir, ctx.version)
            os.remove(exe)  # the app holds the game binary
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
    if not args.keep_work:
        shutil.rmtree(work, ignore_errors=True)
    if kind != "gen":
        os.makedirs(os.path.join(data_dir, "save"), exist_ok=True)
    write_state(data_dir, state)
    return state


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
    ap.add_argument("--disc-key", help="disc key file (default: IMAGE.key)")
    ap.add_argument("--common-key", help="Wii U common key file (or WIIU_COMMON_KEY)")
    ap.add_argument("--game-dir", help="an already extracted game folder (code/, content/, meta/) instead of an image")
    ap.add_argument("--gen-dir", help="use this generated code instead of recompiling (build checks)")
    ap.add_argument("--data-dir", help="where the game is installed (default: %s)" % default_data_dir())
    ap.add_argument("--app-dir", help="macOS: where the app goes (default: ~/Applications)")
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
    version, data_dir, game_dir = ctx.version, ctx.data_dir, ctx.game_dir
    say("The Legend of Zelda: The Wind Waker HD - native PC port, setup %s" % version)
    say("This release contains no game files. Setup builds the game from your own dump of the game.")
    say("Install folder: %s" % data_dir)
    state = ctx.state()
    installed = ctx.installed()
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
    elif args.repair:
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
    if PORTABLE and ui.interactive and source[0] != "gen":
        if not args.shortcuts and ui.yesno("Add a shortcut to %s?" % {"darwin": "your Applications folder", "win32": "the Start menu"}
                                           .get(sys.platform, "your applications menu"), False):
            state["shortcut"] = create_shortcut()
            write_state(data_dir, state)
        tdir = toolchain_dir(data_dir)
        if os.path.isdir(tdir) and ui.yesno("Remove the downloaded compiler (%s)? It is only needed to repair the game "
                                            "and is downloaded again then." % human(folder_size(tdir)), True):
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
#   check_keys   {image, disc_key_file?, common_key_file?, common_key_hex?}
#                                        -> title_id, files, bytes  (keys stay in memory for install)
#   install      {source: image|archive|folder|installed, path?, jobs?}  -> streams step/progress/log, then
#                                           reply {app, exe, data_dir}
#   import_save  {kind: hd|gc, path, replace?}  -> message; problem "exists" if a save is there
#   language_sources {}                  -> sources: [{region, title_id, source, packs: [{file, language, bytes}]}]
#   add_language_source {path, disc_key_file?, common_key_file?, common_key_hex?}
#                                        -> sources (the added ones); experimental (docs/language-packs.md)
#   remove_language_source {region}      -> removed (the folder)
#   launch       {}                      -> starts the installed game
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
    "disc_key_bad": "The disc key file does not contain a key (16 raw bytes or 32 hex digits).",
    "disc_key_wrong": "The disc key does not match this disc image. It must be the key dumped together with this disc.",
    "common_key_bad": "That is not a Wii U common key: it must be 16 raw bytes or 32 hex digits.",
    "common_key_wrong": "The Wii U common key is not correct (it is the same 16 bytes on every Wii U).",
    "image_bad": "This file is not a readable Wii U disc image (.wux or .wud).",
    "archive_bad": "This file is not a readable Cemu Wii U archive (.wua).",
    "image_damaged": "The disc image is damaged.",
}


def gui_main(args):
    global GUI
    GUI = Protocol()
    try:
        ctx = Ctx(args)
    except SetupError as e:
        GUI.emit({"event": "fatal", "message": str(e)})
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
                  "toolchain": ctx.manifest["toolchain"]})

    def reply(req, ok=True, **kw):
        kw.update({"event": "reply", "id": req.get("id"), "cmd": req.get("cmd"), "ok": ok})
        GUI.emit(kw)

    def fail(req, problem, message):
        reply(req, False, problem=problem, message=message)

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
        keys = Keys()
        if req.get("disc_key_file"):
            keys.disc = read_key_file(req["disc_key_file"])
            if not keys.disc:
                return None, ("disc_key_bad", KEY_MESSAGES["disc_key_bad"])
        else:
            side = find_sidecar_disc_key(image)
            keys.disc = read_key_file(side) if side else None
            if not keys.disc:
                return None, ("disc_key_missing", "No disc key was found next to the image: choose the key file.")
        given = req.get("common_key_hex") or req.get("common_key_file")
        if req.get("common_key_hex"):
            keys.common = parse_key(req["common_key_hex"])
        elif req.get("common_key_file"):
            keys.common = read_key_file(req["common_key_file"])
        else:
            keys.common = find_common_key(image)[1]
        if not keys.common:
            return None, (("common_key_bad", KEY_MESSAGES["common_key_bad"]) if given
                          else ("common_key_missing", "The Wii U common key is needed."))
        return keys, None

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
        keys = Keys()
        if req.get("disc_key_file"):
            keys.disc = read_key_file(req["disc_key_file"])
            if not keys.disc:
                return fail(req, "disc_key_bad", KEY_MESSAGES["disc_key_bad"])
        else:
            side = find_sidecar_disc_key(image)
            keys.disc = read_key_file(side) if side else None
            if not keys.disc:
                return fail(req, "disc_key_missing", "No disc key was found next to the image: choose the key file.")
        if req.get("common_key_hex"):
            keys.common = parse_key(req["common_key_hex"])
        elif req.get("common_key_file"):
            keys.common = read_key_file(req["common_key_file"])
        else:
            keys.common = find_common_key(image)[1]
        if not keys.common:
            return fail(req, "common_key_bad" if (req.get("common_key_hex") or req.get("common_key_file"))
                        else "common_key_missing",
                        KEY_MESSAGES["common_key_bad"] if (req.get("common_key_hex") or req.get("common_key_file"))
                        else "The Wii U common key is needed.")
        problem, err, info = disc_info(image, keys)
        if problem:
            return fail(req, problem, KEY_MESSAGES.get(problem, err))
        try:
            check_title(info.get("title_id", ""))
        except SetupError as e:
            return fail(req, "wrong_title", str(e))
        session.update(keys=keys, info=info, image=image)
        reply(req, title_id=info.get("title_id"), files=int(info.get("files", 0)), bytes=int(info.get("bytes", 0)))

    def do_install(req):
        kind = req.get("source")
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
        GUI.emit({"event": "plan", "id": req.get("id"), "steps": [{"id": s, "title": STEP_TITLES[s]} for s in plan_steps(kind)]})
        try:
            state = install(ctx, source, session["keys"], session["info"])
        except SetupError as e:
            LOG.write("setup stopped: %s" % e)
            return fail(req, "install", str(e))
        finally:
            session.update(keys=None, info=None)
        tdir = toolchain_dir(ctx.data_dir)
        reply(req, app=state.get("app", ""), exe=state.get("exe", ""), data_dir=ctx.data_dir,
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

    handlers = {"probe": probe, "check_keys": check, "install": do_install, "import_save": save,
                "import_existing": do_import_existing, "remove_toolchain": do_remove_toolchain,
                "shortcut": do_shortcut, "launch": do_launch, "hello": lambda req: hello(),
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
