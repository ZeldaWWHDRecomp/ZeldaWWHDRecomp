"""Decode the reviewed sea-compass option from a complete function fingerprint.

The public MIT patch removes heading negation. Fingerprints cover all212 bytes
of the public CC0 consumer; no executable bytes or instruction-sized hashes are
included. Original readers must already belong to the canonical USA image.
"""
import hashlib

FUNCTION = 0x0264C5A0
FUNCTION_SIZE = 0xD4
ORIGINAL_SHA256 = 'fcccbeed5a69853f40cd38250417bde927b3c2e867eaa6d36945cf5e9ae81ab6'
ENABLED_SHA256 = '54fdffd24674aba64926c8af4f7fb8ff8387d1c47cbf1d383dd12ab1ddb0925f'


def extract_compass_options(original_read, seed_read):
    def fingerprint(read):
        data = read(FUNCTION, FUNCTION_SIZE)
        if len(data) != FUNCTION_SIZE:
            raise ValueError('short sea-compass option function')
        return hashlib.sha256(data).hexdigest()

    if fingerprint(original_read) != ORIGINAL_SHA256:
        raise ValueError('unsupported original sea-compass function')
    selected = fingerprint(seed_read)
    if selected == ORIGINAL_SHA256:
        enabled = 0
    elif selected == ENABLED_SHA256:
        enabled = 1
    else:
        raise ValueError('unsupported sea-compass option function')
    return {'invert_sea_compass_x_axis': enabled}
