import unittest
from seed_resources import TABLES, INFO_BASE, extract_resources, render_resources


class SeedResources(unittest.TestCase):
    def readers(self):
        original = {base: bytes(stride*256) for base, stride, _ in TABLES.values()}
        original[INFO_BASE] = bytes(1024)
        seed = {key: bytearray(value) for key, value in original.items()}
        def reader(fields):
            def read(address, size):
                for base, value in fields.items():
                    if base <= address and address+size <= base+len(value):
                        return bytes(value[address-base:address-base+size])
                raise ValueError('unmapped fixture data')
            return read
        return original, seed, reader(original), reader(seed)

    def test_metadata_only_and_rendering(self):
        _, seed, original_read, seed_read = self.readers()
        base, stride, _ = TABLES['field']
        seed[0x10000100] = b'Test_arc\0'+bytes(55)
        seed[base][:4] = (0x10000100).to_bytes(4, 'big');seed[base][4] = 5
        seed[INFO_BASE][17] = 40
        result = extract_resources(original_read, seed_read)
        self.assertEqual(result['rows'], [{'table':'field', 'id':0, 'archive':'Test_arc', 'payload':[5]+[0]*23}])
        self.assertEqual(result['info_delta'], [(17,40)])
        output = render_resources(result)
        self.assertIn('resource_field_0_arc[]="Test_arc"', output)
        self.assertIn('n+4', output)

    def test_invalid_pointers_names_textures_and_reads(self):
        for pointer, name in ((0x028F0000,b'name\0'), (0x10000100,b'bad/name\0'),
                              (0x10000100,b'x'*64), (0x10000100,b'\0')):
            _, seed, original_read, seed_read = self.readers()
            base = TABLES['field'][0];seed[base][:4] = pointer.to_bytes(4,'big')
            seed[0x10000100] = name.ljust(64,b'\0')
            with self.subTest(pointer=pointer,name=name), self.assertRaises(ValueError):
                extract_resources(original_read, seed_read)
        _, seed, original_read, seed_read = self.readers()
        seed[TABLES['item'][0]][4] = 1
        with self.assertRaises(ValueError): extract_resources(original_read, seed_read)
        with self.assertRaises(ValueError): extract_resources(lambda a,n:b'', seed_read)


if __name__ == '__main__':
    unittest.main()
