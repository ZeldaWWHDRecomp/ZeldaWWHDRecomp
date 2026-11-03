"""Bound untrusted input and actual inflation without trusting the zlib prefix."""
from pathlib import Path
import struct, tempfile, unittest, zlib
import sys

# Direct execution must work independently of imports performed by another suite.
HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent)]
from unittest import mock
from rpx import Rpx, Section, SHF_RPL_ZLIB, SHT_SYMTAB, SHT_RELA
import randomizer_compat as compat

class RpxLimits(unittest.TestCase):
    def parse(self, sections, limit=64):
        payloads=[(b'\0.names\0.payload\0',0)]+sections
        header=bytearray(0x34);header[:6]=b'\x7fELF\x01\x02';header[18:20]=b'\x00\x14';struct.pack_into('>I',header,0x20,0x34)
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

    def test_names_reject_before_slicing_or_decoding(self):
        class NoSlice(bytes):
            def __getitem__(self, key):
                if isinstance(key, slice):
                    raise AssertionError('must reject before allocating a name')
                return super().__getitem__(key)
        rpx=Rpx.__new__(Rpx);rpx.limits=dict(name=1024,name_bytes=4096);rpx._name_bytes=0
        for offset in (0,1,500):
            with self.assertRaisesRegex(ValueError,'name length'):
                rpx._read_name(NoSlice(b'x'*(1<<20)+b'\0'),offset)
        self.assertEqual(rpx._name_bytes,0)
        # Repeated and overlapping short names share an aggregate budget.
        rpx.limits=dict(name=32,name_bytes=9)
        self.assertEqual(rpx._read_name(b'abcde\0',0),'abcde')
        self.assertEqual(rpx._read_name(b'abcde\0',1),'bcde')
        with self.assertRaisesRegex(ValueError,'aggregate name budget'):
            rpx._read_name(NoSlice(b'abcde\0'),0)
        self.assertEqual(rpx._name_bytes,9)

    def test_program_reuses_parsed_entry_without_reopening_input(self):
        from types import SimpleNamespace
        import analyze
        text=SimpleNamespace(addr=0x2000000,size=4,data=b'\x4e\x80\x00\x20')
        parsed=SimpleNamespace(by_name={'.text':text},entry=0x2000000,relocs=[])
        with mock.patch.object(analyze,'Rpx',return_value=parsed), \
             mock.patch('builtins.open',side_effect=AssertionError('input must not be reopened')):
            program=analyze.Program('could-grow-after-parse.rpx',rpx_limits=compat.RPX_LIMITS)
        self.assertEqual(program.entry,parsed.entry)

    def test_identify_rejects_parsed_buffer_even_if_path_hash_is_unchanged(self):
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'seed.rpx';header=bytearray(0x34)
            header[:6]=b'\x7fELF\x01\x02';header[18:20]=b'\x00\x14';path.write_bytes(header)
            with mock.patch.object(compat.builds,'file_sha256',return_value='a'*64), \
                 mock.patch.object(compat,'Rpx',return_value=SimpleNamespace(buffer_sha256='b'*64)):
                with self.assertRaisesRegex(ValueError,'while parsing its input buffer'):
                    compat.identify(path)

    def test_translation_rejects_replaced_file_before_discovery(self):
        import hashlib
        from types import SimpleNamespace
        import recomp
        # Minimal bounded container, changed after identity selection, before
        # the translator parses it. Restoring the path afterward cannot change
        # the digest of the actual buffer passed into analysis.
        header=bytearray(0x34);header[:6]=b'\x7fELF\x01\x02';header[18:20]=b'\x00\x14';struct.pack_into('>I',header,0x20,0x34)
        struct.pack_into('>HHH',header,0x2E,40,1,0)
        names=b'\0.names\0'
        entry=struct.pack('>10I',1,1,0,0,0x34+40,len(names),0,0,1,0)
        original=bytes(header)+entry+names
        changed=bytearray(original);changed[0x18]=1
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'seed.rpx';path.write_bytes(original)
            build=SimpleNamespace(randomizer=True,sha256=hashlib.sha256(original).hexdigest())
            discovery=mock.Mock()
            def identify(selected):
                path.write_bytes(changed)
                return build
            def program(selected, rpx_limits):
                parsed=Rpx(selected,limits=rpx_limits)
                path.write_bytes(original)
                return SimpleNamespace(rpx=parsed,discover=discovery)
            with mock.patch.object(recomp.game_builds,'identify',side_effect=identify), \
                 mock.patch.object(recomp,'Program',side_effect=program):
                with self.assertRaisesRegex(ValueError,'before translation parsed'):
                    recomp.Recompiler(path)
            discovery.assert_not_called()
            self.assertEqual(path.read_bytes(),original)

    def test_actual_buffer_header_is_validated(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'bad.rpx'
            valid=bytearray(0x34);valid[:6]=b'\x7fELF\x01\x02';valid[18:20]=b'\x00\x14'
            for offset,value in [(0,0),(4,2),(5,1),(19,21)]:
                bad=bytearray(valid);bad[offset]=value;path.write_bytes(bad)
                with self.subTest(offset=offset),self.assertRaisesRegex(ValueError,'PowerPC RPX'):
                    Rpx(path,limits=compat.RPX_LIMITS)

    def test_valid_initial_header_cannot_waive_bad_replacement_header(self):
        import hashlib
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'seed.rpx';header=bytearray(0x34)
            header[:6]=b'\x7fELF\x01\x02';header[18:20]=b'\x00\x14';path.write_bytes(header)
            bad=bytearray(header);bad[5]=1
            def changed_hash(selected):
                path.write_bytes(bad)
                return hashlib.sha256(bad).hexdigest()
            with mock.patch.object(compat.builds,'file_sha256',side_effect=changed_hash):
                with self.assertRaisesRegex(ValueError,'PowerPC RPX'):
                    compat.identify(path)

    def test_header_to_hash_replacement_checks_actual_parsed_entry(self):
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'seed.rpx';header=bytearray(0x34)
            header[:6]=b'\x7fELF\x01\x02';header[18:20]=b'\x00\x14'
            struct.pack_into('>I',header,24,0x2000000);path.write_bytes(header)
            # The first header is valid but a replacement before hashing has a
            # different entry. All subsequent hashes match that replacement.
            parsed=SimpleNamespace(buffer_sha256='b'*64,entry=0x2000004,
                                   by_name={'.text':SimpleNamespace(size=2)})
            profile={'base_sha256':'a'*64,'sections':[{'name':'.text','size':1}]}
            def changed_hash(selected):
                new=bytearray(header);struct.pack_into('>I',new,24,parsed.entry);path.write_bytes(new)
                return 'b'*64
            def check(rpx,profile,hooks,entry):
                self.assertEqual(entry,parsed.entry)
                raise compat.CompatibilityError('actual parsed entry rejected')
            with mock.patch.object(compat.builds,'file_sha256',side_effect=changed_hash), \
                 mock.patch.object(compat,'Rpx',return_value=parsed), \
                 mock.patch.object(compat,'check',side_effect=check):
                with self.assertRaisesRegex(ValueError,'actual parsed entry rejected'):
                    compat.identify(path,profile=profile)

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
