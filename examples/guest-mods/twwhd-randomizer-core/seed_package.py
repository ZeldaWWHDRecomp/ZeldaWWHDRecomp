"""Build a private experimental SDK package from independently verified seed data.

Both player-owned game inputs remain required: the preparation record is not a
signature, and the overlay alone cannot prove the identity of unchanged assets.
No player data belongs in the source repository or a public release.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile

import seed_content
import seed_prepare
from seed_identity import seed_identity

SOURCE = Path(__file__).resolve().parent
REPO = SOURCE.parents[2]
PACKAGE_ID = 'twwhd-randomizer-core'
# Reviewed services supplied by the core mod's runtime. SDK translation alone
# accepts arbitrary named imports; unknown compiler helpers must fail packaging.
CORE_HOST_SERVICES = frozenset({
    'wwhd_log', 'wwhd_log_int', 'wwhd_log_hex',
    'wwhd_native_actor_profile_register', 'wwhd_input_filter_register',
    'wwhd_item_ready_trigger',
    'wwhd_item_action_dispatch',
    'wwhd_ship_normal_sail_wind', 'wwhd_ship_sail_target',
    'memcpy', 'memmove', 'memset',
})
MAX_JSON = 1024 * 1024
MAX_HEADERS = 2 * 1024 * 1024
WARNING = ('EXPERIMENTAL, INCOMPLETE SOURCE PORT. Not a supported playable randomizer. '
           'Native fixtures cover selected behaviors, not a full playthrough or all options. '
           'Use private test saves. Never redistribute this package: it contains player-owned assets.')
FLAGS = ['--target=powerpc-unknown-eabi', '-mcpu=750', '-mstackrealign', '-O2', '-ffreestanding',
         '-fno-builtin', '-nostdlib', '-fno-jump-tables', '-ffunction-sections',
         '-fdata-sections', '-Wall', '-Wextra', '-Werror']


def _json(path):
    def object_pairs(pairs):
        out = {}
        for key, value in pairs:
            if key in out:
                raise ValueError('duplicate JSON key')
            out[key] = value
        return out
    return json.loads(_read(path, MAX_JSON).decode('utf-8'), object_pairs_hook=object_pairs)


def _read(path, maximum):
    descriptor = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
    with os.fdopen(descriptor, 'rb') as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size > maximum:
            raise ValueError('missing, special or oversized input: ' + str(path))
        data = stream.read(maximum + 1)
        after = os.fstat(stream.fileno())
        if len(data) != before.st_size or len(data) > maximum or (
            before.st_mtime_ns, before.st_ctime_ns, before.st_size
        ) != (after.st_mtime_ns, after.st_ctime_ns, after.st_size):
            raise ValueError('input changed while reading: ' + str(path))
        return data


def _hash(data):
    return hashlib.sha256(data).hexdigest()


def _relative(name):
    if (not isinstance(name, str) or not name or len(name.encode('utf-8')) > 512 or
            any(c in name for c in ('\\', ':', '\0')) or
            any(part in ('', '.', '..') for part in name.split('/')) or Path(name).is_absolute()):
        raise ValueError('invalid package-relative content path')
    return name


def _merged_content_digest(base, overlay):
    files = seed_content._inventory(base)
    files.update(overlay)
    identity = hashlib.sha256(b'WWHD source-mod content v1\0')
    for name, path in sorted(files.items()):
        size, digest = seed_content._fingerprint(path)
        identity.update(name.encode('utf-8') + b'\0')
        identity.update(size.to_bytes(8, 'big'))
        identity.update(bytes.fromhex(digest))
    return identity.hexdigest()


def validate_preparation(prepared, base_game, randomized_game):
    """Regenerate declarations/content identity; verify every prepared payload byte."""
    prepared, base_game, randomized_game = map(Path, (prepared, base_game, randomized_game))
    if prepared.is_symlink() or not prepared.is_dir():
        raise ValueError('preparation must be a real directory')
    record = _json(prepared / 'seed-preparation.json')
    if (not isinstance(record, dict) or type(record.get('format_version')) is not int or
            record['format_version'] != 1 or record.get('kind') != 'source-mod-seed-data' or
            record.get('installable') is not False or record.get('content') is None or
            not isinstance(record.get('seed_identity'), str)):
        raise ValueError('a completed --content preparation with a complete seed identity is required')
    original_read, seed_read, actual = seed_prepare._validated_readers(
        base_game / 'code/cking.rpx', randomized_game / 'code/cking.rpx')
    for key in ('base_rpx_sha256', 'seed_rpx_sha256', 'compatibility'):
        if record.get(key) != actual[key]:
            raise ValueError('game inputs no longer match the preparation: ' + key)
    headers = {name: text.encode('utf-8') for name, text in
               seed_prepare.generate_headers(original_read, seed_read).items()}
    hashes = {name: _hash(data) for name, data in headers.items()}
    if record.get('headers') != hashes:
        raise ValueError('prepared header hashes do not match independently decoded seed data')
    allowed = set(headers) | {'assets', 'seed-preparation.json', 'module.elf'}
    for entry in prepared.iterdir():
        if entry.name not in allowed or entry.is_symlink():
            raise ValueError('unexpected or linked preparation input: ' + entry.name)
        if entry.name != 'assets' and not entry.is_file():
            raise ValueError('preparation input is not a regular file')
    for name, data in headers.items():
        if _read(prepared / name, MAX_HEADERS) != data:
            raise ValueError('prepared header changed: ' + name)
    # A prior diagnostic module.elf is deliberately never read or reused.
    assets = prepared / 'assets'
    if assets.is_symlink() or not assets.is_dir():
        raise ValueError('prepared assets must be a real directory')
    if {entry.name for entry in assets.iterdir()} != {'content', 'seed-content.json'}:
        raise ValueError('unexpected files in prepared assets')
    report = record['content']
    if not isinstance(report, dict) or _json(assets / 'seed-content.json') != report:
        raise ValueError('content completion records disagree')
    rows = report.get('files')
    if not isinstance(rows, list) or len(rows) > 4080:
        raise ValueError('content entry count exceeds SDK package limits')
    names = set()
    for row in rows:
        if not isinstance(row, dict) or set(row) != {'path', 'size', 'sha256'}:
            raise ValueError('invalid content record')
        name = _relative(row['path'])
        if name.casefold() in names:
            raise ValueError('duplicate/case-colliding content record')
        names.add(name.casefold())
        if type(row['size']) is not int or not 0 <= row['size'] <= seed_content.FILE_LIMIT:
            raise ValueError('invalid content size')
    inventory = seed_content._inventory(assets / 'content')
    if set(inventory) != {row['path'] for row in rows}:
        raise ValueError('prepared content has missing or extra assets')
    for row in rows:
        if seed_content._fingerprint(inventory[row['path']]) != (row['size'], row['sha256']):
            raise ValueError('prepared content changed: ' + row['path'])
    merged_digest = _merged_content_digest(base_game / 'content', inventory)
    if merged_digest != report.get('seed_content_sha256'):
        raise ValueError('complete merged base/overlay content does not match the preparation')
    # This reconstructs the complete seed inventory by applying its verified
    # overlay to the original content: unchanged files participate in the digest.
    current = seed_content.plan_content_delta(base_game / 'content', randomized_game / 'content')
    if report != current:
        raise ValueError('complete game content no longer matches the prepared overlay')
    complete = seed_identity(actual['seed_rpx_sha256'], current['seed_content_sha256'])
    if record['seed_identity'] != complete:
        raise ValueError('complete seed identity does not match the player game inputs')
    return record, headers


def _snapshot_tree(source, destination, compile_only=False):
    """Pin public compiler inputs; compilation never follows mutable source links."""
    inventory = seed_content._inventory(source)
    destination.mkdir()
    digest = hashlib.sha256()
    total = 0
    for name, path in sorted(inventory.items()):
        if compile_only and Path(name).suffix not in ('.c', '.h'):
            continue
        data = _read(path, 4 * 1024 * 1024)
        total += len(data)
        if total > 16 * 1024 * 1024:
            raise ValueError('compiler input tree is unexpectedly large')
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        digest.update(name.encode('utf-8') + b'\0' + bytes.fromhex(_hash(data)))
    return digest.hexdigest()


def _inspect(package):
    sys.path.insert(0, str(REPO / 'tools/guestmod'))
    from build_guest_mod import inspect_package, package_elf, translator_for
    inspection = inspect_package(package, 0x7F000000)
    manifest, elf = package_elf(package)
    translator = translator_for(elf, 0x7F000000)
    # Traverse every function through the SDK translator as well as parsing its
    # relocation tables. Unsupported instructions cannot become a ready package.
    translated = translator.emit(manifest['id'])
    unsupported = sorted(set(translator.services) - CORE_HOST_SERVICES)
    if unsupported:
        raise ValueError('unsupported core host services: ' + ', '.join(unsupported))
    inspection.update({
        'translated_c_sha256': _hash(translated.encode('utf-8')),
        'function_count': len(translator.entries),
        'services': sorted(translator.services),
        'hooks': [{'kind': kind, 'target': target, 'function': function, 'flags': flags}
                  for kind, target, function, flags in translator.hooks],
        'imports': [{'kind': kind, 'target': value} for kind, value in
                    sorted(set(translator.imports.values()), key=lambda item: (item[0], str(item[1])))],
    })
    return inspection


def _source_revision():
    revision = subprocess.run(['git', '-C', str(REPO), 'rev-parse', 'HEAD'],
                              capture_output=True, text=True, check=True).stdout.strip()
    status = subprocess.run(['git', '-C', str(REPO), 'status', '--porcelain', '--',
                             ':(glob)examples/guest-mods/twwhd-randomizer-core/*.c',
                             ':(glob)examples/guest-mods/twwhd-randomizer-core/*.h',
                             'runtime/guest/include'], capture_output=True, text=True, check=True)
    return {'commit': revision, 'compiler_inputs_modified': bool(status.stdout)}


def _manifest(identity):
    return {'format_version': 1, 'id': PACKAGE_ID, 'name': 'TWWHD Randomizer core (experimental)',
            'version': '0.0.1', 'game_id': 'wwhd', 'kind': 'guest',
            'minimum_manager_version': '1.7.0', 'experimental': True,
            'supported_game_builds': ['USA'],
            'author': 'TWWHD Randomizer contributors; WWHD Recomp source-port contributors',
            'description': WARNING, 'guest': {'api_version': 1, 'elf': 'mod.elf', 'heap_size': 262144},
            'content_dir': 'content',
            'save_profile': {'version': 1, 'provider': 'twwhd-randomizer', 'identity': identity}}


def build_package(prepared, base_game, randomized_game, output, compiler=None, *,
                  source_dir=None, sdk_include=None, provenance=None,
                  expected_source_sha256=None, expected_sdk_sha256=None, compiler_timeout=None):
    prepared, base_game, randomized_game, output = map(Path, (prepared, base_game, randomized_game, output))
    source_dir = Path(source_dir) if source_dir is not None else SOURCE
    sdk_include = Path(sdk_include) if sdk_include is not None else REPO / 'runtime/guest/include'
    if output.exists() or output.is_symlink():
        raise FileExistsError('package output already exists')
    target = output.resolve()
    for source in (prepared, base_game, randomized_game, source_dir, sdk_include):
        source = source.resolve()
        if target == source or source in target.parents or target in source.parents:
            raise ValueError('package output must be separate from all input trees')
    record, headers = validate_preparation(prepared, base_game, randomized_game)
    compiler = compiler or os.environ.get('WWHD_PPC_CLANG') or shutil.which('clang')
    if not compiler:
        raise ValueError('PowerPC clang unavailable; select --ppc-clang')
    command_prefix = [str(compiler)] if isinstance(compiler, (str, Path)) else list(compiler)
    if (not command_prefix or any(not isinstance(part, str) or not part or '\0' in part for part in command_prefix)):
        raise ValueError('invalid structured PowerPC compiler command')
    if compiler_timeout is not None and (type(compiler_timeout) is not int or not 1 <= compiler_timeout <= 1800):
        raise ValueError('invalid compiler timeout')
    version = subprocess.run([*command_prefix, '--version'], capture_output=True, text=True, check=True,
                             timeout=min(compiler_timeout,15) if compiler_timeout else None).stdout
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.wwhd-source-package-', dir=output.parent) as temporary:
        temporary = Path(temporary)
        source = temporary / 'source'
        # Copy the reviewed source tree, then inject independently regenerated
        # declarations. Existing preparation ELF/code never enters this build.
        source_hash = _snapshot_tree(source_dir, source, compile_only=True)
        sdk = temporary / 'sdk'
        sdk_hash = _snapshot_tree(sdk_include, sdk)
        if expected_source_sha256 is not None and source_hash != expected_source_sha256:
            raise ValueError('release source inputs changed')
        if expected_sdk_sha256 is not None and sdk_hash != expected_sdk_sha256:
            raise ValueError('release SDK inputs changed')
        for name, data in headers.items():
            (source / name).write_bytes(data)
        package = temporary / 'package'
        package.mkdir()
        manifest = _manifest(record['seed_identity'])
        (package / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        command = [*command_prefix, *FLAGS, '-I', str(sdk), '-c', str(source / 'mod.c'), '-o', str(package / 'mod.elf')]
        result = subprocess.run(command, capture_output=True, text=True,timeout=compiler_timeout)
        if result.returncode:
            raise ValueError('strict PowerPC compilation failed:\n' + result.stderr[-8000:])
        elf = _read(package / 'mod.elf', 64 * 1024 * 1024)
        try:
            inspection = _inspect(package)
        except Exception as error:
            raise ValueError('SDK inspection/translation failed: ' + str(error)) from error
        if inspection['elf_sha256'] != _hash(elf):
            raise ValueError('compiled ELF changed during SDK inspection')
        receipt = {'format_version': 1, 'kind': 'experimental-source-package',
                   'complete': False, 'warning': WARNING, 'seed_identity': record['seed_identity'],
                   'base_rpx_sha256': record['base_rpx_sha256'], 'seed_rpx_sha256': record['seed_rpx_sha256'],
                   'headers': record['headers'], 'content': record['content'],
                   'source_sha256': source_hash, 'sdk_sha256': sdk_hash,
                   'source_revision': provenance if provenance is not None else _source_revision(),
                   'builder_sha256': _hash(_read(Path(__file__), MAX_HEADERS)),
                   'compiler': version, 'compiler_argv': command_prefix, 'flags': FLAGS, 'elf_sha256': _hash(elf), 'inspection': inspection}
        # Recheck game/preparation inputs after compilation, before any output
        # can appear installable. A stale preparation cannot acquire a new ELF.
        if validate_preparation(prepared, base_game, randomized_game) != (record, headers):
            raise ValueError('seed inputs changed during the package build')
        output.mkdir()  # Exclusive: never replace an existing player directory.
        for row in record['content']['files']:
            data = _read(prepared / 'assets/content' / row['path'], seed_content.FILE_LIMIT)
            if len(data) != row['size'] or _hash(data) != row['sha256']:
                raise ValueError('content changed while publishing: ' + row['path'])
            target = output / 'content' / row['path']
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        (output / 'content').mkdir(exist_ok=True)
        (output / 'mod.elf').write_bytes(elf)
        (output / 'EXPERIMENTAL.txt').write_text(WARNING + '\n', encoding='utf-8')
        (output / 'LICENSE-TWWHD-Randomizer.txt').write_bytes(_read(source_dir / 'LICENSE-TWWHD-Randomizer.txt', MAX_JSON))
        receipt['complete'] = True
        (output / 'source-port-build.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        final_inventory = seed_content._inventory(output)
        if len(final_inventory) + 1 > 4096 or sum(path.stat().st_size for path in final_inventory.values()) + 8192 > seed_content.TOTAL_LIMIT:
            raise ValueError('finished package exceeds SDK size limits')
        # Final completion marker: a failed build/copy has no runtime manifest.
        with (output / 'manifest.json').open('x', encoding='utf-8') as stream:
            stream.write(json.dumps(manifest, indent=2) + '\n')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prepared', type=Path)
    parser.add_argument('output', type=Path, help='new private package directory (never publish it)')
    parser.add_argument('--base-game', type=Path, required=True)
    parser.add_argument('--randomized-game', type=Path, required=True)
    parser.add_argument('--ppc-clang', help='clang executable supporting powerpc-unknown-eabi')
    args = parser.parse_args()
    try:
        result = build_package(args.prepared, args.base_game, args.randomized_game, args.output, args.ppc_clang)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f'Private package build failed: {error}\n')
    print(json.dumps({'package': str(args.output), 'seed_identity': result['seed_identity'],
                      'elf_sha256': result['elf_sha256'], 'experimental': True, 'playthrough_verified': False}))


if __name__ == '__main__':
    main()
