"""Reject unreviewed warp-pot color policies before source conversion."""

DEFAULT_SMOKE_COLORS = 0x100337D4
CUSTOM_SMOKE_COLORS = 0x028FAC0C
REVIEWED_CUSTOM_COLORS = bytes.fromhex('E5101B803C379D80')


def validate_warp_smoke_colors(original_read, seed_read):
    original = original_read(DEFAULT_SMOKE_COLORS, 40)
    randomized = seed_read(DEFAULT_SMOKE_COLORS, 40)
    if len(original) != 40 or randomized != original:
        raise ValueError('unsupported warp-pot default smoke colors')
    if seed_read(CUSTOM_SMOKE_COLORS, 8) != REVIEWED_CUSTOM_COLORS:
        raise ValueError('unsupported warp-pot custom smoke colors')
