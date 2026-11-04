"""Coherent full-function music decoder and compile-time-only consumer registration."""
from test_permanent_resource_heap import synthetic_permanent_hash
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import seed_music_policy
from seed_music_policy import extract_music_policy
from seed_headers import render_music_policy
from test_seed_compass_options import synthetic_compass_hashes
from test_seed_helmaroc_rng import synthetic_helmaroc_hashes


class SeedMusicPolicy(unittest.TestCase):
    def setUp(self):
        permanent = synthetic_permanent_hash()
        permanent.__enter__(); self.addCleanup(permanent.__exit__, None, None, None)
        for factory in (synthetic_compass_hashes, synthetic_helmaroc_hashes):
            context = factory()
            context.__enter__()
            self.addCleanup(context.__exit__, None, None, None)

    def synthetic_catalog(self):
        # Independent synthetic bodies exercise full-range hashing without
        # embedding copyrighted game instructions in the public test suite.
        catalog=[]
        disabled={}
        enabled={}
        for index,(start,end,_,_) in enumerate(seed_music_policy.FUNCTIONS):
            off=bytes((n*29+index*41)&255 for n in range(end-start))
            on=bytes([(index+113)&255])*8+off[8:]
            disabled[start],enabled[start]=off,on
            catalog.append((start,end,hashlib.sha256(off).hexdigest(),hashlib.sha256(on).hexdigest()))
        return tuple(catalog),disabled,enabled

    def test_full_catalog_and_coherent_states(self):
        self.assertEqual(len(seed_music_policy.FUNCTIONS),6)
        self.assertEqual({start for start,_,_,_ in seed_music_policy.FUNCTIONS},
                         {0x0202204C,0x0202879C,0x0201DE00,0x02027F04,0x02897F48,0x02030F1C})
        for start,end,first,second in seed_music_policy.FUNCTIONS:
            self.assertEqual((start&3,end&3),(0,0))
            self.assertGreaterEqual(end-start,64)
            self.assertNotEqual(first,second)
            for digest in (first,second):
                self.assertEqual(len(digest),64)
                self.assertTrue(all(c in '0123456789abcdef' for c in digest))
        catalog,disabled,enabled=self.synthetic_catalog()
        with patch.object(seed_music_policy,'FUNCTIONS',catalog):
            for mask in range(64):
                fields={start:(enabled if mask&(1<<index) else disabled)[start]
                        for index,(start,_,_,_) in enumerate(catalog)}
                def read(address,size):
                    self.assertEqual(size,len(fields[address]))
                    return fields[address]
                if mask in (0,63):
                    self.assertEqual(extract_music_policy(read),int(mask==63))
                else:
                    with self.assertRaisesRegex(ValueError,'incoherent'):
                        extract_music_policy(read)

    def test_unknown_and_short_full_bodies_rejected(self):
        catalog,disabled,enabled=self.synthetic_catalog()
        with patch.object(seed_music_policy,'FUNCTIONS',catalog):
            for start,end,_,_ in catalog:
                for initial in (disabled,enabled):
                    for offset in (0,(end-start)//2,end-start-1):
                        fields=dict(initial);body=bytearray(fields[start]);body[offset]^=1;fields[start]=body
                        with self.subTest(start=start,offset=offset),self.assertRaisesRegex(ValueError,'unsupported'):
                            extract_music_policy(lambda a,n:fields[a])
                    for wrong in (None,b'',initial[start][:-1],initial[start]+b'\0'):
                        fields=dict(initial);fields[start]=wrong
                        with self.assertRaisesRegex(ValueError,'truncated'):
                            extract_music_policy(lambda a,n:fields[a])

    def test_prepare_requires_coherent_policy_before_output(self):
        from seed_prepare import generate_headers, prepare_seed
        from test_seed_prepare import SeedPrepare
        from test_seed_compass_options import synthetic_compass_hashes
        _, original_read, data_read = SeedPrepare().images()
        catalog, disabled, enabled = self.synthetic_catalog()
        def combined(fields):
            def read(address, size):
                if address in fields:
                    self.assertEqual(size, len(fields[address]))
                    return fields[address]
                return data_read(address, size)
            return read
        with patch.object(seed_music_policy, 'FUNCTIONS', catalog), synthetic_compass_hashes(), patch('seed_prepare.extract_classic_policy', return_value=0), patch('seed_prepare.extract_quiet_sail', return_value=0), patch('seed_prepare.extract_particle_policy', return_value=0):
            for value, fields in ((0, disabled), (1, enabled)):
                headers = generate_headers(original_read, combined(fields))
                self.assertIn(f'#define WWHD_RANDOMIZER_REMOVE_MUSIC {value}\n',
                              headers['seed_init_data.h'])
            mixed = dict(disabled)
            mixed[catalog[0][0]] = enabled[catalog[0][0]]
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                original, seed, output = root/'base', root/'seed', root/'prepared'
                original.mkdir(); seed.mkdir()
                with patch('seed_prepare._validated_readers', return_value=(original_read, combined(mixed), {})):
                    with self.assertRaisesRegex(ValueError, 'incoherent'):
                        prepare_seed(original, seed, output)
                self.assertFalse(output.exists())

    def test_generated_policy_domain(self):
        for enabled in (0,1):
            rendered=render_music_policy(enabled)
            self.assertIn(f'#define WWHD_RANDOMIZER_REMOVE_MUSIC {enabled}\n',rendered)
            self.assertIn('init_remove_music = WWHD_RANDOMIZER_REMOVE_MUSIC;',rendered)
        for value in (True,False,-1,2,0.0,None,'1;bad()'):
            with self.assertRaises(ValueError):render_music_policy(value)

    def test_conditional_consumers_and_returns(self):
        header=Path(__file__).with_name('remove_music.h')
        for enabled in (0,1):
            source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uint32_t u32;typedef float f32;
POLICY
enum { before=__COUNTER__ };
#define WWHD_REPLACE(address,result,name,params) enum { name##_descriptor=__COUNTER__ }; result name params
#include "remove_music.h"
enum { after=__COUNTER__ };
int main(void){
 assert(after-before-1==(WWHD_RANDOMIZER_REMOVE_MUSIC?6:0));
 assert(init_remove_music==WWHD_RANDOMIZER_REMOVE_MUSIC);
#if WWHD_RANDOMIZER_REMOVE_MUSIC
 unsigned char memory[64],original[64];memset(memory,0xA5,sizeof memory);memcpy(original,memory,sizeof memory);
 for(u32 value=0;value<65536;value++){
  randomizer_music_bgm_start(value,value^0xFFFF,value+1,value-1);
  randomizer_music_battle(value,(f32)value);randomizer_music_sub_bgm(value,~value);randomizer_music_stream(value);
  assert(randomizer_music_prepare_label(value,0xFFFFFFFF,~value,0)==2);
  assert(randomizer_music_stream_ready(value)==1);
 }
 assert(!memcmp(memory,original,sizeof memory));
#endif
 return 0;
}
'''.replace('POLICY',render_music_policy(enabled))
            with tempfile.TemporaryDirectory() as directory:
                c,exe=Path(directory)/'test.c',Path(directory)/'test'
                c.write_text(source)
                subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(header.parent),str(c),'-o',str(exe)],check=True)
                subprocess.run([str(exe)],check=True)
        # Missing or corrupt generated metadata must fail closed, not assume OFF.
        for prefix in ('','#define WWHD_RANDOMIZER_REMOVE_MUSIC 2\n'):
            with tempfile.TemporaryDirectory() as directory:
                c=Path(directory)/'bad.c';c.write_text(prefix+'#include "remove_music.h"\n')
                result=subprocess.run([os.environ.get('CC','clang'),'-std=c11','-fsyntax-only','-I',str(header.parent),str(c)],capture_output=True)
                self.assertNotEqual(result.returncode,0)
                self.assertIn(b'music policy',result.stderr)
