"""Whole-owner policy coherence with explicitly synthetic executable fixtures."""
import hashlib
import unittest
from contextlib import contextmanager
from unittest.mock import patch

import seed_helmaroc_rng as options

SITES = (0x0206D868, 0x0206DB9C, 0x0206E9B4)


def synthetic_function(mask=0):
    data = bytearray((i * 73 + 19) & 255 for i in range(options.FUNCTION_SIZE))
    for index, site in enumerate(SITES):
        if mask & (1 << index):
            word = 24 << 26 if index == 0 else (18 << 26) | (0x20 if index == 1 else 0x10)
        else:
            word = (16 << 26) | ((12 if index == 1 else 4) << 21) | (0x30 if index == 0 else 0x20 if index == 1 else 0x10)
        offset = site - options.FUNCTION
        data[offset:offset + 4] = word.to_bytes(4, 'big')
    return bytes(data)


@contextmanager
def synthetic_helmaroc_hashes():
    with patch.object(options, 'ORIGINAL_SHA256', hashlib.sha256(synthetic_function()).hexdigest()), \
         patch.object(options, 'ENABLED_SHA256', hashlib.sha256(synthetic_function(7)).hexdigest()):
        yield


def reader(data):
    def read(address, size):
        assert address == options.FUNCTION and size == options.FUNCTION_SIZE
        return data
    return read


class HelmarocPolicy(unittest.TestCase):
    def test_all_three_site_combinations(self):
        self.assertGreaterEqual(options.FUNCTION_SIZE, 64)
        with synthetic_helmaroc_hashes():
            for mask in range(8):
                if mask in (0, 7):
                    self.assertEqual(options.extract_helmaroc_rng(
                        reader(synthetic_function()), reader(synthetic_function(mask))),
                        {'fix_helmaroc_rng': int(mask == 7)})
                else:
                    with self.assertRaisesRegex(ValueError, 'inconsistent'):
                        options.extract_helmaroc_rng(reader(synthetic_function()), reader(synthetic_function(mask)))

    def test_whole_owner_mutations_and_bounds(self):
        original = synthetic_function()
        with synthetic_helmaroc_hashes():
            for mask in (0, 7):
                body = synthetic_function(mask)
                for byte in range(len(body)):
                    for bit in range(8):
                        changed = bytearray(body)
                        changed[byte] ^= 1 << bit
                        with self.assertRaises(ValueError):
                            options.extract_helmaroc_rng(reader(original), reader(changed))
            for size in (0, 1, options.FUNCTION_SIZE - 1, options.FUNCTION_SIZE + 1):
                with self.assertRaisesRegex(ValueError, 'short'):
                    options.extract_helmaroc_rng(reader(bytes(size)), reader(original))
                with self.assertRaisesRegex(ValueError, 'short'):
                    options.extract_helmaroc_rng(reader(original), reader(bytes(size)))
            with self.assertRaisesRegex(ValueError, 'original'):
                options.extract_helmaroc_rng(reader(synthetic_function(7)), reader(original))


if __name__ == '__main__':
    unittest.main()
