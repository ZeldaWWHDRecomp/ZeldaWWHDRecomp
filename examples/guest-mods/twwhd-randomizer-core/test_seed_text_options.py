"""Semantic instant-text option variants, including mixed/unknown rejection."""
import unittest
from seed_text_options import INPUT_SITES, ANIMATION_SITES, extract_text_options


def option_operands(enabled):
    # Synthetic forms from the public patch and PPC field definitions, not game bytes.
    result = {site: ((32 << 26) | (target << 21) | (base << 16) |
                     (0x50 if enabled else 0x54)).to_bytes(4, 'big')
              for site, target, base in INPUT_SITES}
    result.update({site: ((24 << 26) if enabled else
                         (16 << 26) | (12 << 21) | (2 << 16) | 4).to_bytes(4, 'big')
                   for site in ANIMATION_SITES})
    return result


class TextOptions(unittest.TestCase):
    def test_coherent_options(self):
        original = option_operands(False)
        for enabled in (False, True):
            fields = option_operands(enabled)
            self.assertEqual(extract_text_options(lambda a, n: original[a], lambda a, n: fields[a]),
                             {'instant_text_boxes': int(enabled)})
            for address in fields:
                changed = dict(fields); changed[address] = option_operands(not enabled)[address]
                with self.assertRaisesRegex(ValueError, 'inconsistent'):
                    extract_text_options(lambda a, n: original[a], lambda a, n: changed[a])
                for size in (0, 1, 3, 5):
                    with self.assertRaises(ValueError):
                        extract_text_options(lambda a, n: original[a],
                                             lambda a, n: bytes(size) if a == address else fields[a])

    def test_every_changed_instruction_bit_and_original_bounds(self):
        original = option_operands(False)
        enabled = option_operands(True)
        for address, value in enabled.items():
            for bit in range(32):
                changed = dict(enabled)
                changed[address] = (int.from_bytes(value, 'big') ^ (1 << bit)).to_bytes(4, 'big')
                with self.assertRaises(ValueError):
                    extract_text_options(lambda a, n: original[a], lambda a, n: changed[a])
            with self.assertRaises(ValueError):
                extract_text_options(lambda a, n: b'' if a == address else original[a], lambda a, n: enabled[a])
        for address, _, _ in INPUT_SITES:
            changed = dict(original); changed[address] = enabled[address]
            with self.assertRaisesRegex(ValueError, 'original'):
                extract_text_options(lambda a, n: changed[a], lambda a, n: enabled[a])
        for address in ANIMATION_SITES:
            changed = dict(original); changed[address] = enabled[address]
            with self.assertRaisesRegex(ValueError, 'original'):
                extract_text_options(lambda a, n: changed[a], lambda a, n: enabled[a])


if __name__ == '__main__':
    unittest.main()
