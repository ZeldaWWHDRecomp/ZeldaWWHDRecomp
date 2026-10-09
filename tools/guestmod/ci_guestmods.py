#!/usr/bin/env python3
"""Compile the synthetic guest examples with the same host toolchain as setup; no game files."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "installer"))
import setup  # noqa: E402


def main():
    if sys.platform == "darwin":
        name = "xcode-clt"
    elif sys.platform == "win32":
        name = "llvm-mingw-20260922"
    else:
        name = "zig-0.16.0"
    # Use setup's existing checksummed downloader and compiler command selection.
    tc = setup.get_toolchain(name, str(REPO / "build" / "guestmod-ci"), None)
    if tc.env:
        os.environ.update(tc.env)
    os.environ["CC"] = shlex.join(tc.cc)
    if sys.platform == "win32":
        # llvm-mingw is the native module compiler, not the modder's PowerPC toolchain.
        bin_dir = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "LLVM" / "bin"
        os.environ.setdefault("WWHD_PPC_CLANG", str(bin_dir / "clang.exe"))
        os.environ.setdefault("WWHD_PPC_LLD", str(bin_dir / "ld.lld.exe"))
    elif sys.platform == "darwin":
        prefix = subprocess.check_output(["brew", "--prefix", "llvm"], text=True).strip()
        lld_prefix = subprocess.check_output(["brew", "--prefix", "lld"], text=True).strip()
        os.environ["WWHD_PPC_CLANG"] = str(Path(prefix) / "bin" / "clang")
        os.environ["WWHD_PPC_LLD"] = str(Path(lld_prefix) / "bin" / "ld.lld")
    else:
        os.environ["WWHD_PPC_CLANG"] = shutil.which("clang") or "clang"
        os.environ["WWHD_PPC_LLD"] = shutil.which("ld.lld") or "ld.lld"
    sys.path.insert(0, str(REPO / "tools" / "guestmod"))
    import test_guestmod
    if not test_guestmod.ppc_ok():
        for name in ("WWHD_PPC_CLANG", "WWHD_PPC_LLD"):
            print(name, os.environ[name], "exists:", Path(os.environ[name]).exists(), flush=True)
        subprocess.run([os.environ["WWHD_PPC_CLANG"], "--print-targets"], check=False)
        raise SystemExit("PowerPC clang/lld unavailable: refusing to skip module compile tests in CI")
    subprocess.run([sys.executable, str(REPO / "tools/installer/test_setup.py"), "GuestBuildConfig", "CodeModsBuild"], check=True)
    subprocess.run([sys.executable, str(REPO / "tools/guestmod/test_public_sdk_index.py")], check=True)
    subprocess.run([sys.executable, str(REPO / "tools/bench/test_run_bench.py")], check=True)
    headers = ["bindings", "actor", "link", "camera", "items", "messages", "save", "data"]
    command = [os.environ["WWHD_PPC_CLANG"], "--target=powerpc-unknown-eabi",
               "-ffreestanding", "-fsyntax-only", "-x", "c", "-"]
    for header in headers:
        command += ["-include", str(REPO / f"runtime/guest/include/wwhd/{header}.h")]
    subprocess.run(command, input="", text=True, check=True)
    print("Host module compiler:", tc.desc, flush=True)
    subprocess.run([sys.executable, str(REPO / "tools" / "guestmod" / "test_guestmod.py"), "-v"], check=True)


if __name__ == "__main__":
    main()
