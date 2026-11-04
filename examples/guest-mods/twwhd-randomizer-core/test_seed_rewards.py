from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from seed_rewards import ALIASES, RAW_ALIASES, RAW_REWARDS, REWARDS, extract_rewards


class SeedRewards(unittest.TestCase):
    def image(self, value):
        fields = {}
        for site in [s for sites in REWARDS.values() for s in sites] + [s for pair in ALIASES for s in pair]:
            address, op, first, base = site
            fields[address-3] = ((op << 26) | (first << 21) | (base << 16) | value).to_bytes(4, 'big')
        for _, address in RAW_ALIASES:
            fields[address] = bytes([value])
        for sites in RAW_REWARDS.values():
            for address, size in sites:
                fields[address] = value.to_bytes(size, 'big')
        return fields

    def test_all_item_bytes(self):
        for value in range(256):
            fields = self.image(value)
            result = extract_rewards(lambda a, n: fields[a][:n])
            self.assertEqual(result, {key: [value]*len(sites) for key, sites in (REWARDS | RAW_REWARDS).items()})

    def test_reject_each_short_or_changed_layout(self):
        fields = self.image(42)
        for address in fields:
            if len(fields[address]) != 4:
                continue
            with self.subTest(address=address, kind='short'), self.assertRaises(ValueError):
                extract_rewards(lambda a, n: fields[a][:3 if a == address else n])
            changed = dict(fields)
            changed[address] = (int.from_bytes(fields[address], 'big') ^ (1 << 16)).to_bytes(4, 'big')
            with self.subTest(address=address, kind='layout'), self.assertRaises(ValueError):
                extract_rewards(lambda a, n: changed[a][:n])

    def test_reject_inconsistent_alternate_branches(self):
        for _, alias in ALIASES:
            fields = self.image(42)
            a = alias[0]-3
            fields[a] = fields[a][:3] + bytes([43])
            with self.subTest(alias=alias), self.assertRaises(ValueError):
                extract_rewards(lambda a, n: fields[a][:n])

    def test_raw_alias_missing_or_disagreeing(self):
        for _, address in RAW_ALIASES:
            for value in (b'', b'\x2B'):
                fields = self.image(42)
                fields[address] = value
                with self.subTest(address=address, value=value), self.assertRaises(ValueError):
                    extract_rewards(lambda a, n: fields[a][:n])

    def test_raw_rewards_require_all_bytes(self):
        for sites in RAW_REWARDS.values():
            for address, _ in sites:
                fields = self.image(42)
                fields[address] = b''
                with self.subTest(address=address), self.assertRaises(ValueError):
                    extract_rewards(lambda a, n: fields[a][:n])


if __name__ == '__main__':
    unittest.main()
