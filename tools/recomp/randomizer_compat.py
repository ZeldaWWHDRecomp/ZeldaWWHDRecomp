# SPDX-License-Identifier: MPL-2.0
"""Automatic compatibility checks for randomized RPX files (USA; EU from the in-app randomizer's port of its patches).

The shipped profile contains hashes and layout facts, never game instructions. It
covers the port's hook sites and their containing functions. It is a hook-clash
check, not a claim that every seed has been played through.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent)]

import builds
from rpx import Rpx

TITLE_ID = '0005000010143599'
RPX_LIMITS = dict(file=32 << 20, section=32 << 20, inflated=64 << 20,
                  sections=128, symbols=8192, relocations=500000,
                  name=1024, name_bytes=1 << 20)
PROFILE = Path(__file__).with_name('randomizer-usa.json')
# one profile per build the randomizer can be run on, chosen by the randomized RPX's entry point (the randomizer
# never moves it; the releases' differ)
PROFILES = [PROFILE, Path(__file__).with_name('randomizer-eu.json')]
# Short instruction sequences (including li/blr) must not expose enumerable digests.
# Group every body below 64 bytes with all hook words in one canonical aggregate.
MIN_FUNCTION_HASH_BYTES = 64


class CompatibilityError(ValueError):
    pass


def digest(value):
    return hashlib.sha256(value).hexdigest()


def hook_digest(hooks):
    points = sorted({('site' if site else 'function', canon) for site, canon, _, _ in hooks})
    return digest(json.dumps(points, separators=(',', ':')).encode())


def import_digest(rpx):
    imports = sorted((s.import_lib, s.name, s.value, s.import_kind) for s in rpx.symbols
                     if s.import_lib and s.type != 3)
    return digest(json.dumps(imports, separators=(',', ':')).encode())


def text_slice(rpx, start, end):
    text = rpx.by_name['.text']
    offset = start-text.addr
    if offset < 0 or end < start or end-text.addr > len(text.data):
        raise CompatibilityError('hook range %08X..%08X lies outside randomized text' % (start, end))
    return text.data[offset:end-text.addr]


def guard_digest(rpx, points, tiny_functions):
    """One address/length-bound commitment; never publish per-word/tiny-body hashes."""
    value = hashlib.sha256(b'WWHD randomizer hook/tiny aggregate v2\0')
    for address in points:
        value.update(b'P' + struct.pack('>II', address, 4))
        value.update(text_slice(rpx, address, address+4))
    for function in tiny_functions:
        start, end = function['start'], function['end']
        value.update(b'F' + struct.pack('>II', start, end-start))
        value.update(text_slice(rpx, start, end))
    return value.hexdigest()


def range_profile(rpx, points, ranges):
    """Split at 64 bytes; all smaller bodies share the hook-word aggregate."""
    tiny = [dict(start=a, end=b) for a, b in ranges if b-a < MIN_FUNCTION_HASH_BYTES]
    functions = [dict(start=a, end=b, sha256=[digest(text_slice(rpx, a, b))])
                 for a, b in ranges if b-a >= MIN_FUNCTION_HASH_BYTES]
    return dict(points=points, tiny_functions=tiny, functions=functions,
                minimum_function_hash_bytes=MIN_FUNCTION_HASH_BYTES,
                guard_sha256=guard_digest(rpx, points, tiny))


def base_build(profile):
    """the build of the game a profile was made from (tools/recomp/builds.py), or None"""
    return builds.by_sha256(profile.get('base_sha256', ''))


def check(rpx, profile, hooks, entry):
    if profile.get('format') != 2 or base_build(profile) is None:
        raise CompatibilityError('randomizer compatibility profile does not match a supported build')
    if hook_digest(hooks) != profile['hooks_sha256']:
        raise CompatibilityError('randomizer compatibility profile needs updating for this port hook set')
    if entry != profile['entry_point']:
        raise CompatibilityError('randomized RPX moved the entry point')
    loaded = [s.name for s in getattr(rpx, 'sections', rpx.by_name.values()) if s.flags & 2 and s.size]
    if len(loaded) != len(set(loaded)) or set(loaded) != {s['name'] for s in profile['sections']}:
        raise CompatibilityError('randomized RPX changed the loaded section set')
    for expected in profile['sections']:
        section = rpx.by_name.get(expected['name'])
        if (section is None or section.addr != expected['address'] or
                section.type != expected['type'] or section.flags & 7 != expected['flags']):
            raise CompatibilityError('randomized RPX changed the %s section layout' % expected['name'])
        if section.name == '.text':
            if not expected['size'] <= section.size <= 0x03000000-section.addr or section.size & 3:
                raise CompatibilityError('randomized RPX has an unsupported text extent')
        elif section.size != expected['size']:
            raise CompatibilityError('randomized RPX changed the %s section size' % expected['name'])
    if import_digest(rpx) != profile['imports_sha256']:
        raise CompatibilityError('randomized RPX changed the native import layout')
    if profile.get('minimum_function_hash_bytes') != MIN_FUNCTION_HASH_BYTES:
        raise CompatibilityError('randomizer compatibility profile has an unsupported hash threshold')
    if profile['points'] != sorted({a for _, _, a, _ in hooks}):
        raise CompatibilityError('randomizer compatibility profile has incomplete hook coverage')
    if any(not 0 < f['end']-f['start'] < MIN_FUNCTION_HASH_BYTES for f in profile['tiny_functions']):
        raise CompatibilityError('randomizer compatibility profile has an invalid tiny-function range')
    if any(f['end']-f['start'] < MIN_FUNCTION_HASH_BYTES for f in profile['functions']):
        raise CompatibilityError('randomizer compatibility profile exposes a tiny-function digest')
    if guard_digest(rpx, profile['points'], profile['tiny_functions']) != profile['guard_sha256']:
        raise CompatibilityError('randomizer clash: hook instruction or tiny hooked function changed')
    changed = []
    for function in profile['functions']:
        actual = digest(text_slice(rpx, function['start'], function['end']))
        if actual not in function['sha256']:
            raise CompatibilityError('randomizer clash: unreviewed change in hooked function %08X' % function['start'])
        if actual != function['sha256'][0]:
            changed.append('%08X' % function['start'])
    return dict(profile=profile['id'], title_id=TITLE_ID, reviewed_changed_functions=changed,
                hook_points_checked=len(profile['points']), functions_checked=len(profile['functions'])+len(profile['tiny_functions']))


def randomized_build(base, rpx_sha256):
    """the identity of a randomized RPX made from `base`: base's address map, the randomizer's title, its own hash"""
    if base.canonical:
        return builds.Build(dict(name=base.name, title_id=TITLE_ID, rpx_sha256=rpx_sha256))
    for path in sorted(Path(builds.HERE, 'builds').glob('*.json')):
        d = json.loads(path.read_text(encoding='utf-8'))
        if d.get('rpx_sha256') == base.sha256:
            return builds.Build(dict(d, title_id=TITLE_ID, rpx_sha256=rpx_sha256))
    raise CompatibilityError('no address map for the %s build' % base.name)


def identify(path, profile=None, expected_sha256=None):
    """Return a private USA identity map and audit for this compatible randomized RPX.

    This does not add an unknown hash to the normal build registry. Setup checks
    the folder title; the child compiler can additionally pin the parent's hash.
    """
    path = Path(path)
    if path.stat().st_size > RPX_LIMITS['file']:
        raise CompatibilityError('randomizer RPX input exceeds size limit')
    with path.open('rb') as stream:
        header = stream.read(0x34)
    if (len(header) != 0x34 or header[:6] != b'\x7fELF\x01\x02' or
            struct.unpack_from('>H', header, 18)[0] != 20):
        raise CompatibilityError('randomizer input is not a 32-bit big-endian PowerPC RPX')
    actual = builds.file_sha256(path)
    if expected_sha256 is not None and actual != expected_sha256:
        raise CompatibilityError('randomized RPX changed after the setup compatibility check')
    if profile is None:
        entry = struct.unpack_from('>I', header, 0x18)[0]
        for candidate in PROFILES:
            if candidate.is_file():
                p = json.loads(candidate.read_text(encoding='utf-8'))
                if p.get('entry_point') == entry:
                    profile = p
                    break
        if profile is None:  # (the USA profile's checks say what does not match)
            profile = json.loads(PROFILE.read_text(encoding='utf-8'))
    base = base_build(profile) or builds.canonical_build()  # (check() refuses a profile of an unknown build)
    hooks, _ = builds.read_hooks(builds.hook_files(), base)
    try:
        rpx = Rpx(str(path), limits=RPX_LIMITS)
        if rpx.buffer_sha256 != actual:
            raise CompatibilityError('randomized RPX changed while parsing its input buffer')
        original_text = next(section for section in profile['sections'] if section['name']=='.text')
        if actual == profile['base_sha256'] or rpx.by_name['.text'].size <= original_text['size']:
            raise CompatibilityError('randomizer folder does not contain the expected extended randomized game code')
        report = check(rpx, profile, hooks, rpx.entry)
    except (struct.error, zlib.error, IndexError, KeyError) as error:
        raise CompatibilityError('randomizer input has an invalid RPX section or symbol table') from error
    if builds.file_sha256(path) != actual:
        raise CompatibilityError('randomized RPX changed during compatibility checking')
    build = randomized_build(base, actual)
    build.randomizer = True
    report['rpx_sha256'] = actual
    build.randomizer_report = report
    return build
