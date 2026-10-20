"""The island's own ground texture for a water body's banks (island_texture.py): synthetic tests, no game files."""
import struct
import unittest
import zlib

import island_texture as it


class IslandTextureTests(unittest.TestCase):
    def test_room_centre(self):
        self.assertEqual(it.room_centre(1), (-300000.0, -300000.0))
        self.assertEqual(it.room_centre(44), (-200000.0, 300000.0))  # Outset
        with self.assertRaises(it.base.BuildError):
            it.room_centre(50)

    def test_band_is_64_rows_round_v_and_repeats_without_a_seam(self):
        w, h, data = it.band(4, 64 * 4, b''.join(bytes([y, 0, 0, 255]) * 4 for y in range(256)), 0.5)
        self.assertEqual((w, h, len(data)), (4, 64, 4 * 64 * 4))
        first = [data[y * 16] for y in range(64)]
        self.assertEqual(first[:48], list(range(96, 144)))  # the rows round V = 0.5 (row 128), unchanged
        # its last rows fade into the rows just above its first: the step from the last row back to the first is small
        self.assertLess(abs(first[63] - first[0]), 8)

    def test_scaled_and_png_are_the_same_bytes_every_time(self):
        w, h, data = it.scaled(512, 16, b''.join(bytes([x % 256, 0, 0, 255]) for _ in range(16) for x in range(512)))
        self.assertEqual((w, h), (256, 8))
        self.assertEqual(data[:8], bytes([1, 0, 0, 255, 3, 0, 0, 255]))  # 2 x 2 boxes of (0, 1) and (2, 3), halves rounded up
        png = it.png(w, h, data)
        self.assertEqual(png, it.png(w, h, data))
        self.assertEqual(png[:8], b'\x89PNG\r\n\x1a\n')
        idat = png.index(b'IDAT')
        size = struct.unpack('>I', png[idat - 4:idat])[0]
        raw = zlib.decompress(png[idat + 4:idat + 4 + size])
        self.assertEqual(raw[0], 0)  # filter 0
        self.assertEqual(raw[1:1 + w * 4], data[:w * 4])

    def test_materials_without_a_valid_request_are_left_alone(self):
        doc = {'materials': [{'name': 'a'}, {'name': 'b', 'extras': {'wwhd_island': {'room': 'x', 'points': []}}},
                             {'name': 'c', 'extras': {'wwhd_island': {'room': 44, 'points': [[1, 2]] * 300}}}]}
        self.assertEqual(it.island_images('/nonexistent', doc), {})


if __name__ == '__main__':
    unittest.main()
