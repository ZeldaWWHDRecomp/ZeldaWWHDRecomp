"""Compile the authored touch SDK object using the APK's pinned ARM clang on Android.

No NDK download, game files, app installation or app data mutation. The device
workspace is fixed and ownership-marked; only its authored build inputs are reset.
The output object and .recipe.json receipt are published only after validation.
"""
from __future__ import annotations

import argparse
import gzip
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess
import tarfile
import tempfile
import zipfile

import runtime_patch


ROOT = runtime_patch.ROOT
REMOTE = '/data/local/tmp/windwake-runtime-build'
MARKER = 'windwake-runtime-builder-v1\n'
SOURCE = 'runtime/src/mods/context_touch.cpp'
TOOLCHAIN_PREFIXES = ('sysroot/usr/include/', 'lib/clang/20/include/')


def safe_archive_name(name: str):
    path = PurePosixPath(name)
    if not name or path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
        raise ValueError('Unsafe build archive path: ' + name)


def project_inputs(root: Path) -> dict[str, bytes]:
    root = root.resolve()
    entries = {}
    for folder in (root / 'runtime/include', root / 'runtime/src'):
        if not folder.resolve().is_relative_to(root) or not folder.is_dir():
            raise ValueError('Runtime source directory is missing or outside the repository')
        for path in folder.rglob('*'):
            if not path.resolve().is_relative_to(root):
                raise ValueError('Runtime source symlink escapes the repository: ' + str(path))
            # Do not follow directory symlinks (including loops); source inputs
            # must be an enumerable, reviewable tree rooted in this repository.
            if path.is_symlink() and path.is_dir():
                raise ValueError('Runtime source contains a directory symlink: ' + str(path))
            if path.is_file() and (folder.name == 'include' or path.suffix in ('.h', '.hpp', '.inc')):
                name = path.relative_to(root).as_posix()
                safe_archive_name(name)
                entries[name] = path.read_bytes().replace(b'\r\n', b'\n')
    source = root / SOURCE
    if not source.resolve().is_relative_to(root) or not source.is_file():
        raise ValueError('Touch source is missing or outside the repository')
    entries[SOURCE] = source.read_bytes().replace(b'\r\n', b'\n')
    return entries


