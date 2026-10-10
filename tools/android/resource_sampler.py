"""Read-only, bounded-interval Android setup resource sampling.

PSS/RSS reads are sequential and short-lived processes/peaks can be missed.
Reported maxima are sampled observations, never kernel high-water marks.
"""
import json
import re
import threading
import time


def process_tree(output, package):
    rows = {}
    for line in output.splitlines():
        fields = line.split(None, 2)
        if len(fields) == 3 and fields[0].isdigit() and fields[1].isdigit():
            rows[int(fields[0])] = (int(fields[1]), fields[2])
    roots = {pid for pid, (_, name) in rows.items()
             if name == package or name.startswith(package + ":")}
    selected = set(roots)
    while True:
        children = {pid for pid, (parent, _) in rows.items() if parent in selected}
        if children <= selected: break
        selected |= children
    return {pid: ("app" if pid in roots else "native_child") for pid in sorted(selected)}


def memory_totals(output):
    pss = re.search(r"TOTAL PSS:\s*(\d+)", output)
    rss = re.search(r"TOTAL RSS:\s*(\d+)", output)
    if not pss: raise ValueError("Missing process TOTAL PSS")
    return {"pss_bytes": int(pss[1]) * 1024,
            "rss_bytes": int(rss[1]) * 1024 if rss else None}


def private_bytes(output):
    fields = output.split()
    if len(fields) != 2 or not fields[0].isdigit() or fields[1] != ".":
        raise ValueError("Incomplete private storage measurement")
    return int(fields[0]) * 1024


def available_bytes(output):
    rows = output.splitlines()
    if len(rows) != 2: raise ValueError("Unexpected data-volume measurement")
    fields = rows[1].split()
    if len(fields) < 6 or not fields[3].isdigit():
        raise ValueError("Missing available data-volume blocks")
    return int(fields[3]) * 1024


class ResourceSampler:
    def __init__(self, shell, package, job, interval=.25):
        self.shell, self.package, self.job = shell, package, job
        self.interval = interval
        self.samples = []
        self.origin = time.monotonic()
        self.done = threading.Event()
        self.thread = None

    def _read(self, *command):
        return self.shell(*command).decode()

    def _sample(self):
        started = time.monotonic()
        sample = {"start_seconds": started - self.origin, "processes": []}
        try:
            stage = json.loads(self._read("run-as", self.package, "cat", self.job + "/host.json"))
            sample["job_state"] = {k: stage[k] for k in ("state", "stage") if k in stage}
            try:
                worker = json.loads(self._read("run-as", self.package, "cat", self.job + "/state.json"))
                current_stage = worker.get("last_event", {}).get("stage")
                if current_stage in ("import", "extract", "translate", "compile", "link", "activate"):
                    sample["job_state"]["stage"] = current_stage
            except Exception: pass  # Not present during initial resource preparation.
            selected = process_tree(self._read("ps", "-A", "-o", "PID,PPID,NAME"), self.package)
            missing = []
            for pid, role in selected.items():
                if self.done.is_set() or time.monotonic() - started > 10:
                    missing.append(pid)
                    continue
                try:
                    totals = memory_totals(self._read("dumpsys", "meminfo", "-s", str(pid)))
                    sample["processes"].append({"pid": pid, "role": role, **totals})
                except Exception:
                    # The child may exit before meminfo; it must not count as zero.
                    missing.append(pid)
            after = process_tree(self._read("ps", "-A", "-o", "PID,PPID,NAME"), self.package)
            sample["missing_pids"] = missing
            sample["process_set_changed"] = set(after) != set(selected)
            sample["memory_complete"] = bool(selected) and not missing and not sample["process_set_changed"]
            sample["sum_pss_bytes"] = sum(p["pss_bytes"] for p in sample["processes"])
            sample["sum_rss_bytes"] = (sum(p["rss_bytes"] for p in sample["processes"])
                                       if sample["processes"] and all(p["rss_bytes"] is not None for p in sample["processes"]) else None)
            sample["private_allocated_bytes"] = private_bytes(self._read("run-as", self.package, "du", "-sk", "."))
            sample["data_available_bytes"] = available_bytes(self._read("df", "-k", "/data"))
        except Exception as failure:
            sample["error_type"] = type(failure).__name__
        sample["duration_seconds"] = time.monotonic() - started
        self.samples.append(sample)

    def _run(self):
        while not self.done.wait(self.interval): self._sample()

    def start(self):
        self._sample()  # Baseline finishes before the measured build starts.
        self.thread = threading.Thread(target=self._run, name="android-resource-sampler", daemon=True)
        self.thread.start()

    def stop(self):
        self.done.set()
        if self.thread is not None:
            self.thread.join(30)
            if self.thread.is_alive(): raise RuntimeError("Resource sampler did not stop")
        # A separate final observation measures retained data after the build.
        self.done.clear()
        self._sample()
        self.done.set()
        complete = [s for s in self.samples if s.get("memory_complete") and "error_type" not in s]
        storage = [s for s in self.samples if "private_allocated_bytes" in s]
        gaps = [b["start_seconds"] - a["start_seconds"] for a, b in zip(self.samples, self.samples[1:])]
        return {
            "scope": "app processes and descendants; sequential PSS/RSS reads; app-private allocated blocks; shared /data free blocks",
            "limitations": "Sampled maxima can miss short-lived children and peaks. Reads are not simultaneous. RSS sums double-count shared pages. /data free-space changes include unrelated system work. Sampling adds work to the emulator. APK/native installation and external storage are excluded from private allocation.",
            "requested_delay_seconds": self.interval,
            "max_observed_start_gap_seconds": max(gaps, default=0),
            "max_sample_duration_seconds": max((s["duration_seconds"] for s in self.samples), default=0),
            "complete_memory_samples": len(complete),
            "incomplete_memory_samples": len(self.samples) - len(complete),
            "native_child_samples": sum(any(p["role"] == "native_child" for p in s["processes"]) for s in self.samples),
            "complete_native_child_samples": sum(any(p["role"] == "native_child" for p in s["processes"]) for s in complete),
            "max_sampled_sum_pss_bytes": max((s["sum_pss_bytes"] for s in complete), default=None),
            "max_sampled_sum_rss_bytes": max((s["sum_rss_bytes"] for s in complete if s["sum_rss_bytes"] is not None), default=None),
            "baseline_private_allocated_bytes": self.samples[0].get("private_allocated_bytes"),
            "max_sampled_private_allocated_bytes": max((s["private_allocated_bytes"] for s in storage), default=None),
            "final_private_allocated_bytes": self.samples[-1].get("private_allocated_bytes"),
            "min_sampled_data_available_bytes": min((s["data_available_bytes"] for s in storage), default=None),
            "samples": self.samples,
        }


