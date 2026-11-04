"""Build a private experimental core mod from original USA + official seed folders.

Run this worker in a fresh process. It never installs/enables a package, changes
an existing installation or starts a game. Import the completed output through
the mod manager's existing install/acknowledgement/restart flow. Never share the
output: it includes player-owned assets.
"""
import argparse
import importlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

# The Windows release's isolated Python omits the script directory.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from core_randomizer_resources import load


def compiler_command(command):
    if (not isinstance(command, list) or not command or len(command) > 32 or
            any(not isinstance(value, str) or not value or '\0' in value or len(value) > 32768 for value in command) or
            not Path(command[0]).is_absolute()):
        raise ValueError('an explicit absolute compiler executable and structured argv are required')
    return command


def publish_progress(path, phase):
    """Progress is advisory; a reader holding the status file must not fail a build."""
    path = Path(path)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps({'phase': phase}))
    for attempt in range(8):
        try:
            os.replace(temporary, path)
            return
        except PermissionError:
            if attempt < 7:
                time.sleep(.01)
    # Keep the previous complete status if replacement remains busy. Completion
    # still arrives through the worker receipt, not this advisory file.
    try:
        temporary.unlink()
    except OSError:
        pass


def build_core(resources, original, randomized, output, compiler, *, progress_file=None, cancel_file=None):
    compiler = compiler_command(compiler)
    root, resource = load(resources)
    original, randomized, output = map(Path, (original, randomized, output))
    target = output.resolve()
    if output.exists() or output.is_symlink():
        raise FileExistsError('package output already exists')
    if target == root.resolve() or target in root.resolve().parents:
        raise ValueError('output must not replace the release')
    for source in ((root/resource['source']).resolve(), (root/resource['sdk']).resolve(),
                   (root/'tools').resolve(), original.resolve(), randomized.resolve()):
        if target == source or source in target.parents or target in source.parents:
            raise ValueError('private output must be separate from release and game inputs')
    if progress_file is not None:
        progress_path = Path(progress_file)
        temporary_status = progress_path.with_suffix(progress_path.suffix + '.tmp')
        for path in (progress_path, temporary_status):
            if path.exists() or path.is_symlink():
                raise FileExistsError('progress output already exists')
            resolved = path.resolve()
            for source in (original.resolve(), randomized.resolve(), target,
                           (root/resource['source']).resolve(), (root/resource['sdk']).resolve(), (root/'tools').resolve()):
                if resolved == source or source in resolved.parents or resolved in source.parents:
                    raise ValueError('progress output must be separate from input/package trees')
        if cancel_file is not None and progress_path.resolve() == Path(cancel_file).resolve():
            raise ValueError('progress and cancellation paths must differ')

    def progress(phase):
        if cancel_file is not None and Path(cancel_file).exists():
            raise ValueError('Core import cancelled after the current step')
        if progress_file is not None:
            publish_progress(progress_file, phase)

    source = root / resource['source']
    for module in ('seed_prepare', 'seed_package', 'seed_content', 'builds', 'randomizer_compat', 'source_core_compat'):
        previous = sys.modules.get(module)
        if previous is not None and not Path(getattr(previous, '__file__', '')).resolve().is_relative_to(root.resolve()):
            raise ValueError('run the core builder in a fresh worker process')
    sys.path[:0] = [str(source), str(root/'tools/recomp'), str(root/'tools')]
    prepare = importlib.import_module('seed_prepare')
    package = importlib.import_module('seed_package')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.wwhd-core-build-', dir=output.parent) as temporary:
        temporary = Path(temporary)
        # Fail before reading/copying a seed when the selected release compiler
        # lacks the PPC backend. This is a compile only: no binary is executed.
        progress('Checking PowerPC compiler')
        probe = temporary / 'probe.o'
        result = subprocess.run([*compiler, *package.FLAGS, '-x', 'c', '-c', '-o', str(probe), '-'],
                                input='int core_ppc_probe(int x){return x+1;}\n',
                                capture_output=True, text=True,timeout=60)
        if result.returncode:
            raise ValueError('PowerPC compiler unavailable:\n' + result.stderr[-8000:])
        data = package._read(probe, 4 << 20)
        if len(data) < 20 or data[:6] != b'\x7fELF\x01\x02' or data[18:20] != b'\x00\x14':
            raise ValueError('compiler did not produce a big-endian PowerPC ELF')
        prepared = temporary / 'prepared'
        progress('Preparing and validating seed')
        prepare.prepare_seed(original, randomized, prepared, include_content=True)
        # Recheck release tool/source identity after the preparation phase too.
        if load(resources) != (root, resource):
            raise ValueError('release resources changed during seed preparation')
        progress('Building private core package')
        receipt = package.build_package(
            prepared, original, randomized, output, compiler,
            source_dir=source, sdk_include=root/resource['sdk'], provenance=resource['provenance'],
            expected_source_sha256=resource['provenance']['source_sha256'],
            expected_sdk_sha256=resource['provenance']['sdk_sha256'],compiler_timeout=300)
    progress('Ready to import')
    return {'package': str(output), 'seed_identity': receipt['seed_identity'],
            'elf_sha256': receipt['elf_sha256'], 'experimental': True,
            'playthrough_verified': False, 'manager_action': 'import', 'restart_required': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources', required=True, type=Path)
    parser.add_argument('--original', required=True, type=Path)
    parser.add_argument('--randomized', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--compiler-json', required=True, help='structured argv JSON array, no shell or PATH lookup')
    parser.add_argument('--progress-file', type=Path)
    parser.add_argument('--cancel-file', type=Path)
    args = parser.parse_args()
    try:
        result = build_core(args.resources, args.original, args.randomized, args.output,
                            json.loads(args.compiler_json),progress_file=args.progress_file,cancel_file=args.cancel_file)
    except (OSError, ValueError, ImportError, subprocess.SubprocessError) as error:
        parser.exit(1, f'Private core build failed: {error}\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
