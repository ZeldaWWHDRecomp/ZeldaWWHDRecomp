"""Validate invariant MIT randomizer sage spawn metadata before source seed preparation.

These tables are static in official1.2.0. A changed table needs explicit review;
never silently substitute the source module's constants for altered input data.
"""
import hashlib
SAGE_SPAWN_TABLES = (
    (0x028fa63c, '7dc2a74958d4c71fcf8793e3258c461591d1ca40e484ff9df1105fb3790438e7'),
    (0x028fa738, '6ccc58af48d2631bdf15a5addf29970f2054f0cca1b914ee19706ab12b3a26f5'),
 )
def validate_sage_spawns(read):
    for address, expected in SAGE_SPAWN_TABLES:
        data = read(address, 112)
        if len(data) != 112 or hashlib.sha256(data).hexdigest() != expected:
            raise ValueError('unsupported static sage spawn table at %08X' % address)
