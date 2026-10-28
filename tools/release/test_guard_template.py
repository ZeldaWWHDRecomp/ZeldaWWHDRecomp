"""Synthetic release-template security boundaries; contains no game records."""
import json
import struct
import tempfile
from pathlib import Path
import zipfile
import unittest
import zlib
import guard

PATH = 'tools/shadercache/template.bin'

def fixture():
    return {'version': 3, 'states': [{'hash': 1, 'vertex': 1,
        'fetch': {'hash': 2, 'compact': 1, 'metal_hash': 3, 'attributes': [dict(zip(
            ('buffer','semantic','format','fetch_type','numeric','signed','endian','x','y','z','w','offset','divisor'),
            (0,0,1,0,0,0,0,0,1,2,3,0,0)))]},
        'registers': [[0xa21a, 0]]}], 'pipelines': [{'backend':'metal',
        'shader_hashes':[1,2,3], 'formats':[0]*9, 'blend':[0]*8,
        'color_control':0, 'target_mask':0, 'strides':[0]*16}]}

def encode(doc):
    return b'WWHT0003'+zlib.compress(json.dumps(doc).encode())

def problems(data, name=PATH):
    result=[]
    guard.check_entry(name, data, result)
    return result

class SealedTemplateGuard(unittest.TestCase):
    """A sealed template (shaderprep_format.seal) passes only when it opens with WWHD_TEMPLATE_KEY and holds a
    code-free template; sealed with the shaderprep implementation, opened with the guard's own."""
    def sealed(self, doc, key):
        import os, sys
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
        import shaderprep_format
        return shaderprep_format.seal(encode(doc), key)

    def test_sealed_needs_the_key_and_a_code_free_content(self):
        from unittest.mock import patch
        key = bytes(range(32))
        data = self.sealed(fixture(), key)
        with patch.dict('os.environ', {'WWHD_TEMPLATE_KEY': ''}):
            self.assertTrue(problems(data))
        with patch.dict('os.environ', {'WWHD_TEMPLATE_KEY': '11' * 32}):
            self.assertTrue(problems(data))
        with patch.dict('os.environ', {'WWHD_TEMPLATE_KEY': key.hex()}):
            self.assertEqual(problems(data), [])
            self.assertTrue(problems(data, 'other.bin'))
            doc = fixture(); doc['states'][0]['code'] = [1]
            self.assertTrue(problems(self.sealed(doc, key)))

