"""Reviewed MIT tweaks.cpp::add_pirate_ship_to_windfall RPX data change."""

ARYLL_ANIMATIONS = 0x101BFF84
ARYLL_ANIMATION_SIZE = 0x10
ARYLL_ANIMATION_COUNT = 14
ARYLL_DOOR_ANIMATION_OFFSET = 4 * ARYLL_ANIMATION_SIZE


def validate_windfall_seed_data(original_read, seed_read):
    size = ARYLL_ANIMATION_SIZE * ARYLL_ANIMATION_COUNT
    original = original_read(ARYLL_ANIMATIONS, size)
    randomized = seed_read(ARYLL_ANIMATIONS, size)
    if len(original) != size or len(randomized) != size:
        raise ValueError('short Aryll Windfall animation table')
    expected = bytearray(original)
    expected[ARYLL_DOOR_ANIMATION_OFFSET] = 5
    if randomized != expected:
        raise ValueError('unsupported Aryll Windfall animation table')
