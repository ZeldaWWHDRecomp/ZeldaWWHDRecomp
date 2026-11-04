"""Validated optional particle policy, hashed over the complete USA consumer.

Only hashes are retained; the enabled body differs by the MIT performance.asm
max-particle argument. Unknown surrounding code fails closed.
"""
import hashlib

FUNCTION = (0x025A7838, 0x025A7C30,
            '88efcc05a95885803b1bd7dbc46fb842cbc54f564c87e8a7bec3ae3497a722ae',
            '19d1f61aee9ef4d9b52cd7640638541dc49d179b1e82723d8f84d37901d13b89')


def extract_particle_policy(read):
    start, end, disabled, enabled = FUNCTION
    body = read(start, end-start)
    if not isinstance(body, (bytes, bytearray, memoryview)) or len(body) != end-start:
        raise ValueError('truncated optional particle function')
    digest = hashlib.sha256(body).hexdigest()
    if digest == disabled:
        return 0
    if digest == enabled:
        return 1
    raise ValueError('unsupported optional particle function')