def main():
    """Capture a real debug job without changing the app or its selected inputs."""
    import argparse
    from pathlib import Path
    import shlex
    import subprocess

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--job-id", required=True, help="32 hexadecimal digits from the debug app's setup.json pointer")
    parser.add_argument("--seconds", type=int, default=600, help="maximum capture window; also stops when the job completes or fails")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-f]{32}", args.job_id): parser.error("invalid job ID")
    if not 1 <= args.seconds <= 86400: parser.error("capture window must be 1..86400 seconds")
    package = "org.wwhdrecomp.wwhd"

    def shell(*command):
        return subprocess.run([args.adb, "-s", args.serial, "shell", shlex.join(command)],
                              capture_output=True, check=True, timeout=5).stdout

    report = {"device": {key: shell("getprop", prop).decode().strip() for key, prop in
                         (("api", "ro.build.version.sdk"), ("abi", "ro.product.cpu.abi"),
                          ("fingerprint", "ro.build.fingerprint"), ("model", "ro.product.model"))}}
    paths = shell("pm", "path", package).decode().splitlines()
    if len(paths) != 1 or not paths[0].startswith("package:"):
        parser.error("requires one installed debug APK")
    report["installed_apk_sha256"] = shell("sha256sum", paths[0][8:]).decode().split()[0]
    # Reject missing jobs/permissions before spending the entire capture window polling.
    json.loads(shell("run-as", package, "cat", "no_backup/ondevice/jobs/" + args.job_id + "/host.json"))
    sampler = ResourceSampler(shell, package, "no_backup/ondevice/jobs/" + args.job_id)
    started = time.monotonic()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        sampler.start()
        while time.monotonic() - started < args.seconds:
            state = sampler.samples[-1].get("job_state", {}).get("state")
            if state in ("complete", "failed"):
                report["end_reason"] = "job_" + state
                break
            time.sleep(.25)
        else: report["end_reason"] = "capture_window_elapsed"
    except KeyboardInterrupt:
        report["end_reason"] = "capture_interrupted"
    finally:
        try:
            report["resource_samples"] = sampler.stop()
            report["capture_seconds"] = time.monotonic() - started
            report["capture_valid"] = bool(report["resource_samples"]["complete_memory_samples"]
                                           and report["resource_samples"]["max_sampled_private_allocated_bytes"] is not None)
        finally: args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"output": str(args.output), "end_reason": report.get("end_reason")}))


if __name__ == "__main__": main()
