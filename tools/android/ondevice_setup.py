"""Durable setup worker entry point for the Android service.

The host supplies validated, installed tools and privately imported inputs. This
worker builds a candidate; only AndroidGame may validate/load and activate it.
"""
import argparse
import fcntl
import json
from pathlib import Path
import resource
import sys
import threading
import time
from types import SimpleNamespace

from setup_adapter import Adapter, Paused, atomic_json


def private_input(root, name):
    path = (root / name).resolve()
    if not path.is_relative_to(root) or path == root:
        raise ValueError("Imported input must remain inside the setup job")
    return path


def process_resources():
    """Kernel high-water marks for this worker and its reaped native children.

    These are process-lifetime maxima, not a sum or a per-job concurrent peak.
    Android/Linux report KiB; macOS reports bytes for host verification.
    """
    own = resource.getrusage(resource.RUSAGE_SELF)
    children = resource.getrusage(resource.RUSAGE_CHILDREN)
    scale = 1 if sys.platform == "darwin" else 1024
    return {"scope": "process lifetime; reaped children only; maxima are not additive",
            "worker_peak_rss_bytes": int(own.ru_maxrss * scale),
            "largest_reaped_child_peak_rss_bytes": int(children.ru_maxrss * scale),
            "worker_cpu_seconds": own.ru_utime + own.ru_stime,
            "reaped_children_cpu_seconds": children.ru_utime + children.ru_stime}


def run(configuration, package=None, native_run=None, adapter_factory=Adapter):
    configuration = Path(configuration).resolve()
    root = configuration.parent
    package = Path(package).resolve() if package else Path(__file__).resolve().parents[2]
    with (root / "worker.lock").open("a+b") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError("This setup job is already running") from None
        state_lock = threading.Lock()
        state = {"schema": 1, "state": "running", "started": time.time(),
                 "resources_before": process_resources()}

        def publish(event):
            with state_lock:
                state["last_event"] = event
                state["resources_latest"] = process_resources()
                if event.get("stage") and event.get("state") in ("complete", "reused") and "source" not in event:
                    state.setdefault("stages", {})[event["stage"]] = {
                        **event, "resources": state["resources_latest"]}
                state["updated"] = time.time()
                atomic_json(root / "state.json", state)

        def paused():
            return (root / "pause.json").exists() or (root / "manual-pause.json").exists()

        try:
            job = json.loads(configuration.read_text())
            if job.get("schema") != 1:
                raise ValueError("Unsupported setup job version")
            jobs = job.get("jobs", 1)
            if type(jobs) is not int or not 1 <= jobs <= 2:
                raise ValueError("Compilation concurrency must be one or two")
            if not isinstance(job.get("port_revision"), str) or not job["port_revision"]:
                raise ValueError("A release pipeline identity is required")
            adapter = adapter_factory(package, root / "checkpoints", job["port_revision"], publish, paused)
            adapter.boundary()
            source = job["input"]
            path = private_input(root, source["path"])
            if source["kind"] == "folder":
                if not adapter.setup.valid_game_folder(str(path)):
                    raise ValueError("Imported game folder is incomplete")
                game = path
            elif source["kind"] in ("archive", "image"):
                extractor = job["extractor"]
                keys = None
                if source["kind"] == "image":
                    keys = adapter.setup.Keys()
                    for name in ("disc", "common"):
                        key_path = private_input(root, source[name + "_key"])
                        with key_path.open("rb") as stream:
                            data = stream.read(4097)
                        value = adapter.setup.parse_key(data) if len(data) <= 4096 else None
                        if value is None:
                            raise ValueError("Invalid " + name + " key file")
                        setattr(keys, name, value)
                if native_run is None:
                    import wwhd_native
                    native_run = wwhd_native.run
                game = adapter.extract(path, source["kind"], extractor["path"],
                                       extractor["identity"], native_run, keys)
            else:
                raise ValueError("Unsupported imported input kind")
            generated = adapter.translate(game)
            compiler = job["compiler"]
            toolchain = SimpleNamespace(cc=compiler["cc"], cxx=compiler["cxx"], ar=compiler["ar"],
                                        env=compiler.get("env"), rsp=compiler.get("rsp", True))
            manifest = json.loads((package / "sdk/manifest.json").read_text())
            if native_run is None:
                import wwhd_native
                native_run = wwhd_native.run
            objects = adapter.compile(toolchain, manifest, generated, compiler["identity"], native_run, jobs=jobs)
            library = adapter.link(toolchain, manifest, objects, compiler["identity"], native_run)
            adapter.boundary()
            result = {"schema": 1, "library": str(library), "game": str(game),
                      "port_revision": job["port_revision"], "compiler_identity": compiler["identity"]}
            atomic_json(root / "ready.json", result)
            state["state"] = "ready_to_activate"
            publish({"stage": "activate", "state": "ready"})
            return result
        except Paused:
            state["state"] = "paused"
            publish({"state": "paused", "restart_incomplete_stage": True})
            return None
        except Exception as failure:
            state["state"] = "failed"
            # Keys are never interpolated into messages or checkpoint identities.
            state["error"] = str(failure)[:2048]
            publish({"state": "failed"})
            raise


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--job", required=True, type=Path)
    args = parser.parse_args(argv)
    return run(args.job)
