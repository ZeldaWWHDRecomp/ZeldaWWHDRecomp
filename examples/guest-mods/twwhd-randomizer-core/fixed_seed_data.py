"""Validate reviewed non-item RPX data policies before source conversion."""

STONE_HEAD_STATUS = 0x101CA100


def validate_fixed_seed_data(original_read, seed_read):
    original = original_read(STONE_HEAD_STATUS, 4)
    randomized = seed_read(STONE_HEAD_STATUS, 4)
    if len(original) != 4 or len(randomized) != 4:
        raise ValueError('short stone-head profile status')
    expected = int.from_bytes(original, 'big') & ~0x80
    if int.from_bytes(randomized, 'big') != expected:
        raise ValueError('unsupported stone-head profile status')

    ff_quadrant = seed_read(0x100F7BD0, 4)
    if len(ff_quadrant) != 4 or ff_quadrant != bytes(4):
        raise ValueError('unsupported Forsaken Fortress warp quadrant')
