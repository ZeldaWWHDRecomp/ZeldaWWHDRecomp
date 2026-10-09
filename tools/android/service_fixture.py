"""Explicitly packaged debug service fixture; production ondevice_setup stays validating."""
import argparse
import ctypes
from pathlib import Path
import shutil

from android_fixture import synthetic_rpx
from ondevice_setup import run
from setup_adapter import Adapter


def fixture_adapter(*args, resource_probe=False):
    adapter = Adapter(*args)
    package = adapter.package
    # The accepted digest is fixed to authored fixture bytes, never the selected input's digest.
    import hashlib
    adapter.setup.SUPPORTED_RPX_SHA256 = hashlib.sha256(synthetic_rpx()).hexdigest()
    original = adapter.setup.recompile
    original_compile = adapter.compile

    def translate(game, destination):
        count = original(game, destination)
        shutil.copyfile(package / "tools/android/runtime_fixture.c",
                        Path(destination) / "code_runtime_fixture.c")
        (Path(destination) / "code_harness.c").write_text('''#include "funcs.h"
''' + ('''extern unsigned ww_resource_check(unsigned);
''' if resource_probe else "") + '''
int synthetic_check(void) {
    Cpu cpu = {0};
    f_02000000(&cpu);
    return cpu.r[3] == 42''' + (" && ww_resource_check(41) == 42" if resource_probe else "") + ''' ? 0 : 1;
}
''')
        if resource_probe:
            # Long enough to observe a real compiler child, rather than inserting
            # a sleep into the tool launcher. Entirely authored; not translated game C.
            code = [f"__attribute__((noinline)) unsigned ww_resource_{i}(unsigned x) {{ return x + {i}u; }}\n"
                    for i in range(8192)]
            code.append("unsigned ww_resource_check(unsigned x) { return ww_resource_1(x); }\n")
            (Path(destination) / "code_resource_probe.c").write_text("".join(code))
        return count + 2 + int(resource_probe)

    def compile_fixture(toolchain, manifest, *args, **kwargs):
        manifest["link"] = manifest["link"] + ["-Wl,-u,synthetic_check"]
        return original_compile(toolchain, manifest, *args, **kwargs)

    adapter.setup.recompile = translate
    adapter.compile = compile_fixture
    return adapter


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--job", type=Path)
    source.add_argument("--inspect-library", type=Path)
    parser.add_argument("--inspect-output", type=Path)
    args = parser.parse_args(argv)
    import json
    if args.inspect_library:
        if args.inspect_output is None: parser.error("--inspect-output is required")
        library = ctypes.CDLL(str(args.inspect_library))
        if library.synthetic_check() != 0: raise AssertionError("Retained translation did not return 42")
        from setup_adapter import atomic_json
        atomic_json(args.inspect_output, {"passed": True, "revision": library.wwhd_update_fixture_revision()})
        return
    configuration = json.loads(args.job.read_text())
    game = args.job.parent / configuration["input"]["path"]
    # Only the debug harness's exact placeholder can be replaced with authored RPX bytes.
    source = game / "code/cking.rpx"
    if source.read_bytes() == b"authored unsupported synthetic input":
        source.write_bytes(synthetic_rpx())
    ready = run(args.job, adapter_factory=lambda *args: fixture_adapter(
        *args, resource_probe=configuration.get("debug_resource_probe", False)))
    if ready is not None:
        library = ctypes.CDLL(ready["library"])
        if library.synthetic_check() != 0:
            raise AssertionError("Service-compiled translation did not return 42")
        manifest = json.loads((Path(__file__).resolve().parents[2] / "sdk/manifest.json").read_text())
        if "update_fixture_revision" in manifest:
            observed = library.wwhd_update_fixture_revision()
            if observed != manifest["update_fixture_revision"]:
                raise AssertionError("Loaded runtime SDK revision differs from the packaged update")
            from setup_adapter import atomic_json
            atomic_json(args.job.parent / "update-fixture-check.json", {"schema": 1, "revision": observed})
    return ready
