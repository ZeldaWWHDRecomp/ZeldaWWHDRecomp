"""MIT upstream remove_cutscenes fixed chest-animation table policy."""

MODEL_INFO = 0x101D2320
MODEL_INFO_SIZE = 4 * 12
FAST_OPEN_FIELDS = (14, 26, 38)


def validate_fast_chest_data(original_read, seed_read):
    original = original_read(MODEL_INFO, MODEL_INFO_SIZE)
    randomized = seed_read(MODEL_INFO, MODEL_INFO_SIZE)
    if len(original) != MODEL_INFO_SIZE or len(randomized) != MODEL_INFO_SIZE:
        raise ValueError('short treasure-chest model table')
    expected = bytearray(original)
    for offset in FAST_OPEN_FIELDS:
        expected[offset:offset+2] = (9).to_bytes(2, 'big')
    if randomized != expected:
        raise ValueError('unsupported treasure-chest model/animation table')
