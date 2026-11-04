"""Static upstream metadata must match source constants and reject alterations."""
from pathlib import Path
import re
import struct
import unittest
from sage_spawn_data import SAGE_SPAWN_TABLES, validate_sage_spawns


class SageData(unittest.TestCase):
    def setUp(self):
        header = Path(__file__).with_name('sage_spawn_data.h').read_text()
        rows = re.findall(r'\{((?:0x[0-9A-F]{8}u,?){4})\}', header)
        self.assertEqual(len(rows), 14)
        packed = [struct.pack('>IIII', *(int(x[:-1], 16) for x in row.split(',') if x)) for row in rows]
        self.tables = {address: b''.join(packed[i*7:i*7+7]) for i, (address, _) in enumerate(SAGE_SPAWN_TABLES)}

    def test_exact_static_source_tables(self):
        validate_sage_spawns(lambda address, size: self.tables[address][:size])

    def test_each_table_rejects_changed_short_and_missing_data(self):
        for address, _ in SAGE_SPAWN_TABLES:
            for position in (0, 3, 4, 111):
                changed = bytearray(self.tables[address]); changed[position] ^= 1
                with self.assertRaisesRegex(ValueError, 'unsupported static sage'):
                    validate_sage_spawns(lambda a, size: bytes(changed) if a == address else self.tables[a])
            with self.assertRaises(ValueError):
                validate_sage_spawns(lambda a, size: self.tables[a][:-1] if a == address else self.tables[a])
            with self.assertRaises(ValueError):
                validate_sage_spawns(lambda a, size: b'' if a == address else self.tables[a])


if __name__ == '__main__':
    unittest.main()
