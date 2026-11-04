from test_permanent_resource_heap import synthetic_permanent_hash
import unittest
import tempfile
from pathlib import Path
from unittest.mock import patch
import json
from seed_prepare import generate_headers
from seed_startup import BYTE_FIELDS
from seed_options import REVEAL_CHART_SITE
from test_seed_text_options import option_operands
from test_seed_compass_options import synthetic_function, synthetic_compass_hashes
from seed_compass_options import FUNCTION as COMPASS_FUNCTION
from test_seed_helmaroc_rng import synthetic_function as synthetic_rng_function, synthetic_helmaroc_hashes
from seed_helmaroc_rng import FUNCTION as HELMAROC_FUNCTION
from test_seed_entrance_options import native_operands
from seed_rewards import REWARDS, ALIASES, RAW_ALIASES, RAW_REWARDS
from seed_resources import TABLES, INFO_BASE
from seed_reward_tables import REWARD_TABLE_ADDRESSES
from extract_boss_rewards import BOSS_SITES


class SeedPrepare(unittest.TestCase):
    def setUp(self):
        permanent = synthetic_permanent_hash()
        permanent.__enter__(); self.addCleanup(permanent.__exit__, None, None, None)
        classic = patch('seed_prepare.extract_classic_policy', return_value=0)
        classic.start(); self.addCleanup(classic.stop)
        rng = synthetic_helmaroc_hashes()
        rng.__enter__()
        self.addCleanup(rng.__exit__, None, None, None)
        # This fixture supplies data operands only, not proprietary executable
        # bodies. Full policy hashing/coherence is tested independently.
        particle = patch('seed_prepare.extract_particle_policy', return_value=0)
        particle.start(); self.addCleanup(particle.stop)

        quiet = patch('seed_prepare.extract_quiet_sail', return_value=0)
        quiet.start()
        self.addCleanup(quiet.stop)
        music = patch('seed_prepare.extract_music_policy', return_value=0)
        music.start()
        self.addCleanup(music.stop)
        context = synthetic_compass_hashes()
        context.__enter__()
        self.addCleanup(context.__exit__, None, None, None)

    def images(self):
        original = {base: bytes(stride*256) for base,stride,_ in TABLES.values()}
        original[INFO_BASE] = bytes(1024)
        original.update(option_operands(False))
        original[COMPASS_FUNCTION] = synthetic_function()
        original[HELMAROC_FUNCTION] = synthetic_rng_function()
        from seed_windfall_data import ARYLL_ANIMATIONS
        original[ARYLL_ANIMATIONS] = bytes(224)
        from test_permanent_resource_heap import SYNTHETIC_BODY
        from permanent_resource_policy import START
        original[START] = SYNTHETIC_BODY
        seed = dict(original)
        table=bytearray(original[ARYLL_ANIMATIONS]);table[64]=5
        seed[ARYLL_ANIMATIONS]=bytes(table)
        import struct
        from title_seed_data import TITLE_FLOATS
        seed.update({a:struct.pack('>f',v) for a,v in TITLE_FLOATS})
        seed.update(option_operands(True))
        seed[COMPASS_FUNCTION] = synthetic_function(True)
        seed[HELMAROC_FUNCTION] = synthetic_rng_function(7)
        seed.update({address:bytes([0]) for address in BYTE_FIELDS.values()})
        seed[0x028F9F24] = (0xD73).to_bytes(4, 'big')
        seed[0x028FAC24] = bytes([0x40,0,0,0]); seed[0x028F8E10] = bytes([0,12])
        seed[0x028F8D0F] = bytes([255])*257
        for address,value in ((0x025B508C,44),(0x025B50C8,44),(0x025B5090,0),(0x025B50CC,0)):
            seed[address] = ((14<<26)|(3<<21)|value).to_bytes(4,'big')
        seed[REVEAL_CHART_SITE] = ((14<<26)|(10<<21)|1).to_bytes(4,'big')
        for address,op,first,base in [s for sites in REWARDS.values() for s in sites]+[s for pair in ALIASES for s in pair]:
            seed[address-3] = ((op<<26)|(first<<21)|(base<<16)|42).to_bytes(4,'big')
        for _,address in RAW_ALIASES: seed[address] = bytes([42])
        for sites in RAW_REWARDS.values():
            for address,size in sites:seed[address] = bytes([42])*size
        for _,address,cloud in BOSS_SITES:
            seed[address] = ((14<<26)|((7 if cloud else 4)<<21)|(42*256+255 if cloud else 42)).to_bytes(4,'big')
        for address in REWARD_TABLE_ADDRESSES: seed[address] = bytes([42])
        seed.update(native_operands())
        def reader(fields):
            def read(address,size):
                for base,value in fields.items():
                    if base<=address and address+size<=base+len(value):return value[address-base:address-base+size]
                raise ValueError('unmapped test data')
            return read
        return seed,reader(original),reader(seed)

    def test_complete_data_set_and_late_failure(self):
        seed, original_read, seed_read = self.images()
        result = generate_headers(original_read, seed_read)
        self.assertEqual(set(result), {'seed_init_data.h','seed_reward_data.h','seed_resources.h','seed_boss_data.h'})
        self.assertIn('init_reward_zunari[] = {42}', result['seed_init_data.h'])
        self.assertIn('init_instant_text_boxes = 1;', result['seed_init_data.h'])
        self.assertIn('#define init_invert_sea_compass_x_axis 1', result['seed_init_data.h'])
        self.assertIn('init_reward_boss[6] = {42,42,42,42,42,42}', result['seed_boss_data.h'])
        self.assertEqual(result['seed_reward_data.h'].count('WWHD_GAME_DATA('),37)
        seed[REWARD_TABLE_ADDRESSES[-1]] = b''
        with self.assertRaises(ValueError):generate_headers(original_read, seed_read)

    def test_preparation_refuses_existing_or_input_paths(self):
        from seed_prepare import prepare_seed
        with tempfile.TemporaryDirectory() as temp:
            original, seed, out = [Path(temp)/name for name in ('original','seed','out')]
            original.mkdir();seed.mkdir()
            _, first, second = self.images()
            identity = {'base_rpx_sha256':'a'*64,'seed_rpx_sha256':'b'*64,'compatibility':{}}
            with patch('seed_prepare._validated_readers', return_value=(first,second,identity)):
                record = prepare_seed(original,seed,out)
                self.assertFalse(record['installable'])
                self.assertEqual(len(record['headers']),4)
                self.assertEqual(json.loads((out/'seed-preparation.json').read_text()),record)
                with self.assertRaises(FileExistsError):prepare_seed(original,seed,out)
                for nested in (original/'out',seed/'out'):
                    with self.assertRaises(ValueError):prepare_seed(original,seed,nested)
                    self.assertFalse(nested.exists())

    def test_late_decode_error_creates_no_output(self):
        from seed_prepare import prepare_seed
        with tempfile.TemporaryDirectory() as temp:
            original, seed, out = [Path(temp)/name for name in ('original','seed','out')]
            original.mkdir();seed.mkdir()
            fields, first, second = self.images();fields[REWARD_TABLE_ADDRESSES[-1]]=b''
            with patch('seed_prepare._validated_readers', return_value=(first,second,{})), self.assertRaises(ValueError):
                prepare_seed(original,seed,out)
            self.assertFalse(out.exists())

    def test_file_reader_pins_actual_parsed_seed_identity(self):
        import sys
        from types import SimpleNamespace
        from unittest.mock import Mock
        from seed_prepare import _validated_readers
        fake_builds = SimpleNamespace(canonical_build=lambda:SimpleNamespace(sha256='a'*64))
        fake_compat = SimpleNamespace(RPX_LIMITS={'file':32}, identify=Mock(return_value=SimpleNamespace(sha256='b'*64,randomizer_report={})))
        parser = Mock(side_effect=[SimpleNamespace(buffer_sha256='a'*64), SimpleNamespace(buffer_sha256='c'*64)])
        modules = {'builds':fake_builds,'randomizer_compat':fake_compat,'source_core_compat':SimpleNamespace(identify_source_core=fake_compat.identify),'rpx':SimpleNamespace(Rpx=parser)}
        with patch.dict(sys.modules,modules), self.assertRaisesRegex(ValueError,'changed before seed data'):
            _validated_readers(Path('original.rpx'),Path('seed.rpx'))
        self.assertEqual(parser.call_count,2)
        parser = Mock(return_value=SimpleNamespace(buffer_sha256='wrong'))
        modules['rpx'] = SimpleNamespace(Rpx=parser);fake_compat.identify.reset_mock()
        with patch.dict(sys.modules,modules), self.assertRaisesRegex(ValueError,'unmodified USA'):
            _validated_readers(Path('original.rpx'),Path('seed.rpx'))
        fake_compat.identify.assert_not_called()

    def test_content_preparation_binds_complete_seed_identity(self):
        from seed_prepare import prepare_seed
        from seed_identity import seed_identity
        with tempfile.TemporaryDirectory() as temp:
            original, seed, out = [Path(temp)/name for name in ('original','seed','out')]
            for root in (original,seed):(root/'content').mkdir(parents=True)
            (seed/'content/item').write_bytes(b'asset')
            _, first, second = self.images()
            identity = {'base_rpx_sha256':'a'*64,'seed_rpx_sha256':'b'*64,'compatibility':{}}
            with patch('seed_prepare._validated_readers',return_value=(first,second,identity)):
                record = prepare_seed(original,seed,out,True)
            self.assertEqual(record['seed_identity'],seed_identity('b'*64,record['content']['seed_content_sha256']))
            self.assertEqual((out/'assets/content/item').read_bytes(),b'asset')


    def test_fixed_presentation_data_rejected_before_output(self):
        from seed_prepare import prepare_seed
        from seed_windfall_data import ARYLL_ANIMATIONS
        from title_seed_data import TITLE_FLOATS
        fields, original_read, seed_read = self.images()
        from permanent_resource_policy import START
        for address in (START, ARYLL_ANIMATIONS, *(a for a, _ in TITLE_FLOATS)):
            with self.subTest(address=address), tempfile.TemporaryDirectory() as directory:
                root=Path(directory);base=root/'base';seed=root/'seed';output=root/'output'
                base.mkdir();seed.mkdir()
                def altered(a, n):
                    data=seed_read(a,n)
                    return bytes([data[0]^1])+data[1:] if a==address else data
                with patch('seed_prepare._validated_readers',return_value=(original_read,altered,{})):
                    with self.assertRaises(ValueError):
                        prepare_seed(base,seed,output)
                self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
