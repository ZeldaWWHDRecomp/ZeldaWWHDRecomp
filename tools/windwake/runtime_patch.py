"""Package/check a single authored startup-touch object over the pinned Android SDK.

The patch contains patch.json and one AArch64 relocatable object, never game code.
The base SDK and toolchain are pinned to the official v0.5.0 APK. All other SDK
entries, Python sources, assets and native libraries must remain byte-identical.
Source hashes normalize CRLF to LF so Windows and clean CI checkouts agree.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[2]
TARGET = 'sdk/obj/74-context_touch.cpp.o'
SOURCES = ('runtime/src/mods/context_touch.cpp', 'runtime/src/mods/context_touch.h',
           'runtime/src/mods/startup_touch.h')
REQUIRED_SYMBOLS = frozenset('Java_org_wwhdrecomp_wwhd_TouchControls_' + suffix for suffix in
                             ('nativeContextButtons', 'nativeContextAssets', 'nativeStartupTitleTap'))
MAX_OBJECT_BYTES = 4 * 1024 * 1024
MODIFIED_FILES = frozenset(('assets/python-source.zip', 'assets/runtime-sdk.json'))


@dataclass(frozen=True)
class BasePin:
    apk_sha256: str
    source_archive_sha256: str
    toolchain_metadata_sha256: str
    target_sha256: str
    metadata: dict


PIN = BasePin(
    apk_sha256='6af660aac1c67e5d99bbc653b1c2b9f0e429a8513e0e9ca0c497b0c37177f791',
    source_archive_sha256='67a0909e0184c6a30c87e21d1aab123c151a766af0c210be6a1c2e0688067a5c',
    toolchain_metadata_sha256='78cf8f308da654a141416db755248ca96c0d379ab265260ca08e30d472e9278f',
    target_sha256='72720ef7de7cccf920f93ae6e0b60ad651c422b9a615b4c70b8ab858660a5a16',
    metadata={
        'schema': 1, 'host_api': 1, 'identity': '3409110f9bae7ccab31e8908e25d4f7d5625671579d5fab83a2d4b8d1f674308',
        'abi': 'arm64-v8a', 'api': 33,
        'sha256': '7adf56127827f3ac8f62b62a057709fdb1d886f51096403a2b4260ee00bab4a2',
        'installed_bytes': 79683478, 'archive_bytes': 20669526,
    },
)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def json_bytes(value: dict) -> bytes:
    return (json.dumps(value, indent=2) + '\n').encode('utf-8')


def write_entry(bundle: zipfile.ZipFile, name: str, data: bytes):
    # Same canonical recipe as tools/android/prepare_python.py: fixed mtimes,
    # permissions, Unix creator and compression; independent of local paths.
    item = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    item.compress_type = zipfile.ZIP_DEFLATED
    item.external_attr = 0o100644 << 16
    item.create_system = 3
    bundle.writestr(item, data)


def archive_bytes(entries: dict[str, bytes]) -> bytes:
    result = io.BytesIO()
    with zipfile.ZipFile(result, 'w') as bundle:
        for name, data in sorted(entries.items()):
            write_entry(bundle, name, data)
    return result.getvalue()


def read_entries(path: Path) -> dict[str, bytes]:
    with zipfile.ZipFile(path) as bundle:
        infos = bundle.infolist()
        if len({info.filename for info in infos}) != len(infos):
            raise ValueError('Archive contains duplicate entries')
        for info in infos:
            name = info.filename
            if ('\\' in name or PurePosixPath(name).is_absolute() or
                    '..' in PurePosixPath(name).parts or info.is_dir()):
                raise ValueError('Archive contains an unexpected path')
        return {info.filename: bundle.read(info) for info in infos}


def validate_elf(data: bytes):
    """Check architecture/type, section bounds and defined JNI symbols, not a filename.

    Inspecting the symbol table also refuses another valid ARM object accidentally
    supplied instead of the touch runtime, including builds missing the new bridge.
    """
    if (len(data) < 64 or len(data) > MAX_OBJECT_BYTES or data[:7] != b'\x7fELF\x02\x01\x01'):
        raise ValueError('Object must be ELF64 little-endian, within the object size limit')
    kind, machine, version = struct.unpack_from('<HHI', data, 16)
    if (kind, machine, version) != (1, 183, 1):
        raise ValueError('Object must be AArch64 ELF relocatable (ET_REL)')
    shoff = struct.unpack_from('<Q', data, 40)[0]
    ehsize, _, _, shsize, count, _ = struct.unpack_from('<6H', data, 52)
    if ehsize != 64 or shsize != 64 or shoff < 64 or shoff + 64 > len(data):
        raise ValueError('Object has an invalid ELF section table')
    if count == 0:  # ELF extended section count lives in section zero's sh_size.
        count = struct.unpack_from('<Q', data, shoff + 32)[0]
    if not 1 <= count <= 65536 or shoff + count * 64 > len(data):
        raise ValueError('Object has an invalid ELF section count')
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + index * 64) for index in range(count)]
    for section in sections:
        if section[1] != 8 and section[4] + section[5] > len(data):  # SHT_NOBITS has no file payload.
            raise ValueError('Object section exceeds the file')
    defined = set()
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        _, _, _, _, offset, size, linked, _, _, entry_size = section
        if entry_size != 24 or size % entry_size or linked >= count or sections[linked][1] != 3:
            raise ValueError('Object has an invalid ELF symbol table')
        strings = sections[linked]
        table = data[strings[4]:strings[4] + strings[5]]
        for at in range(offset, offset + size, entry_size):
            name, info, _, index, _, _ = struct.unpack_from('<IBBHQQ', data, at)
            if info >> 4 not in (1, 2) or info & 15 != 2 or not 0 < index < count:
                continue
            if name >= len(table) or (end := table.find(b'\0', name)) < 0:
                raise ValueError('Object symbol has an invalid string-table offset')
            defined.add(table[name:end].decode('utf-8', errors='replace'))
    missing = REQUIRED_SYMBOLS - defined
    if missing:
        raise ValueError('Object is missing defined startup/touch JNI symbols: ' + ', '.join(sorted(missing)))


def validate_sdk(entries: dict[str, bytes], metadata: dict) -> dict:
    try:
        manifest = json.loads(entries['sdk/manifest.json'])
        identity = manifest.pop('identity')
        if identity != sha(json.dumps(manifest, sort_keys=True).encode()) or identity != metadata['identity']:
            raise ValueError('Runtime SDK identity mismatch')
        if any(manifest.get(key) != value or metadata.get(key) != value for key, value in
               (('schema', 1), ('host_api', 1), ('abi', 'arm64-v8a'), ('api', 33))):
            raise ValueError('Unsupported runtime SDK ABI/API')
        files = {name[4:]: sha(data) for name, data in entries.items()
                 if name.startswith('sdk/') and name != 'sdk/manifest.json'}
        if files != manifest['files'] or TARGET[4:] not in files:
            raise ValueError('Runtime SDK file inventory mismatch')
        if '{sdk}/' + TARGET[4:] not in manifest['link']:
            raise ValueError('Runtime SDK link recipe does not contain the touch object')
        manifest['identity'] = identity
        return manifest
    except (KeyError, TypeError, json.JSONDecodeError) as error:
        raise ValueError('Invalid runtime SDK manifest or metadata') from error


def runtime_files(root: Path) -> dict[str, Path]:
    result = {}
    if root.is_symlink() or not root.is_dir():
        raise ValueError('Runtime package must be a real directory')
    for path in root.rglob('*'):
        if path.is_symlink():
            raise ValueError('Runtime package contains a symlink')
        if path.is_file():
            result[path.relative_to(root).as_posix()] = path
    return result


def load_base(base: Path, *, pin: BasePin = PIN) -> tuple[dict, dict, dict]:
    files = runtime_files(base)
    try:
        archive = files['assets/python-source.zip']
        metadata = json.loads(files['assets/runtime-sdk.json'].read_text(encoding='utf-8'))
        if metadata != pin.metadata or file_sha(archive) != pin.source_archive_sha256:
            raise ValueError('Base runtime differs from the pinned v0.5.0 SDK')
        compiler_raw = files['assets/toolchain-apk.json'].read_bytes()
        if sha(compiler_raw) != pin.toolchain_metadata_sha256:
            raise ValueError('Base compiler metadata differs from the pinned toolchain')
        entries = read_entries(archive)
        manifest = validate_sdk(entries, metadata)
        if manifest['files'][TARGET[4:]] != pin.target_sha256:
            raise ValueError('Base touch object differs from the pinned SDK')
        return entries, metadata, json.loads(compiler_raw)
    except (KeyError, TypeError, json.JSONDecodeError) as error:
        raise ValueError('Incomplete base runtime package') from error


def source_hashes(source_root: Path) -> dict[str, str]:
    return {name: sha((source_root / name).read_bytes().replace(b'\r\n', b'\n')) for name in SOURCES}


def patched_payload(entries: dict, metadata: dict, obj: bytes) -> tuple[bytes, bytes, dict]:
    entries = dict(entries)
    manifest = json.loads(entries['sdk/manifest.json'])
    manifest.pop('identity')
    manifest['files'][TARGET[4:]] = sha(obj)
    manifest['identity'] = sha(json.dumps(manifest, sort_keys=True).encode())
    entries[TARGET] = obj
    entries['sdk/manifest.json'] = json_bytes(manifest)
    sdk = archive_bytes({name: data for name, data in entries.items() if name.startswith('sdk/')})
    updated = dict(metadata, identity=manifest['identity'], sha256=sha(sdk), archive_bytes=len(sdk),
                   installed_bytes=sum(len(data) for name, data in entries.items() if name.startswith('sdk/')))
    return archive_bytes(entries), json_bytes(updated), updated


def preparation(base: Path, obj: bytes, source_root: Path, *, pin: BasePin = PIN) -> tuple[dict, bytes, bytes]:
    validate_elf(obj)
    entries, metadata, compiler = load_base(base, pin=pin)
    if sha(obj) == pin.target_sha256:
        raise ValueError('Replacement must differ from the original touch object')
    archive, updated_json, updated = patched_payload(entries, metadata, obj)
    patch = {
        'schema': 1, 'patch_id': 'windwake-startup-touch-v1', 'license': 'MPL-2.0',
        'source_repository': 'https://github.com/GenissonEmilio/Windwake',
        'provenance': 'Authored host runtime object; no translated game code or game assets.',
        'source_hash_normalization': 'CRLF-to-LF', 'sources': source_hashes(source_root),
        'compiler': compiler, 'toolchain_metadata_sha256': pin.toolchain_metadata_sha256,
        'base': {'apk_sha256': pin.apk_sha256, 'python_source_sha256': pin.source_archive_sha256,
                 'runtime_sdk': metadata, 'object_sha256': pin.target_sha256},
        'replacement': {'entry': TARGET, 'sha256': sha(obj), 'bytes': len(obj)},
        'result': {'runtime_sdk': updated, 'python_source_sha256': sha(archive)},
    }
    return patch, archive, updated_json


def pack(base: Path, obj: Path, source_root: Path, output: Path, *, pin: BasePin = PIN) -> dict:
    raw = obj.read_bytes()
    patch, _, _ = preparation(base, raw, source_root, pin=pin)
    bundle = archive_bytes({'patch.json': json_bytes(patch), TARGET: raw})
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        if output.read_bytes() != bundle:
            raise ValueError('Patch output already exists with different content; choose a new output')
    else:
        with output.open('xb') as stream:
            stream.write(bundle)
    return dict(patch, patch_sha256=sha(bundle), patch_bytes=len(bundle))


def verified_patch(base: Path, patch_path: Path, source_root: Path, *, pin: BasePin = PIN) -> tuple[dict, bytes, bytes]:
    with zipfile.ZipFile(patch_path) as bundle:
        infos = bundle.infolist()
        if len(infos) != 2 or {item.filename for item in infos} != {'patch.json', TARGET}:
            raise ValueError('Patch may replace only the fixed touch object; unexpected entry inventory')
        if bundle.getinfo(TARGET).file_size > MAX_OBJECT_BYTES or bundle.getinfo('patch.json').file_size > 128 * 1024:
            raise ValueError('Patch payload exceeds its bounds')
        claimed = json.loads(bundle.read('patch.json'))
        obj = bundle.read(TARGET)
    expected, archive, updated = preparation(base, obj, source_root, pin=pin)
    if claimed.get('sources') != expected['sources']:
        raise ValueError('Patch source hash mismatch; regenerate from the final authored sources')
    if claimed != expected:
        raise ValueError('Patch metadata, object hash or expected result mismatch')
    return expected, archive, updated


def check(base: Path, runtime: Path, patch_path: Path, source_root: Path, *, pin: BasePin = PIN) -> dict:
    patch, archive, metadata = verified_patch(base, patch_path, source_root, pin=pin)
    before, after = runtime_files(base), runtime_files(runtime)
    if set(before) != set(after):
        raise ValueError('Patched runtime file inventory differs from the base')
    for name in sorted(before):
        if name not in MODIFIED_FILES and file_sha(before[name]) != file_sha(after[name]):
            raise ValueError('Unrelated runtime file changed: ' + name)
    if after['assets/python-source.zip'].read_bytes() != archive:
        raise ValueError('Patched Python archive differs; only the touch object and SDK manifest may change')
    if after['assets/runtime-sdk.json'].read_bytes() != metadata:
        raise ValueError('Patched runtime SDK metadata differs')
    entries = read_entries(after['assets/python-source.zip'])
    validate_sdk(entries, json.loads(metadata))
    return {'runtime_identity': patch['result']['runtime_sdk']['identity'],
            'python_source_sha256': sha(archive), 'patch_sha256': file_sha(patch_path),
            'object_sha256': patch['replacement']['sha256'], 'unchanged_runtime_files': len(before) - 2,
            'sources': patch['sources']}


def apply(base: Path, patch_path: Path, source_root: Path, output: Path, *, pin: BasePin = PIN) -> dict:
    base, output = base.resolve(), output.resolve()
    if output.is_relative_to(base) or base.is_relative_to(output):
        raise ValueError('Patched runtime output must be separate from the base')
    patch, archive, metadata = verified_patch(base, patch_path, source_root, pin=pin)
    if output.exists():
        return check(base, output, patch_path, source_root, pin=pin)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.windwake-runtime-', dir=output.parent) as temporary:
        staging = Path(temporary).resolve() / 'runtime'
        if not staging.is_relative_to(output.parent):
            raise ValueError('Runtime staging path is outside the intended output directory')
        shutil.copytree(base, staging)
        (staging / 'assets/python-source.zip').write_bytes(archive)
        (staging / 'assets/runtime-sdk.json').write_bytes(metadata)
        check(base, staging, patch_path, source_root, pin=pin)
        os.rename(staging, output)
    return {'runtime_identity': patch['result']['runtime_sdk']['identity'], 'python_source_sha256': sha(archive),
            'patch_sha256': file_sha(patch_path), 'object_sha256': patch['replacement']['sha256']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('pack', 'apply', 'check'):
        command = commands.add_parser(name)
        command.add_argument('--base', type=Path, required=True)
        command.add_argument('--source-root', type=Path, default=ROOT)
        if name == 'pack':
            command.add_argument('--object', type=Path, required=True)
        else:
            command.add_argument('--patch', type=Path, required=True)
        command.add_argument('--runtime' if name == 'check' else '--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == 'pack':
            result = pack(args.base, args.object, args.source_root, args.out)
        elif args.command == 'apply':
            result = apply(args.base, args.patch, args.source_root, args.out)
        else:
            result = check(args.base, args.runtime, args.patch, args.source_root)
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, str(error) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
