"""The decomp version's per-OS parts (decomp_setup.py; docs/decomp-build.md "In setup"): which toolchain does what,
with the compiler setup already downloads for the normal game and nothing from the system.

  macOS    Apple's Command Line Tools: `ld -r` turns hidden symbols local (ld64); fast mode can link the whole image
           with ThinLTO through the compiler's libLTO (decomp_setup.whole_lto).
  Linux    the pinned zig (toolchains.json; also on the Steam Deck, whose system has no compiler): `zig ld.lld -r`
           merges a unit (fast mode: its ThinLTO bitcode, one backend job), then tools/decomp_build/elf_localize.py
           makes its hidden symbols local (llvm-objcopy --localize-hidden, which zig does not have). No whole-image
           link (zig has neither llvm-link nor libLTO).
  Windows  the pinned llvm-mingw: COFF has no relocatable link, so a unit's bitcode (every unit source compiled with
           -flto=thin) is linked and isolated in the IR by tools/decomp_build/unit_ir.py through llvm-mingw's own
           LLVM library (bin/libLLVM-NN.dll, the LLVM C API) and compiled to one native object (--native).
decl_lint.py (fast mode's declaration table) runs `clang++ -fsyntax-only`: the toolchain's own clang++ (Windows:
llvm-mingw's bin; Linux: a two-line script in the work folder that runs zig c++ for the release's target)."""
import glob
import os
import platform
import re
import shutil
import subprocess
import sys

IS_MAC = sys.platform == "darwin"
IS_WIN = sys.platform.startswith("win")
EXE_SUFFIX = ".exe" if IS_WIN else ""

# fast mode's declaration table runs decl_lint.py, which disassembles the game code with capstone (BSD-3-Clause; a
# py3-none wheel from PyPI: no compiled Python module, its own libcapstone). Pinned per OS and processor
# (platform.machine()); none for Windows on ARM64 (PyPI has no such wheel): fast mode is not offered there
# (tools/release/package.py ships only the plain parts for windows-arm64).
_PYPI = "https://files.pythonhosted.org/packages/"
CAPSTONE = {
    ("darwin", "arm64"): {"url": _PYPI + "8f/43/29cd1dbdb2b55bf339bca36444809503b8311dcc04898726a7e35b47ac86/"
                                         "capstone-5.0.7-py3-none-macosx_11_0_arm64.whl",
                          "sha256": "acb89f5bf6f625745a104a3a44819d3acea173228055c1eadc60d2282ae490bb", "size": 1200084},
    ("darwin", "x86_64"): {"url": _PYPI + "b7/6c/f02847b3385651eb00a6a27049bd12982cdd61e981b248e7f0dc8ed15756/"
                                          "capstone-5.0.7-py3-none-macosx_10_9_x86_64.whl",
                           "sha256": "a9f64e3d75d8c4d7b3d26bba153b2992aadcf6b8d57674b4ef176b4ecdd9822f", "size": 1188995},
    ("linux", "x86_64"): {"url": _PYPI + "15/3e/3a4f45dd4eda6fc8051f76bf3ed50ead6040d827000371211fbaaf057625/"
                                         "capstone-5.0.7-py3-none-manylinux_2_17_x86_64.manylinux2014_x86_64.whl",
                          "sha256": "b809a9654844ce0d35099121a851ddd2ab2689df1ff6687037babcedcaae6391", "size": 1482184},
    ("linux", "aarch64"): {"url": _PYPI + "7d/32/084419edcd9a3efaadf5c22166de625090fae833aad0672ddd9fa436d1e2/"
                                          "capstone-5.0.7-py3-none-manylinux_2_17_aarch64.manylinux2014_aarch64.whl",
                           "sha256": "c58546c814567c95e4b9a63bdb8624c960cb8508855c7c767d5f108d7bc09ce2", "size": 1458867},
    ("windows", "x86_64"): {"url": _PYPI + "70/39/2138d890a8e827636b9de9924fbc8527fe83e38b6d26605b30ac55e30ebe/"
                                           "capstone-5.0.7-py3-none-win_amd64.whl",
                            "sha256": "4ab8bcb7da8f221ff45926ca168ca33e76f7237d06fbf3c10780002faa2670e1", "size": 1272204},
}
MACHINES = {"amd64": "x86_64", "x64": "x86_64", "x86_64": "x86_64", "arm64": "arm64", "aarch64": "aarch64"}


