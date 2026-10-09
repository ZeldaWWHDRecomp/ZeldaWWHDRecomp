"""Game-free entry point used only by the debug PythonSmokeActivity."""
import json
from pathlib import Path
import shutil

from android_fixture import main as fixture_main, inventory
from setup_adapter import Adapter, Paused, digest, inventory as file_hashes


def main(argv):
    argv = list(argv)
    compiler = None
    extractor = None
    if "--extractor-json" in argv:
        position = argv.index("--extractor-json")
        extractor = json.loads(Path(argv[position + 1]).read_text())
        del argv[position:position + 2]
    if "--toolchain-json" in argv:
        position = argv.index("--toolchain-json")
        compiler = json.loads(Path(argv[position + 1]).read_text())
        del argv[position:position + 2]
    import wwhd_native
    status, captured = wwhd_native.run(
        ["/system/bin/sh", "-c", "printf '%s' \"$WWHD_PROBE\"; printf ':stderr' >&2; exit 7"],
        env={"WWHD_PROBE": "native-\u2603"}, timeout=5)
    if status != 7 or captured != "native-\u2603:stderr".encode():
        raise AssertionError("native process status/environment/output mismatch")
    try:
        wwhd_native.run(["/system/bin/sh", "-c", "sleep 10"], timeout=.1)
    except TimeoutError:
        pass
    else:
        raise AssertionError("native process timeout was not enforced")
    try:
        wwhd_native.run(["/this-executable-does-not-exist"])
    except FileNotFoundError:
        pass
    else:
        raise AssertionError("native spawn error was not reported")
    from concurrent.futures import ThreadPoolExecutor
    chunks = []
    status, out, err = wwhd_native.run(
        ["/system/bin/sh", "-c", "cat; printf diagnostic >&2"],
        input=b"synthetic stdin\n", separate=True,
        on_output=lambda stream, chunk: chunks.append((stream, chunk)), timeout=5)
    if (status, out, err) != (0, b"synthetic stdin\n", b"diagnostic"):
        raise AssertionError("private stdin/separate output mismatch")
    if b"".join(chunk for stream, chunk in chunks if stream == "stdout") != out:
        raise AssertionError("streamed stdout mismatch")
    if b"".join(chunk for stream, chunk in chunks if stream == "stderr") != err:
        raise AssertionError("streamed stderr mismatch")
    for options, expected in [({"cancel": lambda: True}, InterruptedError),
                              ({"on_output": lambda *args: 1 / 0}, ZeroDivisionError)]:
        try:
            wwhd_native.run(["/system/bin/sh", "-c", "echo ready; sleep 10"],
                            timeout=5, **options)
        except expected:
            pass
        else:
            raise AssertionError("native cancellation/callback error was not propagated")
    status, _ = wwhd_native.run(["/system/bin/sh", "-c", "exit 0"],
                               input=b"x" * (1024 * 1024), timeout=5)
    if status:
        raise AssertionError("child closing stdin early failed")
    with ThreadPoolExecutor(max_workers=2) as workers:
        futures = [workers.submit(wwhd_native.run, ["/system/bin/echo", str(index)], timeout=5)
                   for index in range(2)]
        if [future.result() for future in futures] != [(0, b"0\n"), (0, b"1\n")]:
            raise AssertionError("parallel native process output mismatch")
    metrics = fixture_main(argv)
    metrics["native_process_verified"] = True
    output = Path(argv[argv.index("--output") + 1])
    game = output / "game"
    (game / "code").mkdir(parents=True)
    shutil.copyfile(output / "synthetic.rpx", game / "code/cking.rpx")
    (game / "content").mkdir()
    (game / "meta").mkdir()
    (game / "meta/meta.xml").write_text("<menu/>")
    package = Path(__file__).resolve().parents[2]
    events = []
    first = Adapter(package, output / "checkpoints", "synthetic-v1", events.append)
    # Test-local validation override: production Adapter always checks the
    # supported game hash. No game bytes or keys are part of this fixture.
    first.setup.SUPPORTED_RPX_SHA256 = digest(game / "code/cking.rpx")
    if extractor:
        archive = package / "tools/android/extraction-fixture.bin"
        pause = [False]
        def pause_on_progress(event):
            if event.get("event") == "progress": pause[0] = True
        interrupted = Adapter(package, output / "paused-extraction", "synthetic-v1",
                              pause_on_progress, lambda: pause[0])
        interrupted.setup.SUPPORTED_RPX_SHA256 = first.setup.SUPPORTED_RPX_SHA256
        try:
            interrupted.extract(archive, "archive", extractor["path"], extractor["identity"], wwhd_native.run)
        except Paused:
            pass
        else:
            raise AssertionError("Extraction did not pause")
        if list(interrupted.storage.rglob("pending-*")) or list(interrupted.storage.rglob("complete.json")):
            raise AssertionError("Paused extraction published incomplete output")
        extracted = first.extract(archive, "archive", extractor["path"], extractor["identity"], wwhd_native.run)
        if (extracted / "code/cking.rpx").read_bytes() != (game / "code/cking.rpx").read_bytes():
            raise AssertionError("Extracted synthetic RPX differs")
        expected_files = json.loads(archive.with_suffix(".json").read_text())
        if file_hashes(extracted) != expected_files:
            raise AssertionError("Extracted archive inventory/content differs")
        if not any(event.get("event") == "progress" for event in events):
            raise AssertionError("Extractor did not stream progress")
        def no_extract(*args, **kwargs):
            raise AssertionError("Completed extraction was not reused")
        if first.extract(archive, "archive", extractor["path"], extractor["identity"], no_extract) != extracted:
            raise AssertionError("Extraction checkpoint changed")
        damaged = output / "damaged-fixture.bin"
        damaged_bytes = bytearray(archive.read_bytes())
        damaged_bytes[0] ^= 1
        damaged.write_bytes(damaged_bytes)
        try:
            first.extract(damaged, "archive", extractor["path"], extractor["identity"], wwhd_native.run)
        except first.setup.SetupError:
            pass
        else:
            raise AssertionError("Damaged archive was accepted")
        if file_hashes(extracted) != expected_files:
            raise AssertionError("Failed extraction damaged completed generation")
        game = extracted
        metrics["extraction_verified"] = True
    # Interrupt a real ppc2c invocation, with an authored delay giving the
    # policy watcher time to arm. Production translator files stay unchanged.
    import ppc2c
    import threading
    import time
    requested = threading.Event()
    interrupted = Adapter(package, output / "paused-translation", "synthetic-v1",
                          events.append, requested.is_set)
    interrupted.setup.SUPPORTED_RPX_SHA256 = first.setup.SUPPORTED_RPX_SHA256
    original_translate = ppc2c.translate
    def slow_instruction(*args):
        requested.set()
        time.sleep(.2)
        return original_translate(*args)
    ppc2c.translate = slow_instruction
    try:
        interrupted.translate(game)
    except Paused:
        pass
    else:
        raise AssertionError("Translation did not pause inside the translator")
    finally:
        ppc2c.translate = original_translate
    if list(interrupted.storage.rglob("pending-*")) or list(interrupted.storage.rglob("complete.json")):
        raise AssertionError("Paused translation retained or published incomplete output")
    if any(thread.name == "android-translation-pause" for thread in threading.enumerate()):
        raise AssertionError("Translation pause watcher was not stopped")
    requested.clear()
    resumed = interrupted.translate(game)
    expected_retry = inventory(output / "gen")
    expected_retry.pop("harness.c")
    if inventory(resumed) != expected_retry:
        raise AssertionError("Resumed translation differs from the unchanged fixture")
    metrics["translation_pause_verified"] = True
    generated = first.translate(game)
    expected = inventory(output / "gen")
    expected.pop("harness.c")
    if inventory(generated) != expected:
        raise AssertionError("shared installer translation differs from fixture")
    restarted = Adapter(package, output / "checkpoints", "synthetic-v1", events.append)
    restarted.setup.SUPPORTED_RPX_SHA256 = first.setup.SUPPORTED_RPX_SHA256
    def unexpected(*args):
        raise AssertionError("completed translation was not reused")
    restarted.setup.recompile = unexpected
    if restarted.translate(game) != generated or events[-1]["state"] != "reused":
        raise AssertionError("checkpoint reuse failed")
    metrics["installer_checkpoint_verified"] = True
    metrics["game_path"] = str(game)
    if compiler:
        import ctypes
        from types import SimpleNamespace
        from android_fixture import HARNESS
        compile_gen = output / "compile-gen"
        shutil.copytree(generated, compile_gen)
        runtime = package / "sdk/manifest.json"
        if runtime.is_file():
            manifest = json.loads(runtime.read_text())
            # The production archive extracts only guest symbols referenced by
            # the runtime. Retain this test-only entry point explicitly too.
            manifest["link"] = manifest["link"] + ["-Wl,-u,synthetic_check"]
            shutil.copyfile(package / "tools/android/runtime_fixture.c", compile_gen / "code_runtime_fixture.c")
            (compile_gen / "code_harness.c").write_text('''#include "funcs.h"
int synthetic_check(void) {
    Cpu cpu = {0};
    f_02000000(&cpu);
    return cpu.r[3] == 42 ? 0 : 1;
}
''')
        else:
            (compile_gen / "code_harness.c").write_text(HARNESS.replace("int main(void)", "int synthetic_check(void)"))
            manifest = {"gamecode_cflags": ["-std=c11", "-O2", "-fPIC", "-I{gen}", "-I{sdk}/include"],
                        "link": ["-shared", "-Wl,--whole-archive", "{gamecode}", "-Wl,--no-whole-archive",
                                 "-Wl,-z,defs", "-o", "{out}"]}
        tc = SimpleNamespace(cc=compiler["cc"], cxx=compiler["cxx"], ar=compiler["ar"], env=None, rsp=True)
        objects = first.compile(tc, manifest, compile_gen, compiler["identity"], wwhd_native.run, jobs=2)
        library = first.link(tc, manifest, objects, compiler["identity"], wwhd_native.run)
        loaded = ctypes.CDLL(str(library))
        if loaded.synthetic_check() != 0:
            raise AssertionError("Android-compiled translation did not return 42")
        if runtime.is_file():
            if not loaded.SDL_main:
                raise AssertionError("Relinked Android runtime lacks SDL entry point")
            metrics["full_runtime_load_verified"] = True
            metrics["runtime_identity"] = manifest["identity"]
        def no_process(*args, **kwargs):
            raise AssertionError("completed compile/link was not reused")
        if restarted.compile(tc, manifest, compile_gen, compiler["identity"], no_process, jobs=2) != objects:
            raise AssertionError("object checkpoint reuse changed inventory")
        if restarted.link(tc, manifest, objects, compiler["identity"], no_process) != library:
            raise AssertionError("library checkpoint reuse changed output")
        # Exercise cancellation after the native bridge has spawned a real
        # tool. The extra translation unit is authored test work, never game C.
        pause = [False]
        interrupted = Adapter(package, first.storage, "synthetic-v1", events.append, lambda: pause[0])
        retained = {path: digest(path) for path in objects}
        previous_library = digest(library)
        heavy = compile_gen / "code_pause_fixture.c"
        callbacks = []
        def interrupt_tool(kind):
            def runner(command, env=None, cancel=None):
                selected = "-c" in command if kind == "compile" else command[0] == tc.cxx[0]
                if not selected:
                    return wwhd_native.run(command, env=env, cancel=cancel)
                calls = 0
                def request():
                    nonlocal calls
                    calls += 1
                    # Compilation reaches a third native poll. Tiny synthetic
                    # links can finish before the second, so cancel that real
                    # process on its first poll, immediately after posix_spawn.
                    if calls >= (3 if kind == "compile" else 1):
                        pause[0] = True
                        callbacks.append(kind)
                    return cancel()
                return wwhd_native.run(command, env=env, cancel=request)
            return runner
        try:
            heavy.write_text("\n".join("int pause_fixture_%d(int x) { return x + %d; }" % (n, n)
                                       for n in range(8000)))
            started = time.monotonic()
            try:
                interrupted.compile(tc, manifest, compile_gen, compiler["identity"], interrupt_tool("compile"), jobs=1)
            except Paused:
                pass
            else:
                raise AssertionError("Active Android compiler did not pause")
            if callbacks != ["compile"] or list(first.storage.rglob("*.pending-*")) or list(first.storage.rglob("pending-*")):
                raise AssertionError("Compiler cancellation did not reap/clean the incomplete command")
            if {path: digest(path) for path in objects} != retained:
                raise AssertionError("Compiler pause changed verified objects")
            metrics["active_compile_pause_seconds"] = time.monotonic() - started
            pause[0] = False
            interrupted.compile(tc, manifest, compile_gen, compiler["identity"], wwhd_native.run, jobs=1)
            interrupted.compile(tc, manifest, compile_gen, compiler["identity"], no_process, jobs=1)
            metrics["active_compile_pause_verified"] = True
        finally:
            heavy.unlink(missing_ok=True)
            pause[0] = False
        if first.compile(tc, manifest, compile_gen, compiler["identity"], no_process, jobs=1) != objects:
            raise AssertionError("Compiler pause probe lost the original object checkpoint")
        alternate = {**manifest, "link": manifest["link"] + ["-Wl,--no-undefined"]}
        started = time.monotonic()
        try:
            interrupted.link(tc, alternate, objects, compiler["identity"], interrupt_tool("link"))
        except Paused:
            pass
        else:
            raise AssertionError("Active Android linker did not pause")
        if callbacks != ["compile", "link"] or list(first.storage.rglob("pending-*")):
            raise AssertionError("Linker cancellation did not reap/clean the incomplete command")
        if digest(library) != previous_library:
            raise AssertionError("Linker pause damaged the previous library")
        metrics["active_link_pause_seconds"] = time.monotonic() - started
        pause[0] = False
        resumed_library = interrupted.link(tc, alternate, objects, compiler["identity"], wwhd_native.run)
        if ctypes.CDLL(str(resumed_library)).synthetic_check() != 0:
            raise AssertionError("Retried Android linker produced an invalid translation")
        if digest(library) != previous_library:
            raise AssertionError("Retried link damaged the previous library")
        metrics["active_link_pause_verified"] = True
        metrics["link_pause_probe_scope"] = "first native poll after process spawn; mid-link progress not established"
        metrics["native_pause_timing_scope"] = "stage entry through pause unwind; includes input validation and cleanup, not isolated cancellation latency"
        metrics["compiler_load_verified"] = True
        metrics["compiled_library_bytes"] = library.stat().st_size
        metrics["compiled_library_path"] = str(library)
        metrics["compiler_identity"] = compiler["identity"]
        if runtime.is_file():
            from ondevice_setup import run as run_job
            job = output / "durable-job"
            shutil.copytree(game, job / "input/game")
            configuration = job / "job.json"
            configuration.write_text(json.dumps({"schema": 1, "jobs": 2, "port_revision": "synthetic-job-v1",
                "input": {"kind": "folder", "path": "input/game"}, "compiler": compiler}))
            def fixture_adapter(*args):
                adapter = Adapter(*args)
                adapter.setup.SUPPORTED_RPX_SHA256 = first.setup.SUPPORTED_RPX_SHA256
                original_translate = adapter.setup.recompile
                original_compile = adapter.compile
                def translate_fixture(game_path, destination):
                    count = original_translate(game_path, destination)
                    for name in ("code_harness.c", "code_runtime_fixture.c"):
                        shutil.copyfile(compile_gen / name, Path(destination) / name)
                    return count + 2
                def compile_fixture(toolchain, recipe, *args, **kwargs):
                    recipe["link"] = recipe["link"] + ["-Wl,-u,synthetic_check"]
                    return original_compile(toolchain, recipe, *args, **kwargs)
                adapter.setup.recompile = translate_fixture
                adapter.compile = compile_fixture
                return adapter
            ready = run_job(configuration, package, wwhd_native.run, fixture_adapter)
            if ctypes.CDLL(ready["library"]).synthetic_check() != 0:
                raise AssertionError("Durable job's compiled translation did not execute")
            if run_job(configuration, package, no_process, fixture_adapter) != ready:
                raise AssertionError("Durable job did not reuse verified stages")
            ready_bytes = (job / "ready.json").read_bytes()
            (job / "pause.json").write_text('{"reason":"manual"}')
            if run_job(configuration, package, no_process, fixture_adapter) is not None:
                raise AssertionError("Durable job ignored manual pause")
            if (job / "ready.json").read_bytes() != ready_bytes:
                raise AssertionError("Pause discarded the previous ready candidate")
            (job / "pause.json").unlink()
            if run_job(configuration, package, no_process, fixture_adapter) != ready:
                raise AssertionError("Paused durable job did not resume from its checkpoints")
            original_configuration = configuration.read_text()
            failed_update = json.loads(original_configuration)
            failed_update["port_revision"] = "synthetic-job-failed-update"
            failed_update["compiler"]["cc"] += ["--wwhd-invalid-fixture-compiler-option"]
            configuration.write_text(json.dumps(failed_update))
            try:
                run_job(configuration, package, wwhd_native.run, fixture_adapter)
            except Exception as failure:
                if "wwhd-invalid-fixture-compiler-option" not in str(failure): raise
            else:
                raise AssertionError("Invalid update compiler command succeeded")
            if json.loads((job / "state.json").read_text())["state"] != "failed" or (job / "ready.json").read_bytes() != ready_bytes:
                raise AssertionError("Failed update discarded its previous ready candidate")
            configuration.write_text(original_configuration)
            if run_job(configuration, package, no_process, fixture_adapter) != ready:
                raise AssertionError("Failed update could not recover the previous verified job")
            metrics["durable_job_verified"] = True
            metrics["compiled_library_path"] = ready["library"]
            metrics["game_path"] = ready["game"]
    metrics["adapter_events"] = events
    (output / "metrics.json").write_text(json.dumps(metrics, indent=2) + "\n")
    return metrics
