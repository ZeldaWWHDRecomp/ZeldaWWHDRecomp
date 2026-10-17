"""Durable setup worker entry point for the Android service.

The host supplies validated, installed tools and privately imported inputs. This
worker builds a candidate; only AndroidGame may validate/load and activate it.
"""
import argparse
import fcntl
import json
import os
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


def run(configuration, package=None, native_run=None, adapter_factory=Adapter, key_files=None):
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
                if event.get("state") == "keys_found":
                    state.setdefault("key_sources", []).append(event["detail"])
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
            if type(jobs) is not int or not 1 <= jobs <= 4:
                raise ValueError("Compilation concurrency must be one to four")
            if not isinstance(job.get("port_revision"), str) or not job["port_revision"]:
                raise ValueError("A release pipeline identity is required")
            # the player's code-mod choice in setup: game code with mod support (the desktop "code mods")
            code_mods = job.get("code_mods", False)
            if type(code_mods) is not bool:
                raise ValueError("The code-mod choice must be true or false")
            adapter = adapter_factory(package, root / "checkpoints", job["port_revision"], publish, paused)
            adapter.boundary()
            source = job["input"]
            path = private_input(root, source["path"])
            if source["kind"] == "folder":
                if not adapter.setup.valid_game_folder(str(path)):
                    missing = [name for name, present in (
                        ("code/cking.rpx", (path / "code" / "cking.rpx").is_file()),
                        ("content", (path / "content").is_dir()),
                        ("meta/meta.xml", (path / "meta" / "meta.xml").is_file())) if not present]
                    raise ValueError("Imported game folder is incomplete (missing " + ", ".join(missing) +
                                     "). Choose the folder that contains code, content and meta.")
                game = path
            elif source["kind"] in ("archive", "image"):
                extractor = job["extractor"]
                keys = None
                if source["kind"] == "image":
                    request = {}
                    entries = {}
                    for name in ("disc", "common"):
                        key_path = (key_files or {}).get(name)
                        if key_path is None and source.get(name + "_key"):
                            key_path = str(private_input(root, source[name + "_key"]))  # legacy test/host inputs
                        if key_path is not None:
                            if isinstance(key_path, bytes):
                                values = (adapter.setup.parse_key_entries(key_path)
                                          if len(key_path) <= adapter.setup.KEY_FILE_LIMIT else [])
                            else:
                                values = adapter.setup.read_key_entries(key_path)
                            if not values:
                                raise ValueError("Invalid " + name + " key file: no valid keys (16 raw bytes or 32 hex digits / Cemu keys.txt)")
                            request[name + "_key_file"] = name + "-keys.txt" if isinstance(key_path, bytes) else key_path
                            if isinstance(key_path, bytes):
                                request.setdefault("_entries", {})[name + "-keys.txt"] = values
                                request[name + "_key_label"] = "selected file"
                            entries[name] = values
                    if entries and all(len(entries.get(name, [])) == 1 for name in ("disc", "common")):
                        keys = adapter.setup.Keys()
                        keys.disc, keys.common = entries["disc"][0], entries["common"][0]
                    else:
                        keys = request
                if native_run is None:
                    import wwhd_native
                    native_run = wwhd_native.run
                game = adapter.extract(path, source["kind"], extractor["path"],
                                       extractor["identity"], native_run, keys)
                if source["kind"] == "image":
                    keys = request = entries = key_path = values = None
                    if key_files is not None:
                        key_files.clear()
            else:
                raise ValueError("Unsupported imported input kind")
            generated = adapter.translate(game, hooks=code_mods)
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
                      "port_revision": job["port_revision"], "compiler_identity": compiler["identity"],
                      "code_mods": code_mods}
            # Android cannot execute the guest shared library as a subprocess.
            # Assemble privately here; renderer startup owns native GPU warming.
            import importlib.util
            helper_path = package / "tools/installer/shader_prebuild.py"
            shader_status = {"status": "failed", "message": "Shader preparation helper missing"}
            if helper_path.is_file():
                spec = importlib.util.spec_from_file_location("wwhd_shader_prebuild", helper_path)
                helper = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(helper)
                def build_headstart(game_path, output, template, progress=None):
                    sys.path.insert(0, str(package / "tools"))
                    shader_spec = importlib.util.spec_from_file_location("wwhd_shaderprep", package / "tools/shaderprep.py")
                    shader = importlib.util.module_from_spec(shader_spec)
                    shader_spec.loader.exec_module(shader)
                    # Embedded Python cannot start worker processes: at most two scan threads,
                    # which overlap the native Yaz0 decoding (more add heat, little speed)
                    shader.cmd_build(str(game_path), str(output), str(template), 0, True, progress,
                                     jobs=min(2, jobs), threads=True)
                if isinstance(job.get("yaz0"), str) and job["yaz0"]:
                    # the APK's native decoder (nativeLibraryDir); shaderprep also finds it itself
                    os.environ["WWHD_YAZ0_LIB"] = job["yaz0"]
                cache_root = Path(job.get("shader_cache_root", root / "shader-prebuild"))
                cache_root.mkdir(parents=True, exist_ok=True)
                helper.atomic_json(cache_root / "launcher.json", dict(schema=1, package=str(package),
                    helper=str(helper_path), template=str(package / "tools/shadercache/template.bin")))
                publish({"stage": "shaders", "state": "running"})
                shader_status = helper.prepare(game, package / "tools/shadercache/template.bin", None,
                    cache_root, package,
                    renderer="vulkan",
                    progress=lambda text, fraction=None: publish(
                        {"stage": "shaders", "detail": text} if fraction is None else
                        {"stage": "shaders", "detail": text, "fraction": round(fraction, 3)}),
                    log=lambda line: None,
                    cancel=paused, skip=bool(job.get("skip_shader_prebuild", False)), builder=build_headstart,
                    runtime_identity=library)
                publish({"stage": "shaders", "state": shader_status["status"]})
            result["shader_prebuild"] = shader_status
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


def read_key_descriptors(descriptors):
    """Read SAF regular files or pipes into bounded memory; never create a key copy."""
    from contextlib import ExitStack
    with ExitStack() as stack:
        files = {}
        for name, fd in descriptors.items():
            if fd is not None:
                stream = stack.enter_context(os.fdopen(os.dup(fd), "rb"))
                data = stream.read(1024 * 1024 + 1)
                if len(data) > 1024 * 1024:
                    raise ValueError("The selected %s key file exceeds 1 MiB." % name)
                files[name] = data
        return files


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--job", required=True, type=Path)
    parser.add_argument("--disc-key-fd", type=int)
    parser.add_argument("--common-key-fd", type=int)
    args = parser.parse_args(argv)
    return run(args.job, key_files=read_key_descriptors({
        "disc": args.disc_key_fd, "common": args.common_key_fd}))
