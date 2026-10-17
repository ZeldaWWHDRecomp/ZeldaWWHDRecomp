#!/usr/bin/env python3
"""Measure synthetic production full-state save/load only on an idle machine.

Build full_state_memory_bench explicitly, then coordinate the machine-wide
measurement slot before invoking this tool. Every result includes uptime/load
before and after; a violated gate discards all numbers and returns failure.
No game assets are read. The synthetic slot is isolated and removed afterwards.
"""
import argparse
import hashlib
import json
import os
import shlex
from pathlib import Path
import subprocess
import tempfile
import time


GAME_NAMES = ("wwhd", "wwhd-vulkan", "wwhd-metal", "ZeldaWWHD", "full_state_memory_bench")


def benchmark_processes(processes, own_pid):
    """Recognize benchmark drivers from ps output, including macOS Python."""
    benchmarks = []
    for line in processes.splitlines():
        fields = line.strip().split(None, 1)
        if len(fields) != 2 or int(fields[0]) == own_pid:
            continue
        try:
            argv = shlex.split(fields[1])
        except ValueError:
            continue
        if argv and (Path(argv[0]).name == "full_state_memory_bench" or
                (Path(argv[0]).name.casefold().startswith("python") and any(
                    Path(arg).name in ("run_bench.py", "full_state_memory_bench.py") for arg in argv[1:]))):
            benchmarks.append(int(fields[0]))
    return benchmarks


def idle_snapshot():
    uptime = subprocess.check_output(["uptime"], text=True).strip()
    load = os.getloadavg()[0]
    checks = {}
    for name in GAME_NAMES:
        result = subprocess.run(["pgrep", "-x", name], text=True, capture_output=True)
        if result.returncode not in (0, 1):
            raise RuntimeError(f"pgrep -x {name} failed")
        checks[name] = result.stdout.split()
    processes = subprocess.check_output(["ps", "-axo", "pid=,args="], text=True)
    benchmarks = benchmark_processes(processes, os.getpid())
    snapshot = {"uptime": uptime, "load_1m": load, "pgrep_x": checks,
                "other_benchmark_pids": benchmarks, "time_unix": time.time()}
    if load > 10 or any(checks.values()) or benchmarks:
        raise RuntimeError("idle gate not met: " + json.dumps(snapshot))
    return snapshot


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if not 1 <= args.repeats <= 10:
        parser.error("--repeats must be 1..10")
    binary = args.binary.resolve(strict=True)
    revision = subprocess.check_output(["git", "-C", str(Path(__file__).resolve().parents[2]), "rev-parse", "HEAD"], text=True).strip()
    evidence = {"source_revision": revision, "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "profile": "320MiB resident:192MiB patterned,64MiB pseudorandom,64MiB zero",
                "binary": str(binary),
                "measurement_scope": "Synthetic serializer overhead, not total live-game memory. Load elapsed time includes restoration checksum.",
                "rss_semantics": {"baseline_rss_bytes": "Process high-water RSS before serialization, not instantaneous RSS",
                                  "peak_rss_bytes": "Process high-water RSS after serialization",
                                  "extra_rss_bytes": "Difference between high-water marks, not instantaneous allocation"},
                "measurements": [], "valid": False}
    try:
        with tempfile.TemporaryDirectory(prefix="wwhd-synthetic-state-bench-") as directory:
            (Path(directory) / ".synthetic-full-state-bench").touch()
            env = dict(os.environ, WWHD_STATE_DIR=directory, WWHD_NO_AUDIO="1", WWHD_HEADLESS="1")
            for repeat in range(args.repeats):
                for mode in ("save", "load"):
                    before = idle_snapshot()
                    child = subprocess.run([str(binary), mode], env=env, text=True,
                                           capture_output=True, timeout=120, check=True)
                    after = idle_snapshot()
                    number = json.loads(child.stdout)
                    evidence["measurements"].append({"repeat": repeat + 1, "before": before,
                                                     "after": after, "result": number})
        evidence["valid"] = True
    except (RuntimeError, ValueError, OSError, subprocess.SubprocessError) as error:
        # Never retain measurements from a window whose idle gate was violated.
        evidence["measurements"] = []
        evidence["error"] = str(error)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps({"valid": evidence["valid"], "output": str(args.output),
                      "error": evidence.get("error")}))
    return 0 if evidence["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
