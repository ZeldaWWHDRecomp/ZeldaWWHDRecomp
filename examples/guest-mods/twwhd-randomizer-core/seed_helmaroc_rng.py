"""Recognize the coherent three-choice Helmaroc policy over its entire owner.

These SHA256 values cover9,116 bytes of a reviewed function, not isolated
instructions. Unknown bodies and all mixed three-site variants fail closed.
No original executable data is embedded or emitted by this source port.
"""
import hashlib

FUNCTION = 0x0206D644
FUNCTION_SIZE = 0x239C
ORIGINAL_SHA256 = 'b47dc3eaf213446fd47eb933900559a561260db9614007463cefbf42ac12698e'
ENABLED_SHA256 = '8ce81c69b32d20703e5dddc6d6bfb086986788ed84ce42f80e4a862afe66992b'


def extract_helmaroc_rng(original_read, seed_read):
    def fingerprint(read):
        data = read(FUNCTION, FUNCTION_SIZE)
        if len(data) != FUNCTION_SIZE:
            raise ValueError('short Helmaroc RNG function')
        return hashlib.sha256(data).hexdigest()

    if fingerprint(original_read) != ORIGINAL_SHA256:
        raise ValueError('unsupported original Helmaroc RNG function')
    selected = fingerprint(seed_read)
    if selected == ORIGINAL_SHA256:
        enabled = 0
    elif selected == ENABLED_SHA256:
        enabled = 1
    else:
        raise ValueError('unsupported or inconsistent Helmaroc RNG function')
    return {'fix_helmaroc_rng': enabled}
