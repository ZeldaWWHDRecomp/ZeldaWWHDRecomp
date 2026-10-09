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
    headers = ["bindings", "vectors", "ptmf", "animation", "objects", "valoo", "medli", "audio", "actor", "link", "camera", "items", "messages", "save", "data"]
    # Both supported source languages exercise nested public aggregate layout.
    assertions = """
#ifdef __cplusplus
#define SDK_ASSERT(expression) static_assert(expression, "SDK layout")
#else
#define SDK_ASSERT(expression) _Static_assert(expression, "SDK layout")
#endif
SDK_ASSERT(sizeof(ProcFunc_l) == 8);
SDK_ASSERT(sizeof(wwhd_line_check_storage) == 0x6C);
SDK_ASSERT(sizeof(wwhd_safe_string) == 8);
SDK_ASSERT(__builtin_offsetof(wwhd_safe_string, __vtbl) == 4);
SDK_ASSERT(__builtin_offsetof(daPy_lk_c, mFrameCtrlUnder[0].mFrame) == 0x589C);
SDK_ASSERT(__builtin_offsetof(mDoExt_McaMorf, mpModel) == 0x90);
SDK_ASSERT(__builtin_offsetof(mDoExt_McaMorf, mFrameCtrl.mRate) == 0x98);
SDK_ASSERT(__builtin_offsetof(mDoExt_McaMorf, mFrameCtrl.mFrame) == 0x9C);
SDK_ASSERT(WWHD_OFFSET_J3DModel_base_matrix == 0xC8);
SDK_ASSERT(WWHD_OFFSET_line_mat0_points_table == 0x144);
SDK_ASSERT(WWHD_OFFSET_line_check_group == 0x68);
SDK_ASSERT(__builtin_offsetof(ProcFunc_l, d) == 0);
SDK_ASSERT(__builtin_offsetof(ProcFunc_l, i) == 2);
SDK_ASSERT(__builtin_offsetof(ProcFunc_l, f) == 4);
SDK_ASSERT(__builtin_offsetof(daPy_lk_c, mCurProcFunc) == 0x65F4);
SDK_ASSERT(__builtin_offsetof(dr_class, mpMorf) == 0x3D0);
SDK_ASSERT(__builtin_offsetof(daNpc_Md_c, mpMorf) == 0x618);
SDK_ASSERT(WWHD_OFFSET_daObjGong_Act_c_mpMorf == 0x3B4);
SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, current.pos.x) == 0x314);
SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, current.pos.z) == 0x31C);
SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, shape_angle.y) == 0x32A);
SDK_ASSERT(WWHD_PLAY_START_STAGE_NAME_OFFSET == 0x5134);
SDK_ASSERT(WWHD_PLAY_EVENT_RUNNING_OFFSET == 0x5292);
SDK_ASSERT(WWHD_PLAY_ENABLE_NEXT_STAGE_OFFSET == 0x514C);
"""
    for language in ("c", "c++"):
        command = [os.environ["WWHD_PPC_CLANG"], "--target=powerpc-unknown-eabi",
                   "-ffreestanding", "-fsyntax-only", "-x", language, "-"]
        for header in headers:
            command += ["-include", str(REPO / f"runtime/guest/include/wwhd/{header}.h")]
        subprocess.run(command, input=assertions, text=True, check=True)
    print("Host module compiler:", tc.desc, flush=True)
    subprocess.run([sys.executable, str(REPO / "tools" / "guestmod" / "test_guestmod.py"), "-v"], check=True)


if __name__ == "__main__":
    main()
