"""Bound untrusted input and actual inflation without trusting the zlib prefix."""
from pathlib import Path
import struct, tempfile, unittest, zlib
from unittest import mock
from rpx import Rpx, Section, SHF_RPL_ZLIB, SHT_SYMTAB, SHT_RELA
import randomizer_compat as compat

class RpxLimits(unittest.TestCase):
    def parse(self, sections, limit=64):
        payloads=[(b'\0.names\0.payload\0',0)]+sections
        header=bytearray(0x34);struct.pack_into('>I',header,0x20,0x34)
        struct.pack_into('>HHH',header,0x2E,40,len(payloads),0)
        offset=len(header)+40*len(payloads);entries=[];data=b''
        for index,(raw,flags) in enumerate(payloads):
            entries.append(struct.pack('>10I',1 if index==0 else 8,1,flags,0,offset,len(raw),0,0,1,0))
            offset+=len(raw);data+=raw
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'test.rpx';path.write_bytes(header+b''.join(entries)+data)
            return Rpx(path,limits=dict(file=4096,section=limit,inflated=limit))

    def packed(self, raw, declared=None):
        return struct.pack('>I',len(raw) if declared is None else declared)+zlib.compress(raw),SHF_RPL_ZLIB

    def test_output_and_aggregate_cap(self):
        self.assertEqual(self.parse([self.packed(b'x'*47)]).sections[1].data,b'x'*47)
        for sections in [[self.packed(b'x'*24),self.packed(b'x'*25)],[(b'x'*49,0)]]:
            with self.assertRaisesRegex(ValueError,'limit'):self.parse(sections)

    def test_lengths_truncated_and_trailing_streams(self):
        for raw in [self.packed(b'x'*1000000,1)[0],self.packed(b'xx',3)[0],
                    self.packed(b'xx')[0][:-1],self.packed(b'xx')[0]+b'trailing',
                    self.packed(b'xx')[0]+zlib.compress(b'other'),b'xx',b'']:
            with self.subTest(size=len(raw)),self.assertRaises(ValueError):self.parse([(raw,SHF_RPL_ZLIB)])

    def test_declared_cap_before_inflate(self):
        with mock.patch('rpx.zlib.decompressobj') as inflate:
            with self.assertRaisesRegex(ValueError,'limit'):self.parse([self.packed(b'x',100000000)])
            inflate.assert_not_called()

    def test_child_translation_parser_uses_same_limits(self):
        from types import SimpleNamespace
        import recomp
        with mock.patch.object(recomp, 'Program', side_effect=RuntimeError('stop before discovery')) as program:
            with self.assertRaisesRegex(RuntimeError, 'stop before discovery'):
                recomp.Recompiler('seed.rpx', build=SimpleNamespace(randomizer=True))
            program.assert_called_once_with('seed.rpx', rpx_limits=compat.RPX_LIMITS)

    def test_object_counts_fail_before_object_loops(self):
        rpx=Rpx.__new__(Rpx);rpx.limits=dict(symbols=1,relocations=1)
        section=Section();section.type=SHT_SYMTAB;section.data=b'x'*32
        rpx.sections=[section]
        with mock.patch('rpx.Symbol') as symbol:
            with self.assertRaisesRegex(ValueError,'symbol count'):rpx._load_symbols()
            symbol.assert_not_called()
        section.type=SHT_RELA;section.data=b'x'*24
        with self.assertRaisesRegex(ValueError,'relocation count'):rpx._load_relocs()

    def test_file_cap_before_hash_or_parse(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'large.rpx'
            with path.open('wb') as stream:stream.truncate(compat.RPX_LIMITS['file']+1)
            with mock.patch.object(compat.builds,'file_sha256') as hashed:
                with self.assertRaisesRegex(ValueError,'size limit'):compat.identify(path)
                hashed.assert_not_called()
            with self.assertRaisesRegex(ValueError,'size limit'):Rpx(path,limits=dict(file=32,section=32,inflated=32))

if __name__=='__main__':unittest.main()
