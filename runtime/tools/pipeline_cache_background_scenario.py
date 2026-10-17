#!/usr/bin/env python3
"""Synthetic Vulkan smoke: background-save, kill without shutdown, restart.

Usage: python3 runtime/tools/pipeline_cache_background_scenario.py BUILD/wwhd OUT
No game files are read. Only the two child processes started here may be killed.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import time


def gate():
    load = os.getloadavg()[0]
    print(f"Start load: {load:.2f}", flush=True)
    if load > 45:
        raise RuntimeError("wait for load <=45")
    for name in ("wwhd", "wwhd_recomp"):
        if subprocess.run(["pgrep", "-x", name], capture_output=True).returncode == 0:
            raise RuntimeError("another game/smoke process is running")


def payload(cache):
    data = cache.read_bytes()
    assert data[:8] == b"WWVKPC02"
    size = int.from_bytes(data[8:16], "little")
    assert size == len(data) - 24 and size > 32
    checksum = 14695981039346656037
    for byte in data[24:]:
        checksum = ((checksum ^ byte) * 1099511628211) & ((1 << 64) - 1)
    assert checksum == int.from_bytes(data[16:24], "little")
    return size


def main():
    binary = Path(sys.argv[1]).resolve()
    out = Path(sys.argv[2]).resolve()
    out.mkdir(parents=True, exist_ok=False)
    cache = out / "pipelines.bin"
    env = dict(os.environ, WWHD_HIDDEN_WINDOWS="1", WWHD_NO_AUDIO="1", WWHD_NO_GAMEPAD="1",
               WWHD_USER_DIR=str(out / "user"), WWHD_VK_PIPELINE_CACHE=str(cache),
               WWHD_SHADER_CACHE=str(out / "shaders"), WWHD_VK_STATS="1")
    gate()
    with (out / "background.log").open("w") as log:
        child = subprocess.Popen([str(binary), "--renderer-smoke"], env=dict(env, WWHD_TEST_CACHE_BACKGROUND="1"), stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                if child.poll() is not None:
                    raise RuntimeError("smoke exited before background checkpoint; inspect background.log")
                if cache.is_file() and "waiting for kill after background cache request" in (out / "background.log").read_text(errors="replace"):
                    size = payload(cache)
                    print(f"Complete cache before kill: {size} payload bytes (initially absent)", flush=True)
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("background checkpoint timed out")
        finally:
            if child.poll() is None:
                child.kill()  # our own child only; never an external PID
            child.wait()
    gate()
    with (out / "restart.log").open("w") as log:
        subprocess.run([str(binary), "--renderer-smoke"], env=env, stdout=log, stderr=log, timeout=180, check=True)
    text = (out / "restart.log").read_text(errors="replace")
    loaded = re.search(r"\[vulkan cache\] loaded (\d+) bytes", text)
    assert loaded and int(loaded.group(1)) == size
    assert "[renderer smoke] PASS" in text
    payload(cache)
    print("PASS: checkpoint survived abrupt kill and restart accepted the full cache", flush=True)


if __name__ == "__main__":
    main()