class TemplateGuard(unittest.TestCase):
    def test_synthetic_safe_template_only_at_approved_path(self):
        for name in (PATH, 'Release/'+PATH):
            self.assertEqual(problems(encode(fixture()), name), [])
        for name in ('template.bin', 'other.bin', '../'+PATH, 'a/b/'+PATH):
            self.assertTrue(problems(encode(fixture()), name))

    def test_explicit_linked_vertex_is_only_typed_pixel_metadata(self):
        doc=fixture(); state=doc['states'][0]
        companion={'hash':4,'fetch':state['fetch']}
        state.update(vertex=0, fetch=None, linked_vertex=companion)
        self.assertEqual(problems(encode(doc)), [])
        for field in ('microcode','spirv','fetch_bytes','unknown'):
            bad=json.loads(json.dumps(doc)); bad['states'][0]['linked_vertex'][field]=[1,2,3]
            self.assertTrue(problems(encode(bad)))
        bad=json.loads(json.dumps(doc)); bad['states'][0]['vertex']=1
        self.assertTrue(problems(encode(bad)))
        bad=json.loads(json.dumps(doc)); bad['states'][0]['linked_vertex']['fetch']=None
        self.assertTrue(problems(encode(bad)))
        bad=json.loads(json.dumps(doc)); bad['states'][0]['linked_vertex']['hash']=True
        self.assertTrue(problems(encode(bad)))
        state['linked_vertex']=None
        self.assertEqual(problems(encode(doc)), [])

    def test_typed_vulkan_recipe_and_code_injection(self):
        pipeline={'backend':'vulkan','shader_hashes':[1,2,3], 'key_words':[0]*46,
            'dynamic_uniforms':0,'descriptors':[[],[]],'rendering':[0,0,0],
            'formats':[],'bindings':[],'attributes':[], 'attachments':[], 'dynamics':[0,1,4,8],
            'state':{'assembly':[0,3,0], 'viewport':[0,1,1],
                'raster':[0,0,0,0,0,0,0,0,0,0,0x3f800000],
                'multisample':[0,1,0,0,0,0], 'depth':[0,0,0,0,0,0],
                'front_stencil':[0]*7,'back_stencil':[0]*7,'depth_bounds':[0,0x3f800000],
                'blend':[0,0,0],'blend_constants':[0]*4}}
        doc=fixture(); doc['pipelines']=[pipeline]
        self.assertEqual(problems(encode(doc)), [])
        for field in ('spirv','native_cache','device_uuid','microcode'):
            bad=json.loads(json.dumps(doc)); bad['pipelines'][0][field]=[1,2,3]
            self.assertTrue(problems(encode(bad)))
        bad=json.loads(json.dumps(doc)); bad['pipelines'][0]['state']['raster'].append(0)
        self.assertTrue(problems(encode(bad)))

    def test_actual_directory_and_zip_release_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)/'release'
            template=root/PATH
            template.parent.mkdir(parents=True)
            template.write_bytes(encode(fixture()))
            self.assertEqual(guard.scan(str(root)), ([], 1))
            archive=Path(tmp)/'release.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.write(template, 'Release/'+PATH)
            self.assertEqual(guard.scan(str(archive)), ([], 1))
            unsafe=fixture(); unsafe['states'][0]['microcode']=[1,2,3,4]
            template.write_bytes(encode(unsafe))
            self.assertTrue(guard.scan(str(root))[0])
            with zipfile.ZipFile(archive, 'w') as z:
                z.write(template, 'Release/'+PATH)
            self.assertTrue(guard.scan(str(archive))[0])

    def test_code_or_unknown_fields_rejected_at_every_level(self):
        for target in ('root','state','fetch','attribute','pipeline'):
            for field in ('microcode','fetch_bytes','spirv','native_cache','device_uuid','unknown'):
                doc=fixture()
                obj={'root':doc,'state':doc['states'][0], 'fetch':doc['states'][0]['fetch'],
                     'attribute':doc['states'][0]['fetch']['attributes'][0], 'pipeline':doc['pipelines'][0]}[target]
                obj[field]=[3,2,35,7]
                with self.subTest(target=target, field=field):
                    self.assertTrue(problems(encode(doc)))

    def test_types_ranges_pointer_registers_and_duplicates(self):
        for value in (True, -1, 1<<64, 'code'):
            doc=fixture(); doc['states'][0]['hash']=value
            self.assertTrue(problems(encode(doc)))
        for registers in ([[0xa210,0]], [[0xa216,0]], [[0xa225,0]], [[0xc000,0]], [[0xc400,0]], [[0xdfff,0]], [[1,0]], [[0xa21a,0],[0xa21a,1]]):
            doc=fixture(); doc['states'][0]['registers']=registers
            self.assertTrue(problems(encode(doc)))
        for value in (-1, 2, True, 'compact'):
            doc=fixture(); doc['states'][0]['fetch']['compact']=value
            self.assertTrue(problems(encode(doc)))
        for value in (-1, 1<<64, True, 'code'):
            doc=fixture(); doc['states'][0]['fetch']['metal_hash']=value
            self.assertTrue(problems(encode(doc)))
        doc=fixture(); del doc['states'][0]['fetch']['metal_hash']
        self.assertTrue(problems(encode(doc)))
        doc=fixture(); del doc['states'][0]['fetch']['compact']
        self.assertTrue(problems(encode(doc)))
        doc=fixture(); doc['states'][0]['fetch']['attributes']*=65
        self.assertTrue(problems(encode(doc)))
        doc=fixture(); doc['pipelines'][0]['backend']='unknown'
        self.assertTrue(problems(encode(doc)))

    def test_envelope_duplicate_keys_trailing_and_truncation(self):
        safe=encode(fixture())
        for data in (safe+b'code', safe+zlib.compress(b'{}'), safe[:-1],
                     b'WWHT0003'+zlib.compress(b'{"version":3,"version":3,"states":[],"pipelines":[]}'),
                     b'WWHT0003'+zlib.compress(b'{"version":NaN,"states":[],"pipelines":[]}')):
            self.assertTrue(problems(data))

    def test_existing_code_cache_recording_checks_retained_even_renamed(self):
        for data in (b'WWHT'+zlib.compress(b'synthetic code'), b'WWVKSC', b'WWVKPC',
                     b'WWVKPR', b'WWVKST01', b'WWFA0001', b'\x03\x02\x23\x07',
                     struct.pack('<III',1,4,len(zlib.compress(b'code')))+zlib.compress(b'code')):
            self.assertTrue(problems(data, 'renamed.dat'))
        self.assertTrue(problems(b'ordinary bytes'))

if __name__ == '__main__': unittest.main()