def build_archive(root: Path, toolchain_zip: Path, expected_sha256: str, output: Path) -> dict:
    if runtime_patch.file_sha(toolchain_zip) != expected_sha256:
        raise ValueError('Toolchain data ZIP differs from the pinned compiler metadata')
    entries = project_inputs(root)
    selected = {prefix: 0 for prefix in TOOLCHAIN_PREFIXES}
    with zipfile.ZipFile(toolchain_zip) as bundle:
        names = set()
        for info in bundle.infolist():
            name = info.filename
            prefix = next((prefix for prefix in TOOLCHAIN_PREFIXES if name.startswith(prefix)), None)
            if prefix is None or info.is_dir():
                continue
            safe_archive_name(name)
            if name in names or (info.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Toolchain headers contain a duplicate or symbolic link')
            names.add(name)
            entries['toolchain/' + name] = bundle.read(info)
            selected[prefix] += 1
    if not all(selected.values()):
        raise ValueError('Toolchain data ZIP lacks sysroot or clang resource headers')
    output.parent.mkdir(parents=True, exist_ok=True)
    # Canonical tar and gzip metadata make the input hash independent of source
    # checkout mtimes, owner names, Windows line endings and output filename.
    with output.open('wb') as stream:
        with gzip.GzipFile(fileobj=stream, mode='wb', filename='', mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w|', format=tarfile.USTAR_FORMAT) as archive:
                for name, raw in sorted(entries.items()):
                    item = tarfile.TarInfo(name)
                    item.size = len(raw); item.mode = 0o644
                    item.uid = item.gid = item.mtime = 0
                    item.uname = item.gname = ''
                    archive.addfile(item, io.BytesIO(raw))
    return {'sha256': runtime_patch.file_sha(output), 'bytes': output.stat().st_size,
            'entries': len(entries), 'toolchain_header_entries': selected,
            'toolchain_data_sha256': expected_sha256,
            'project_inputs_sha256': {name: runtime_patch.sha(raw) for name, raw in sorted(entries.items())
                                      if name.startswith('runtime/')}}


def native_library_directory(pm_output: str) -> str:
    paths = [line[len('package:'):].strip() for line in pm_output.splitlines() if line.startswith('package:')]
    bases = [path for path in paths if PurePosixPath(path).name == 'base.apk']
    if len(bases) != 1:
        raise ValueError('Installed package must have exactly one base.apk')
    apk = bases[0]
    if (not re.fullmatch(r'/data/app/[A-Za-z0-9_~+/=.\-]+/base\.apk', apk) or
            '..' in PurePosixPath(apk).parts):
        raise ValueError('Installed APK path is not under the expected /data/app directory')
    return str(PurePosixPath(apk).parent / 'lib/arm64')


def compiler_flags() -> list[str]:
    return [
        '--driver-mode=g++', '--target=aarch64-linux-android33',
        '--sysroot=' + REMOTE + '/toolchain/sysroot',
        '-resource-dir=' + REMOTE + '/toolchain/lib/clang/20',
        '-std=c++20', '-O3', '-DNDEBUG', '-DANDROID', '-DWWHD_HAS_VULKAN=1',
        '-DVK_NO_PROTOTYPES', '-DWWHD_EDITOR_MCP=0', '-fPIC', '-ffp-contract=off',
        '-fsigned-char', '-fdata-sections', '-ffunction-sections', '-funwind-tables',
        '-fstack-protector-strong', '-D_FORTIFY_SOURCE=2', '-fvisibility=hidden',
        '-ffile-prefix-map=' + REMOTE + '=.', '-fmacro-prefix-map=' + REMOTE + '=.',
        '-I' + REMOTE + '/runtime/include', '-I' + REMOTE + '/runtime/src',
        '-I' + REMOTE + '/runtime/src/gx2',
        '-c', REMOTE + '/' + SOURCE, '-o', REMOTE + '/context_touch.cpp.o',
    ]


class Adb:
    def __init__(self, executable: Path, serial: str):
        self.executable, self.serial = executable, serial

    def run(self, *args: str, check=True, timeout=120):
        result = subprocess.run([str(self.executable), '-s', self.serial, *args],
                                capture_output=True, timeout=timeout)
        if check and result.returncode:
            raise RuntimeError('ADB command failed: ' + result.stderr.decode(errors='replace').strip() +
                               '\n' + result.stdout.decode(errors='replace').strip())
        return result

    def shell(self, *args: str, check=True, timeout=120):
        return self.run('shell', shlex.join(args), check=check, timeout=timeout)

    def text(self, *args: str) -> str:
        return self.shell(*args).stdout.decode('utf-8', errors='replace').strip()


def prepare_remote(adb: Adb):
    existed = adb.shell('test', '-e', REMOTE, check=False).returncode == 0
    if not existed:
        adb.shell('mkdir', REMOTE)
    if adb.shell('test', '-L', REMOTE, check=False).returncode == 0 or adb.text('realpath', REMOTE) != REMOTE:
        raise ValueError('Build workspace does not resolve to the fixed authored directory')
    marker = REMOTE + '/.windwake-runtime-builder-v1'
    if existed:
        if (adb.shell('test', '-L', marker, check=False).returncode == 0 or
                adb.shell('cat', marker, check=False).stdout.decode(errors='replace') != MARKER):
            raise ValueError('Refusing to reuse an existing device directory without the authored build marker')
    else:
        adb.run('shell', 'printf %s ' + shlex.quote(MARKER) + ' > ' + shlex.quote(marker))
    # Only reset these two source directories after checking their resolved
    # absolute paths. No app installation, save, cache or other folder is touched.
    for name in ('runtime', 'toolchain'):
        child = REMOTE + '/' + name
        if adb.shell('test', '-e', child, check=False).returncode == 0:
            if adb.shell('test', '-L', child, check=False).returncode == 0 or adb.text('realpath', child) != child:
                raise ValueError('Build input directory resolves outside the authored workspace')
            adb.shell('rm', '-rf', '--', child)
    adb.shell('rm', '-f', '--', REMOTE + '/context_touch.cpp.o')


def remote_sha(adb: Adb, path: str) -> str:
    result = adb.text('sha256sum', path).split()
    if not result or not re.fullmatch('[0-9a-f]{64}', result[0]):
        raise ValueError('Device did not return a SHA-256 checksum for ' + path)
    return result[0]


def invoke_driver(adb: Adb, driver: str, *arguments: str, timeout=120):
    # Execute directly: the APK driver already uses $ORIGIN for its libraries.
    # Toybox env can misread randomized /data/app paths containing '=' as an
    # assignment and then try to execute '--version' instead of the driver.
    return adb.shell(driver, *arguments, timeout=timeout)


def build(serial: str, package: str, base: Path, output: Path, source_root: Path = ROOT) -> dict:
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+', package):
        raise ValueError('Invalid Android package identifier')
    base, output, source_root = base.resolve(), output.absolute(), source_root.resolve()
    if output.is_symlink():
        raise ValueError('Output object may not be a symbolic link')
    _, _, compiler = runtime_patch.load_base(base)
    expected = compiler['sha256'][compiler['executables']['clang']]
    executable = ROOT / 'build/android-sdk/platform-tools' / ('adb.exe' if os.name == 'nt' else 'adb')
    if not executable.is_file():
        raise ValueError('ADB not found; run bootstrap_android.py first')
    adb = Adb(executable, serial)
    library = native_library_directory(adb.text('pm', 'path', package))
    driver = library + '/' + compiler['executables']['clang']
    if remote_sha(adb, driver) != expected:
        raise ValueError('Installed clang differs from the pinned APK toolchain')
    version = invoke_driver(adb, driver, '--version').stdout.decode('utf-8', errors='replace').strip()
    version = '\n'.join(line for line in version.splitlines() if not line.startswith('InstalledDir:'))
    output.parent.mkdir(parents=True, exist_ok=True)
    archive_path = output.with_suffix('.inputs.tar.gz')
    receipt_path = output.with_suffix('.recipe.json')
    inputs = build_archive(source_root, base / 'assets/toolchain-data.zip', compiler['data_sha256'], archive_path)
    prepare_remote(adb)
    adb.run('push', str(archive_path), REMOTE + '/inputs.tar.gz')
    if remote_sha(adb, REMOTE + '/inputs.tar.gz') != inputs['sha256']:
        raise ValueError('Transferred build inputs checksum mismatch')
    adb.shell('tar', '-xzf', REMOTE + '/inputs.tar.gz', '-C', REMOTE)
    flags = compiler_flags()
    compiled = invoke_driver(adb, driver, *flags, timeout=300)
    remote_digest = remote_sha(adb, REMOTE + '/context_touch.cpp.o')
    with tempfile.TemporaryDirectory(prefix='.windwake-object-', dir=output.parent) as temporary:
        pending = Path(temporary) / 'context_touch.cpp.o'
        adb.run('pull', REMOTE + '/context_touch.cpp.o', str(pending))
        raw = pending.read_bytes()
        runtime_patch.validate_elf(raw)
        if runtime_patch.sha(raw) != remote_digest:
            raise ValueError('Pulled object checksum mismatch')
        compiled_sources = {name: inputs['project_inputs_sha256'][name] for name in runtime_patch.SOURCES}
        if runtime_patch.source_hashes(source_root) != compiled_sources:
            raise ValueError('Authored touch sources changed during compilation; rebuild before publishing the object')
        receipt = {
            'schema': 1, 'license': 'MPL-2.0', 'target': runtime_patch.TARGET,
            'workspace': REMOTE, 'source_hash_normalization': 'CRLF-to-LF',
            'sources': compiled_sources, 'inputs': inputs,
            'compiler': {'driver': compiler['executables']['clang'], 'sha256': expected,
                         'identity': compiler['compiler_identity'], 'llvm_revision': compiler['llvm_revision'],
                         'version': version},
            'flags': flags, 'object_sha256': remote_digest, 'object_bytes': len(raw),
            'diagnostics': compiled.stderr.decode('utf-8', errors='replace'),
        }
        pending_receipt = Path(temporary) / 'recipe.json'
        pending_receipt.write_bytes(runtime_patch.json_bytes(receipt))
        # All compiler/object checks passed before either output is replaced.
        os.replace(pending, output)
        os.replace(pending_receipt, receipt_path)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--package', default='dev.windwake.android')
    parser.add_argument('--base', type=Path, default=ROOT / 'build/vendor/runtime-v0.5.0')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/native-patch/context_touch.cpp.o')
    parser.add_argument('--source-root', type=Path, default=ROOT)
    args = parser.parse_args()
    try:
        receipt = build(args.serial, args.package, args.base, args.out, args.source_root)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired, zipfile.BadZipFile) as error:
        parser.exit(1, str(error) + '\n')
    print(json.dumps({'object': str(args.out), 'object_sha256': receipt['object_sha256'],
                      'object_bytes': receipt['object_bytes'], 'recipe': str(args.out.with_suffix('.recipe.json')),
                      'inputs_sha256': receipt['inputs']['sha256'], 'compiler_sha256': receipt['compiler']['sha256'],
                      'sources': receipt['sources'], 'diagnostics': receipt['diagnostics']}, indent=2))


if __name__ == '__main__':
    main()
