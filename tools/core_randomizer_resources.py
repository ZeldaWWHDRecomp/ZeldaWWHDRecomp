"""Public release inputs for the experimental source-core builder, never seeds."""
import hashlib
import json
import posixpath
from pathlib import Path
import re

CORE = 'examples/guest-mods/twwhd-randomizer-core'
DESCRIPTOR = 'core-randomizer-resources.json'
DEPENDENCIES = ('tools/rpx.py', 'tools/recomp/builds.py', 'tools/recomp/ppc2c.py',
                'tools/recomp/randomizer_compat.py', 'tools/recomp/source_core_compat.py',
                'tools/recomp/randomizer-usa.json', 'tools/recomp/source-core-usa.json',
                'tools/guestmod/build_guest_mod.py', 'tools/guestmod/guestmod.py',
                'tools/core_randomizer.py', 'tools/core_randomizer_resources.py')
FORBIDDEN = {'seed_init_data.h', 'seed_reward_data.h', 'seed_boss_data.h', 'seed_resources.h'}


def relative(name):
    if (not isinstance(name, str) or not name or '\\' in name or ':' in name or '\0' in name or
            any(part in ('', '.', '..') for part in name.split('/')) or Path(name).is_absolute()):
        raise ValueError('invalid public resource path')
    return name


def read(path, maximum=4 << 20):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > maximum:
        raise ValueError('invalid public resource file: ' + str(path))
    data = path.read_bytes()
    if len(data) > maximum:
        raise ValueError('oversized public resource file')
    return data


def inventory(root, paths):
    result = {}
    for name in sorted(paths):
        relative(name)
        path = root / name
        current = root
        for part in name.split('/'):
            current = current / part
            if current.is_symlink():
                raise ValueError('linked public resource path')
        result[name] = hashlib.sha256(read(path)).hexdigest()
    return result


def tree_digest(files, prefix, compile_only=False):
    digest = hashlib.sha256()
    for name, value in sorted(files.items()):
        if name.startswith(prefix + '/'):
            local = name[len(prefix)+1:]
            if compile_only and Path(local).suffix not in ('.c', '.h'):
                continue
            digest.update(local.encode() + b'\0' + bytes.fromhex(value))
    return digest.hexdigest()


def validate_guest_includes(root, files):
    """Check quoted dependencies against the shipped inventory, not the checkout.

    Compiler-provided angle-bracket headers remain the selected compiler's job.
    The four generated seed headers are injected only during private conversion.
    """
    sdk = 'sdk/guest/include'
    generated = {CORE + '/' + name for name in FORBIDDEN}
    for name in sorted(files):
        if Path(name).suffix not in ('.c', '.h') or not name.startswith((CORE+'/', sdk+'/')):
            continue
        text = read(root/name).decode('utf-8')
        for dependency in re.findall(r'^\s*#\s*include\s*"([^"\r\n]+)"', text, re.M):
            candidates = {posixpath.normpath(prefix+'/'+dependency)
                          for prefix in (posixpath.dirname(name), CORE, sdk)}
            if not candidates.intersection(set(files) | generated):
                raise ValueError(f'public guest header missing: {dependency} (required by {name})')


def bundle(repo, release, version):
    """Release packager only: copy an explicit checked-in public source allowlist."""
    repo, release = Path(repo), Path(release)
    if not isinstance(version, str) or not version or len(version) > 128:
        raise ValueError('invalid release version')
    names = json.loads(read(repo / CORE / 'public-files.json'))
    if (not isinstance(names, list) or not names or len(names) != len(set(names)) or
            any('/' in relative(name) or name in FORBIDDEN or (name.startswith('test_') or name.endswith('_test.c')) or
                (Path(name).suffix not in ('.c', '.h', '.py') and not name.startswith('LICENSE'))
                for name in names)):
        raise ValueError('invalid public source allowlist')
    paths = [CORE + '/' + name for name in names] + [CORE + '/public-files.json']
    for name in paths:
        data = read(repo / name)
        target = release / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    paths += list(DEPENDENCIES)
    paths += [p.relative_to(release).as_posix() for p in (release/'sdk/guest/include').rglob('*') if p.is_file()]
    paths += [p.relative_to(release).as_posix() for p in (release/'tools/recomp').glob('hooks*.txt')]
    paths += [p.relative_to(release).as_posix() for p in (release/'tools/recomp/builds').glob('*.json')]
    files = inventory(release, paths)
    validate_guest_includes(release, files)
    if not any(name.startswith('sdk/guest/include/') for name in files):
        raise ValueError('release SDK headers missing')
    descriptor = {'format_version': 1, 'source': CORE, 'sdk': 'sdk/guest/include', 'files': files,
                  'provenance': {'release_version': version,
                                 'source_sha256': tree_digest(files, CORE, True),
                                 'sdk_sha256': tree_digest(files, 'sdk/guest/include')}}
    (release / DESCRIPTOR).write_text(json.dumps(descriptor, indent=2) + '\n')
    return descriptor


def load(descriptor):
    descriptor = Path(descriptor).absolute()
    root = descriptor.parent
    data = json.loads(read(descriptor, 1 << 20))
    if (not isinstance(data, dict) or set(data) != {'format_version', 'source', 'sdk', 'files', 'provenance'} or
            type(data['format_version']) is not int or data['format_version'] != 1 or
            data['source'] != CORE or data['sdk'] != 'sdk/guest/include' or
            not isinstance(data['files'], dict) or not 1 <= len(data['files']) <= 4096):
        raise ValueError('invalid core builder resources')
    required = set(DEPENDENCIES) | {CORE+'/mod.c', CORE+'/seed_prepare.py', CORE+'/seed_package.py',
                                         CORE+'/public-files.json', CORE+'/LICENSE-TWWHD-Randomizer.txt',
                                         'sdk/guest/include/wwhd_guest.h'}
    if not required <= set(data['files']):
        raise ValueError('missing public core builder resources')
    for name, value in data['files'].items():
        relative(name)
        if not isinstance(value, str) or len(value) != 64 or any(c not in '0123456789abcdef' for c in value):
            raise ValueError('invalid public resource digest')
    if inventory(root, data['files']) != data['files']:
        raise ValueError('public release resources changed')
    validate_guest_includes(root, data['files'])
    if (not isinstance(data['provenance'], dict) or
            set(data['provenance']) != {'release_version', 'source_sha256', 'sdk_sha256'} or
            not isinstance(data['provenance']['release_version'], str) or not data['provenance']['release_version'] or
            len(data['provenance']['release_version']) > 128):
        raise ValueError('invalid release provenance')
    for key, prefix, only in [('source_sha256', CORE, True), ('sdk_sha256', 'sdk/guest/include', False)]:
        if data['provenance'][key] != tree_digest(data['files'], prefix, only):
            raise ValueError('release provenance mismatch')
    return root, data
