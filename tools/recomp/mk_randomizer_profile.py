#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Maintainer tool: derive hash-only randomizer hook profiles from private original/reviewed RPX files."""
import argparse
import json
import struct
from pathlib import Path

import builds
from recomp import Recompiler
from randomizer_compat import (digest, guard_digest, hook_digest, import_digest, text_slice, check, range_profile,
                               CompatibilityError, PROFILE_FORMAT)
from rpx import Rpx

# the profile of each build (randomizer_compat.PROFILES); EU seeds come from the in-app randomizer's port of the patches
PROFILE_IDS = {'USA': 'twwhd-randomizer-usa-v0-3', 'EU': 'twwhd-randomizer-eu-v0-2'}


def make(base_path, reviewed_paths, reviewed_functions):
    reference = Recompiler(str(base_path))  # exact supported-build SHA check (its text: relocations applied)
    if reference.build.name not in PROFILE_IDS:
        raise ValueError('profile reference must be the original USA or EU RPX')
    rpx = reference.p.rpx
    hooks, _ = builds.read_hooks(builds.hook_files(), reference.build)
    starts = sorted({reference.func_of(address) for _, _, address, _ in hooks})
    points = sorted({address for _, _, address, _ in hooks})
    profile = dict(format=PROFILE_FORMAT, id=PROFILE_IDS[reference.build.name], base_sha256=reference.build.sha256,
                   hooks_sha256=hook_digest(hooks), entry_point=reference.p.entry,
                   imports_sha256=import_digest(rpx),
                   sections=[dict(name=s.name, address=s.addr, size=s.size, type=s.type, flags=s.flags & 7)
                             for s in rpx.sections if s.flags & 2 and s.size],
                   **range_profile(rpx, points, [(a, reference.func_end(a)) for a in starts]))
    profile['guard_sha256'] = [profile['guard_sha256']]
    seen = set()
    for path in reviewed_paths:
        patched = Rpx(str(path))
        patched.apply_internal_relocations()  # what the port loads and translates (issue #383)
        # Hook words must stay canonical. A tiny hooked body (below 64 bytes) can change only when its
        # function is on the review list; the profile then accepts the whole hook-word/tiny-body aggregate
        # of this input as a variant, never a digest of one tiny body.
        for address in profile['points']:
            if text_slice(patched, address, address+4) != text_slice(rpx, address, address+4):
                raise CompatibilityError('randomizer clash: %s changes the hook instruction at %08X' % (path, address))
        for function in profile['tiny_functions']:
            start, end = function['start'], function['end']
            if text_slice(patched, start, end) != text_slice(rpx, start, end):
                if start not in reviewed_functions:
                    raise CompatibilityError('randomizer clash: %s changes the tiny hooked function %08X' % (path, start))
                seen.add(start)
        guard = guard_digest(patched, profile['points'], profile['tiny_functions'])
        if guard not in profile['guard_sha256']:
            profile['guard_sha256'].append(guard)
        layout = dict(profile, functions=[])
        check(patched, layout, hooks, struct.unpack_from('>I', path.read_bytes(), 24)[0])
        for function in profile['functions']:
            actual = digest(text_slice(patched, function['start'], function['end']))
            if actual == function['sha256'][0]:
                continue
            if function['start'] not in reviewed_functions:
                raise ValueError('%s changes unreviewed hook function %08X' % (path, function['start']))
            seen.add(function['start'])
            if actual not in function['sha256']:
                function['sha256'].append(actual)
    if seen != reviewed_functions:
        raise ValueError('review list differs from the functions actually changed by these inputs')
    return profile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('original_rpx', type=Path)
    ap.add_argument('output', type=Path)
    ap.add_argument('--reviewed-rpx', type=Path, action='append', default=[])
    ap.add_argument('--reviewed-functions', default='')
    a = ap.parse_args()
    reviewed = {int(n, 16) for n in a.reviewed_functions.split(',') if n.strip()}
    profile = make(a.original_rpx, a.reviewed_rpx, reviewed)
    a.output.write_text(json.dumps(profile, indent=2)+'\n', encoding='utf-8')
    print('%d hook points, %d containing functions; wrote hashes/layout only' %
          (len(profile['points']), len(profile['functions'])+len(profile['tiny_functions'])))


if __name__ == '__main__':
    main()
