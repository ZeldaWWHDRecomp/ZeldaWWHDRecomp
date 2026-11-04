"""Whole-consumer particle policy validation; synthetic bodies contain no game code."""
from test_permanent_resource_heap import synthetic_permanent_hash
import hashlib
from pathlib import Path
import unittest
from unittest.mock import patch
import seed_particle_policy
from seed_headers import render_particle_policy

class SeedParticlePolicy(unittest.TestCase):
    def test_full_body_policy(self):
        start,end,off,on=seed_particle_policy.FUNCTION
        self.assertEqual((start,end),(0x025A7838,0x025A7C30))
        self.assertGreaterEqual(end-start,64)
        self.assertNotEqual(off,on)
        a=bytes(n%251 for n in range(end-start));b=b'policy!!'+a[8:]
        catalog=(start,end,hashlib.sha256(a).hexdigest(),hashlib.sha256(b).hexdigest())
        with patch.object(seed_particle_policy,'FUNCTION',catalog):
            for value,body in enumerate((a,b)):
                self.assertEqual(seed_particle_policy.extract_particle_policy(lambda x,n:body),value)
                for index in (0,len(body)//2,len(body)-1):
                    bad=bytearray(body);bad[index]^=1
                    with self.assertRaisesRegex(ValueError,'unsupported'):
                        seed_particle_policy.extract_particle_policy(lambda x,n:bad)
                for bad in (None,b'',body[:-1],body+b'!'):
                    with self.assertRaisesRegex(ValueError,'truncated'):
                        seed_particle_policy.extract_particle_policy(lambda x,n:bad)

    def test_metadata(self):
        for value in (0,1):
            self.assertIn(f'#define WWHD_RANDOMIZER_PARTICLE_PERFORMANCE {value}\n',render_particle_policy(value))
        for value in (True,False,-1,2,None,0.0,'1'):
            with self.assertRaises(ValueError):render_particle_policy(value)

    def test_prepare_policy_required(self):
        from seed_prepare import generate_headers
        from test_seed_prepare import SeedPrepare
        from test_seed_compass_options import synthetic_compass_hashes
        _,original,seed=SeedPrepare().images()
        with synthetic_permanent_hash(), patch('seed_prepare.extract_music_policy',return_value=0), synthetic_compass_hashes(), patch('seed_prepare.extract_classic_policy',return_value=0), patch('seed_prepare.extract_quiet_sail',return_value=0), patch('seed_prepare.extract_helmaroc_rng',return_value={'fix_helmaroc_rng':0}):
            for value in (0,1):
                with patch('seed_prepare.extract_particle_policy',return_value=value):
                    headers=generate_headers(original,seed)
                    self.assertIn(render_particle_policy(value),headers['seed_init_data.h'])
            with patch('seed_prepare.extract_particle_policy',side_effect=ValueError('unsupported optional particle function')):
                with self.assertRaisesRegex(ValueError,'unsupported optional particle'):
                    generate_headers(original,seed)

    def test_complete_consumer_callback_matrix(self):
        import os
        import subprocess
        import tempfile
        source=Path(__file__).with_name('particle_performance_test.c')
        for value in (0,1):
            with tempfile.TemporaryDirectory() as directory:
                exe=Path(directory)/'test'
                subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror',f'-DWWHD_RANDOMIZER_PARTICLE_PERFORMANCE={value}',str(source),'-o',str(exe)],check=True)
                subprocess.run([str(exe)],check=True)
