"""The fixed smoke policy must not silently accept seed color modifications."""
import unittest
from warp_smoke_seed_data import (
    DEFAULT_SMOKE_COLORS, CUSTOM_SMOKE_COLORS, REVIEWED_CUSTOM_COLORS,
    validate_warp_smoke_colors,
)


class WarpSmokePolicy(unittest.TestCase):
    def test_reviewed_policy_and_every_single_bit_change(self):
        defaults = bytes(range(40))

        def original(address, size):
            self.assertEqual((address, size), (DEFAULT_SMOKE_COLORS, 40))
            return defaults

        def seed(default, custom):
            def read(address, size):
                if address == DEFAULT_SMOKE_COLORS:
                    self.assertEqual(size, 40)
                    return default
                self.assertEqual((address, size), (CUSTOM_SMOKE_COLORS, 8))
                return custom
            return read

        validate_warp_smoke_colors(original, seed(defaults, REVIEWED_CUSTOM_COLORS))
        for byte in range(48):
            for bit in range(8):
                changed = bytearray(defaults + REVIEWED_CUSTOM_COLORS)
                changed[byte] ^= 1 << bit
                with self.assertRaisesRegex(ValueError, 'warp-pot .* smoke colors'):
                    validate_warp_smoke_colors(original, seed(changed[:40], changed[40:]))
        for size in range(40):
            with self.assertRaisesRegex(ValueError, 'default smoke colors'):
                validate_warp_smoke_colors(lambda a, n: defaults[:size], seed(defaults, REVIEWED_CUSTOM_COLORS))
            with self.assertRaisesRegex(ValueError, 'default smoke colors'):
                validate_warp_smoke_colors(original, seed(defaults[:size], REVIEWED_CUSTOM_COLORS))
        for size in range(8):
            with self.assertRaisesRegex(ValueError, 'custom smoke colors'):
                validate_warp_smoke_colors(original, seed(defaults, REVIEWED_CUSTOM_COLORS[:size]))
        with self.assertRaises(ValueError):
            validate_warp_smoke_colors(original, seed(defaults + b'\0', REVIEWED_CUSTOM_COLORS))
        with self.assertRaises(ValueError):
            validate_warp_smoke_colors(original, seed(defaults, REVIEWED_CUSTOM_COLORS + b'\0'))


if __name__ == '__main__':
    unittest.main()
