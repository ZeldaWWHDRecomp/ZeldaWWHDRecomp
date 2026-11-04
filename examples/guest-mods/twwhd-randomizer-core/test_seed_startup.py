from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from seed_startup import BYTE_FIELDS, extract_startup
from seed_options import REVEAL_CHART_SITE
from test_seed_entrance_options import native_operands


class SeedStartup(unittest.TestCase):
    def image(self):
        fields = {address: bytes([index % 2]) for index, address in enumerate(BYTE_FIELDS.values())}
        fields[BYTE_FIELDS['starting_magic']] = bytes([16])
        fields[BYTE_FIELDS['outset_pig_color']] = bytes([2])
        fields[0x028F9F24] = (0xD73).to_bytes(4, 'big')
        fields[0x028FAC24] = bytes([0x40, 0, 0, 0])
        fields[0x028F8E10] = b'\x00\x0c'
        fields[0x028F8D0F] = bytes([0x38, 0x33, 255]) + bytes(254)
        # Synthetic ISA encodings, no game instructions or assets.
        for address, value in ((0x025B508C, 44), (0x025B50C8, 44),
                               (0x025B5090, 0), (0x025B50CC, 0)):
            fields[address] = ((14 << 26) | (3 << 21) | value).to_bytes(4, 'big')
        fields[REVEAL_CHART_SITE] = ((14 << 26) | (10 << 21) | 1).to_bytes(4, 'big')
        fields.update(native_operands())
        return fields

    def test_data_and_terminated_gear(self):
        fields = self.image()
        result = extract_startup(lambda a, n: fields[a][:n])
        self.assertEqual(result['starting_gear'], [0x38, 0x33])
        self.assertEqual(result['quarter_hearts'], 12)
        self.assertEqual(result['damage_multiplier'], 2.0)
        self.assertEqual(result['start_room'], 44)
        self.assertEqual(result['reveal_full_sea_chart'], 1)
        for i, key in enumerate(BYTE_FIELDS):
            self.assertEqual(result[key], fields[BYTE_FIELDS[key]][0])

    def test_reject_short_every_field(self):
        fields = self.image()
        for address in fields:
            with self.subTest(address=address), self.assertRaises(ValueError):
                extract_startup(lambda a, n: fields[a][:(n-1 if a == address else n)])

    def test_reject_unsupported_option_domains(self):
        for key in BYTE_FIELDS:
            fields = self.image(); fields[BYTE_FIELDS[key]] = bytes([255])
            with self.subTest(key=key), self.assertRaises(ValueError):
                extract_startup(lambda a, n: fields[a][:n])
        for health in (0, 3, 81, 65535):
            fields = self.image(); fields[0x028F8E10] = health.to_bytes(2, 'big')
            with self.subTest(health=health), self.assertRaises(ValueError):
                extract_startup(lambda a, n: fields[a][:n])

    def test_korl_symbol_data_domains(self):
        for end in (0xD73, 0xD74, 0x7FFFFFFF):
            fields = self.image(); fields[0x028F9F24] = end.to_bytes(4, 'big')
            self.assertEqual(extract_startup(lambda a, n: fields[a][:n])['last_korl_hint_message_number'], end)
        for end in (0, 0xD72, 0x80000000, 0xFFFFFFFF):
            fields = self.image(); fields[0x028F9F24] = end.to_bytes(4, 'big')
            with self.subTest(end=end), self.assertRaisesRegex(ValueError, 'last_korl_hint'):
                extract_startup(lambda a, n: fields[a][:n])
        fields = self.image(); fields[BYTE_FIELDS['use_different_korl_hyrule_text']] = bytes([2])
        with self.assertRaisesRegex(ValueError, 'use_different_korl'):
            extract_startup(lambda a, n: fields[a][:n])

    def test_reject_bad_gear_and_places(self):
        for address, data in (
            (0x028F8D0F, bytes(257)),
            (0x028FAC24, bytes([0x7F, 0x80, 0, 0])),
            (0x028FAC24, bytes([0xBF, 0x80, 0, 0])),
            (0x025B50C8, ((14 << 26) | (3 << 21) | 45).to_bytes(4, 'big')),
            (0x025B508C, ((14 << 26) | (3 << 21) | (1 << 16) | 44).to_bytes(4, 'big')),
            (0x025B5090, ((14 << 26) | (3 << 21) | 256).to_bytes(4, 'big')),
            (0x025B5090, bytes(4)),
        ):
            fields = self.image()
            fields[address] = data
            with self.subTest(address=address), self.assertRaises(ValueError):
                extract_startup(lambda a, n: fields[a][:n])


if __name__ == '__main__':
    unittest.main()
