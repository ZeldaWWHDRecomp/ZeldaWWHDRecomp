"""Whole-function option classification with synthetic non-game fixtures."""
import hashlib
import unittest
from contextlib import contextmanager
from unittest.mock import patch

import seed_compass_options as options


def synthetic_function(enabled=False):
    # Deliberately non-executable synthetic filler, not original game bytes.
    data = bytearray((i * 73 + 19) & 255 for i in range(options.FUNCTION_SIZE))
    if enabled:
        word = (31 << 26) | (12 << 21) | (5 << 16) | (12 << 11) | (444 << 1)
    else:
        word = (31 << 26) | (5 << 21) | (12 << 16) | (104 << 1)
    data[0x58:0x5C] = word.to_bytes(4, 'big')
    return bytes(data)


@contextmanager
def synthetic_compass_hashes():
    with patch.object(options, 'ORIGINAL_SHA256', hashlib.sha256(synthetic_function()).hexdigest()), \
         patch.object(options, 'ENABLED_SHA256', hashlib.sha256(synthetic_function(True)).hexdigest()):
        yield


def reader(data):
    def read(address, size):
        assert address == options.FUNCTION and size == options.FUNCTION_SIZE
        return data
    return read


class CompassOptions(unittest.TestCase):
    def test_exact_variants(self):
        self.assertGreaterEqual(options.FUNCTION_SIZE, 64)
        with synthetic_compass_hashes():
            for enabled in (False, True):
                self.assertEqual(options.extract_compass_options(
                    reader(synthetic_function()), reader(synthetic_function(enabled))),
                    {'invert_sea_compass_x_axis': int(enabled)})

    def test_every_function_bit_and_short_reader(self):
        with synthetic_compass_hashes():
            for enabled in (False, True):
                body = synthetic_function(enabled)
                for byte in range(len(body)):
                    for bit in range(8):
                        changed = bytearray(body)
                        changed[byte] ^= 1 << bit
                        with self.assertRaisesRegex(ValueError, 'unsupported sea-compass'):
                            options.extract_compass_options(reader(synthetic_function()), reader(changed))
            for size in (0, 1, options.FUNCTION_SIZE - 1, options.FUNCTION_SIZE + 1):
                with self.assertRaisesRegex(ValueError, 'short'):
                    options.extract_compass_options(reader(bytes(size)), reader(synthetic_function()))
                with self.assertRaisesRegex(ValueError, 'short'):
                    options.extract_compass_options(reader(synthetic_function()), reader(bytes(size)))
            with self.assertRaisesRegex(ValueError, 'original'):
                options.extract_compass_options(reader(synthetic_function(True)), reader(synthetic_function(True)))

if __name__ == '__main__':
    unittest.main()
