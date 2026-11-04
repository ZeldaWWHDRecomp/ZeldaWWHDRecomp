import unittest
from seed_entrance_options import SITES, MESA_SITE, extract_entrance_options


def native_operands():
    fields = {}
    for site, _, family, target, source in SITES:
        word = ((34 << 26) | (target << 21) | (source << 16) | 0x7B8) if family == 'lbz' else (
            (31 << 26) | (source << 21) | (target << 16) | (source << 11) | (444 << 1))
        fields[site] = word.to_bytes(4, 'big')
    fields[MESA_SITE] = ((16 << 26) | (12 << 21) | (2 << 16) | 0x5C).to_bytes(4, 'big')
    return fields


def patched_operands():
    fields = {site: ((18 << 26) | ((helper - site) & 0x03FFFFFC)).to_bytes(4, 'big')
              for site, helper, *_ in SITES}
    fields[MESA_SITE] = ((16 << 26) | (12 << 21) | (2 << 16) | 0xF8).to_bytes(4, 'big')
    return fields


class EntranceOptions(unittest.TestCase):
    def test_group_enabled_and_disabled(self):
        for expected, fields in ((0, native_operands()), (1, patched_operands())):
            self.assertEqual(extract_entrance_options(lambda a, n: fields[a][:n]),
                             {'entrances_shuffled': expected, 'mesa_door_unlocked': expected})

    def test_mesa_is_separate_and_strict(self):
        fields = patched_operands();fields[MESA_SITE] = native_operands()[MESA_SITE]
        self.assertEqual(extract_entrance_options(lambda a, n: fields[a][:n]),
                         {'entrances_shuffled': 1, 'mesa_door_unlocked': 0})
        for value in (b'', b'\0'*4, ((16<<26)|(12<<21)|(2<<16)|0xFC).to_bytes(4, 'big'),
                      ((16<<26)|(12<<21)|(2<<16)|0xF9).to_bytes(4, 'big')):
            fields = patched_operands();fields[MESA_SITE] = value
            with self.assertRaises(ValueError):extract_entrance_options(lambda a, n: fields[a][:n])
        fields = native_operands();fields[MESA_SITE] = patched_operands()[MESA_SITE]
        with self.assertRaisesRegex(ValueError, 'inconsistent'):
            extract_entrance_options(lambda a, n: fields[a][:n])

    def test_partial_group_unknown_branch_flags_and_short_reads(self):
        for site, *_ in SITES:
            fields = native_operands();fields[site] = patched_operands()[site]
            with self.assertRaisesRegex(ValueError, 'inconsistent'):
                extract_entrance_options(lambda a, n: fields[a][:n])
            for invalid in (b'', b'\0\0\0\0', (int.from_bytes(patched_operands()[site], 'big') | 1).to_bytes(4, 'big'),
                            (int.from_bytes(patched_operands()[site], 'big') + 4).to_bytes(4, 'big')):
                fields = patched_operands();fields[site] = invalid
                with self.assertRaises(ValueError):extract_entrance_options(lambda a, n: fields[a][:n])