def os_name():
    return "darwin" if IS_MAC else "windows" if IS_WIN else "linux"


def capstone_pin(system=None, machine=None):
    """the capstone wheel for this OS and processor, or None"""
    system = system or os_name()
    m = MACHINES.get((machine or platform.machine()).lower(), (machine or platform.machine()).lower())
    if system == "linux" and m == "arm64":
        m = "aarch64"
    if system == "darwin" and m == "aarch64":
        m = "arm64"
    if system == "windows" and m == "aarch64":
        m = "arm64"
    return CAPSTONE.get((system, m))


def total_memory():
    """the machine's memory in bytes (0 when unknown), as setup.default_jobs reads it"""
    try:
        if IS_MAC:
            return int(subprocess.check_output(["sysctl", "-n", "hw.memsize"]))
        if IS_WIN:
            import ctypes

            class MS(ctypes.Structure):
                _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                            ("ullTotalPhys", ctypes.c_ulonglong), ("a", ctypes.c_ulonglong * 6)]
            ms = MS()
            ms.dwLength = ctypes.sizeof(MS)
            ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(ms))
            return int(ms.ullTotalPhys)
        with open("/proc/meminfo") as f:
            return int(re.search(r"MemTotal:\s+(\d+)", f.read()).group(1)) * 1024
    except Exception:
        return 0


def exe_path(data_dir, portable):
    """Where the decomp game goes: next to the normal one in a portable release (it shares bin/'s runtime files and
    portable.txt, so settings, saves and shader caches), else decomp/bin/ in the data folder."""
    if portable:
        return os.path.join(data_dir, "bin", "wwhd-decomp" + EXE_SUFFIX)
    return os.path.join(data_dir, "decomp", "bin", "wwhd" + EXE_SUFFIX)


# Windows: gen.py, decl_lint.py and the compiler work with ordinary paths (no \\?\), so the deepest path the decomp step
# reads or writes below the data folder must stay within MAX_PATH (setup.WINDOWS_MAX_PATH, 259). The deepest one, a
# name of the decompilation's (test_setup.DecompWindowsPaths checks it against the submodule):
#   decomp-src\<12 hex>\wwhd_src\include\gc_api\PowerPC_EABI_Support\Runtime\Inc\CPlusLibPPC.h      (94)
# setup's own steps (unpacking and checking the source, whose tools\verify\units files go deeper) use \\?\ paths.
DEEPEST_DECOMP_PATH = 94
WINDOWS_MAX_PATH = 259


def windows_path_problem(data_dir, windows=None):
    """None, or why the data folder is too deep for the decomp step on Windows"""
    if not (IS_WIN if windows is None else windows):
        return None
    most = WINDOWS_MAX_PATH - 1 - DEEPEST_DECOMP_PATH
    n = len(data_dir.rstrip("\\/"))
    if n <= most:
        return None
    return ("the data folder's path is too long for building the decomp version on Windows (%d characters, at most %d "
            "work): move the game's folder to a shorter path, e.g. C:\\Games\\WindWakerHD" % (n, most))


def toolchain_kind(tc):
    """"xcode-clt", "zig" or "llvm-mingw" (setup.get_toolchain's Toolchain.kind; read from the compiler otherwise)"""
    kind = getattr(tc, "kind", "") or ""
    if kind:
        return kind
    first = os.path.basename(tc.cxx[0]).lower()
    if first == "xcrun":
        return "xcode-clt"
    if first in ("zig", "zig.exe"):
        return "zig"
    if "mingw32" in first:
        return "llvm-mingw"
    return ""


