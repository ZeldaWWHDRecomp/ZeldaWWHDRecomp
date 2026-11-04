# SPDX-License-Identifier: MPL-2.0
"""Narrow data-extraction policy for the experimental source core mod.

The core module executes original USA code, not randomized RPX code. Its private
seed-data extraction therefore has a separate explicit review extension. Normal
randomizer_compat.identify() and the randomized-executable setup path remain
unchanged. Accepting seed data is not a claim of complete source-port coverage.
"""
from copy import deepcopy
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import sys

# Resolve shipped siblings even with the release's isolated Python.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import randomizer_compat as compat

POLICY = Path(__file__).with_name('source-core-usa.json')


@dataclass(frozen=True)
class SourceSeedIdentity:
    # Deliberately no Build address map: this is data input, never an executable
    # identity that the recompiler/setup can use in place of normal identify().
    sha256: str
    randomizer_report: dict
    source_core_data: bool = True


def reviewed_profile(base_bytes, policy):
    """Extend exact full-function hashes only; never waive guards or layout checks."""
    if (not isinstance(policy, dict) or set(policy) != {'format', 'id', 'base_profile_sha256', 'functions'} or
            type(policy.get('format')) is not int or policy['format'] != 1 or
            not isinstance(policy.get('id'), str) or not policy['id'] or
            hashlib.sha256(base_bytes).hexdigest() != policy.get('base_profile_sha256')):
        raise compat.CompatibilityError('source-core policy does not match the reviewed base profile')
    base = json.loads(base_bytes)
    result = deepcopy(base)
    functions = {(function['start'], function['end']): function for function in result['functions']}
    extensions = policy.get('functions')
    if not isinstance(extensions, list) or not extensions or len(extensions) > 32:
        raise compat.CompatibilityError('invalid source-core reviewed function list')
    seen = set()
    for extension in extensions:
        if not isinstance(extension, dict) or set(extension) != {'start', 'end', 'sha256', 'review'}:
            raise compat.CompatibilityError('invalid source-core function review')
        start, end = extension['start'], extension['end']
        if (type(start) is not int or type(end) is not int or start & 3 or end & 3 or
                end - start < compat.MIN_FUNCTION_HASH_BYTES or (start, end) not in functions or
                (start, end) in seen or not isinstance(extension['review'], str) or not extension['review']):
            raise compat.CompatibilityError('source-core review must match a complete non-tiny function')
        seen.add((start, end))
        hashes = extension['sha256']
        if not isinstance(hashes, list) or not hashes or len(hashes) > 8:
            raise compat.CompatibilityError('invalid source-core function hashes')
        for digest in hashes:
            if (not isinstance(digest, str) or len(digest) != 64 or
                    any(c not in '0123456789abcdef' for c in digest)):
                raise compat.CompatibilityError('invalid source-core function hash')
            if hashes.count(digest) != 1:
                raise compat.CompatibilityError('duplicate source-core function hash')
            if digest not in functions[(start, end)]['sha256']:
                functions[(start, end)]['sha256'].append(digest)
    # Everything else, including every hook word/tiny-function aggregate, stays
    # byte-for-byte semantically equal to the normal profile.
    result['id'] = policy['id']
    return result


def identify_source_core(path, expected_sha256=None):
    base_bytes = compat.PROFILE.read_bytes()
    policy = json.loads(POLICY.read_text(encoding='utf-8'))
    profile = reviewed_profile(base_bytes, policy)
    build = compat.identify(path, profile=profile, expected_sha256=expected_sha256)
    report = deepcopy(build.randomizer_report)
    report['validation_policy'] = 'source-core-data-only-v1'
    report['execution_policy'] = 'original-USA-code-only'
    report['base_profile'] = json.loads(base_bytes)['id']
    return SourceSeedIdentity(build.sha256, report)
