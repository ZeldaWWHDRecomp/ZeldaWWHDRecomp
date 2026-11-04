"""Seven-function coherence and bounded full-body validation, without game bytes."""
from test_permanent_resource_heap import synthetic_permanent_hash
import hashlib
import unittest
from unittest.mock import patch
import seed_classic_policy as policy


class ClassicPolicy(unittest.TestCase):
    def test_coherence_and_negatives(self):
        catalog=[];variants=[]
        for i,(start,end,_,_) in enumerate(policy.FUNCTIONS):
            off=bytes((i*13+j*7)&255 for j in range(128+i*4))
            on=bytes(v^0x55 for v in off)
            variants.append((off,on));catalog.append((start,start+len(off),hashlib.sha256(off).hexdigest(),hashlib.sha256(on).hexdigest()))
        with patch.object(policy,'FUNCTIONS',catalog):
            for mask in range(128):
                bodies={start:variants[i][(mask>>i)&1] for i,(start,*_) in enumerate(catalog)}
                read=lambda address,size:bodies[address]
                if mask in (0,127):self.assertEqual(policy.extract_classic_policy(read),mask==127)
                else:
                    with self.assertRaisesRegex(ValueError,'incoherent'):policy.extract_classic_policy(read)
            for i,(start,*_) in enumerate(catalog):
                for variant in (0,1):
                    for offset in (0,len(variants[i][variant])//2,len(variants[i][variant])-1):
                        bodies={a:variants[j][variant] for j,(a,*_) in enumerate(catalog)}
                        bad=bytearray(bodies[start]);bad[offset]^=1;bodies[start]=bad
                        with self.assertRaisesRegex(ValueError,'unsupported'):policy.extract_classic_policy(lambda a,n:bodies[a])
                    for bad in (None,b'',variants[i][variant][:-1],variants[i][variant]+b'X'):
                        bodies={a:variants[j][variant] for j,(a,*_) in enumerate(catalog)};bodies[start]=bad
                        with self.assertRaisesRegex(ValueError,'truncated'):policy.extract_classic_policy(lambda a,n:bodies[a])

    def test_catalog_has_no_tiny_ranges(self):
        self.assertEqual(len(policy.FUNCTIONS),7)
        self.assertEqual(len({a for a,*_ in policy.FUNCTIONS}),7)
        for start,end,off,on in policy.FUNCTIONS:
            self.assertEqual(start&3,0);self.assertEqual(end&3,0)
            self.assertGreaterEqual(end-start,64)
            self.assertNotEqual(off,on)
            for value in (off,on):self.assertRegex(value,r'^[0-9a-f]{64}$')


class ClassicPreparation(unittest.TestCase):
    def test_metadata_domain(self):
        from seed_headers import render_classic_policy
        for value in (0, 1):
            self.assertEqual(render_classic_policy(value),
                             f'#define WWHD_RANDOMIZER_CLASSIC_MODE {value}\n')
        for value in (True, False, -1, 2, None, 0.0, '1'):
            with self.assertRaises(ValueError):
                render_classic_policy(value)

    def test_family_selected_before_artifacts(self):
        from contextlib import ExitStack
        from pathlib import Path
        import tempfile
        from seed_prepare import generate_headers, prepare_seed
        from test_seed_prepare import SeedPrepare
        from test_seed_compass_options import synthetic_compass_hashes
        from test_seed_helmaroc_rng import synthetic_helmaroc_hashes
        _, original, data = SeedPrepare().images()
        variants=[];catalog=[]
        for index,(start,end,_,_) in enumerate(policy.FUNCTIONS):
            off=bytes((index*17+j*31)&255 for j in range(128+index*4))
            on=bytes(v^0x55 for v in off)
            variants.append((off,on))
            catalog.append((start,start+len(off),hashlib.sha256(off).hexdigest(),hashlib.sha256(on).hexdigest()))
        def selected(mask):
            bodies={start:variants[i][(mask>>i)&1] for i,(start,*_) in enumerate(catalog)}
            return lambda address,size:bodies[address] if address in bodies else data(address,size)
        with ExitStack() as stack:
            stack.enter_context(synthetic_permanent_hash())
            stack.enter_context(patch.object(policy,'FUNCTIONS',catalog))
            stack.enter_context(synthetic_compass_hashes())
            stack.enter_context(synthetic_helmaroc_hashes())
            for name in ('music_policy','particle_policy','quiet_sail'):
                stack.enter_context(patch('seed_prepare.extract_'+name,return_value=0))
            for mask in (0,127):
                header=generate_headers(original,selected(mask))['seed_init_data.h']
                self.assertIn(f'#define WWHD_RANDOMIZER_CLASSIC_MODE {int(mask==127)}\n',header)
            with tempfile.TemporaryDirectory() as directory:
                root=Path(directory);base=root/'base';seed=root/'seed'
                base.mkdir();seed.mkdir()
                for mask in range(1,127):
                    output=root/f'prepared-{mask}'
                    with patch('seed_prepare._validated_readers',return_value=(original,selected(mask),{})):
                        with self.assertRaisesRegex(ValueError,'incoherent'):
                            prepare_seed(base,seed,output)
                    self.assertFalse(output.exists())