def tool_path(tc, name):
    """the toolchain's NAME as one path: xcrun -f on the Mac, the compiler's own bin folder otherwise"""
    if tc.cxx and os.path.basename(tc.cxx[0]) == "xcrun":
        return subprocess.run(["xcrun", "-f", name], capture_output=True, text=True, check=True).stdout.strip()
    here = os.path.join(os.path.dirname(tc.cxx[0]), name + EXE_SUFFIX)
    if os.path.dirname(tc.cxx[0]) and os.path.isfile(here):
        return here
    return shutil.which(name) or name


def libllvm(tc):
    """llvm-mingw's LLVM library (bin/libLLVM-NN.dll), or None. WWHD_DECOMP_LIBLLVM: another one (tests)."""
    if os.environ.get("WWHD_DECOMP_LIBLLVM"):
        return os.environ["WWHD_DECOMP_LIBLLVM"]
    found = sorted(glob.glob(os.path.join(os.path.dirname(tc.cxx[0]), "libLLVM-*.dll")))
    return found[-1] if found else None


def clangxx_dir(tc, work):
    """a folder with a `clang++` for decl_lint.py's -fsyntax-only pass (put first on PATH)"""
    kind = toolchain_kind(tc)
    if kind == "zig":
        # zig has no clang++ program: a script that runs zig c++ for the release's target (tc.cxx = zig c++ -target T)
        d = os.path.join(work, "bin")
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, "clang++")
        with open(path, "w", encoding="utf-8") as f:
            f.write("#!/bin/sh\nexec %s \"$@\"\n" % " ".join(sh_quote(a) for a in tc.cxx))
        os.chmod(path, 0o755)
        return d
    return os.path.dirname(tool_path(tc, "clang++"))


def sh_quote(a):
    return a if re.match(r"^[A-Za-z0-9_./:=+-]+$", a) else "'" + a.replace("'", "'\\''") + "'"


def unit_flags_extra(tc, fast):
    """more compile options for a game unit: on Windows every unit source is bitcode (the IR isolation needs it;
    fast mode's flags have -flto=thin already)"""
    return ["-flto=thin"] if toolchain_kind(tc) == "llvm-mingw" and not fast else []


def merge_commands(tc, pkg, out, objs, fast, lto=False, liblto=None, python=None):
    """The commands that make one game unit's object `out` from its objects `objs`, hidden symbols local (see the
    module text). lto/liblto: macOS fast mode's whole-image link (decomp_setup.whole_lto). python: the interpreter for
    the port's tools (sys.executable)."""
    python = python or sys.executable
    tools = os.path.join(pkg, "tools", "decomp_build")
    kind = toolchain_kind(tc)
    if kind == "zig":
        lld = [tc.cxx[0], "ld.lld", "-r"] + (["--lto-O2", "--thinlto-jobs=1"] if fast else [])
        return [lld + ["-o", out] + objs, [python, os.path.join(tools, "elf_localize.py"), out]]
    if kind == "llvm-mingw":
        lib = libllvm(tc)
        if not lib:
            raise OSError("the compiler (llvm-mingw) has no bin/libLLVM-*.dll")
        return [[python, os.path.join(tools, "unit_ir.py"), out, "--libllvm", lib, "--cc", tc.cxx[0], "--native"] + objs]
    # macOS (Apple's Command Line Tools), as before
    if lto:
        return [[python, os.path.join(tools, "unit_ir.py"), out, "--liblto", liblto, "--cc", tool_path(tc, "clang++")]
                + objs]
    if fast:
        return [tc.cxx + ["-r", "-nostdlib", "-flto=thin", "-o", out] + objs]
    return [["xcrun", "ld", "-r", "-o", out] + objs]
