import unittest
from seed_reward_tables import REWARD_TABLE_ADDRESSES, extract_reward_tables, render_reward_tables


class SeedRewardTables(unittest.TestCase):
    def test_only_reviewed_data_locations(self):
        calls = []
        def read(address, size):
            calls.append((address,size)); return bytes([address&255])
        values = extract_reward_tables(read)
        self.assertEqual(calls, [(address,1) for address in REWARD_TABLE_ADDRESSES])
        self.assertEqual(len(set(REWARD_TABLE_ADDRESSES)), 37)
        output = render_reward_tables(values)
        self.assertEqual(output.count('WWHD_GAME_DATA('), 37)
        self.assertIn('randomizer_apply_reward_data(void)', output)

    def test_short_and_invalid_data(self):
        for short in REWARD_TABLE_ADDRESSES:
            with self.subTest(address=short), self.assertRaises(ValueError):
                extract_reward_tables(lambda a,n: b'' if a==short else b'\x00')
        for values in ([], [0]*36, [0]*38, [256]*37, ['0; injected()']*37):
            with self.assertRaises(ValueError): render_reward_tables(values)


if __name__ == '__main__':
    unittest.main()
