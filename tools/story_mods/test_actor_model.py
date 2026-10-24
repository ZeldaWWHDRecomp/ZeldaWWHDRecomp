"""Original synthetic fixtures: no game resources required."""
import copy
import hashlib
import collision_mesh
import importlib.util
import json
import os
import pathlib
import struct
import tempfile
import unittest

import actor_model as model
import test_notebook_model as fixture

ROOT=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('carved_post',ROOT/'examples/guest-mods/sdk-v3-windfall/assets/make_carved_post.py')
asset=importlib.util.module_from_spec(spec); spec.loader.exec_module(asset)


def static_template():
    data = bytearray(fixture.template())
    # Original synthetic one-root skeleton with a tilted/translating rest pose.
    fm, skeleton, bone = 0x200, 0x700, 0x740
    struct.pack_into('>i', data, fm+0xC, skeleton-(fm+0xC))
    data[skeleton:skeleton+4] = b'FSKL'
    struct.pack_into('>HHH', data, skeleton+8, 1, 0, 0)
    struct.pack_into('>i', data, skeleton+0x14, bone-(skeleton+0x14))
    struct.pack_into('>H', data, bone+6, 0xFFFF)
    struct.pack_into('>I', data, bone+0x10, 0x03001001)
    struct.pack_into('>3f', data, bone+0x14, 2, 3, 4)
    struct.pack_into('>4f', data, bone+0x20, .2, .3, .4, 1)
    struct.pack_into('>3f', data, bone+0x30, 10, 20, 30)
    return bytes(data)


def mutate(data, change):
    length=struct.unpack_from('<I',data,12)[0]; doc=json.loads(data[20:20+length]); change(doc)
    text=json.dumps(doc).encode(); text+=b' '*(-len(text)%4)
    body=struct.pack('<I4s',len(text),b'JSON')+text+data[20+length:]
    return b'glTF'+struct.pack('<II',2,12+len(body))+body


class ActorModelTests(unittest.TestCase):
    def parse(self,data):
        with tempfile.TemporaryDirectory() as directory:
            path=os.path.join(directory,'model.glb'); pathlib.Path(path).write_bytes(data)
            return model.read_glb(path)

    def test_original_asset_reproducible(self):
        data=asset.make()
        self.assertEqual(data,(ROOT/'examples/guest-mods/sdk-v3-windfall/assets/carved_post.glb').read_bytes())
        geometry,info=self.parse(data)
        self.assertEqual(info,dict(vertices=24,triangles=12,texture_width=128,texture_height=128))
        self.assertAlmostEqual(max(p[1] for p in geometry[0]),160,places=4)
        self.assertEqual(min(p[1] for p in geometry[0]),0)

    def test_transform_and_mirrored_winding(self):
        original,_=self.parse(asset.make())
        data=mutate(asset.make(),lambda d:d['nodes'][0].update(translation=[1,2,3],scale=[-2,1,1]))
        transformed,_=self.parse(data)
        self.assertAlmostEqual(transformed[0][0][0],160,places=4)
        self.assertAlmostEqual(transformed[0][0][1],200,places=4)
        self.assertEqual(transformed[3][:3],[original[3][0],original[3][2],original[3][1]])

    def test_golden_bfres_structure_identity_and_origin(self):
        geometry,_=self.parse(asset.make())
        # Synthetic fixture carries strings but no original resources.
        output=model.build_bfres(static_template(),geometry,'mod_123abc')
        f=model.base.Bfres(output); dictionary=f.rel(0x20); name,fm=f.dict(dictionary)[0]
        self.assertEqual(name,'mod_123abc'); self.assertEqual(f.cstr(f.rel(fm+4)),name)
        self.assertEqual(f.u32(dictionary+24),(len(name)-1)*8+(ord(name[-1]).bit_length()-1))
        self.assertEqual(struct.unpack_from('>IHH',output,dictionary+8),(0xffffffff,1,0))
        vertices=f.rel(f.rel(f.rel(fm+0x10)+0x18)+0x14)
        for actual, expected in zip(struct.unpack_from('>3f',output,vertices),geometry[0][0]): self.assertAlmostEqual(actual,expected,places=4)
        shape=f.dict(f.rel(fm+0x14))[0][1]; box=f.rel(shape+0x34)
        self.assertAlmostEqual(struct.unpack_from('>3f',output,box)[1],80,places=4)
        mesh=f.rel(shape+0x24); self.assertEqual(f.u32(mesh+8),36)
        self.assertEqual(f.u32(0xC),len(output))
        skeleton=f.rel(fm+0xC); bone=f.rel(skeleton+0x14)
        self.assertEqual(struct.unpack_from('>3f',output,bone+0x14),(1,1,1))
        self.assertEqual(struct.unpack_from('>4f',output,bone+0x20),(0,0,0,1))
        self.assertEqual(struct.unpack_from('>3f',output,bone+0x30),(0,0,0))
        self.assertEqual(f.u32(bone+0x10),0x03001001)

    def test_reject_invalid_contract(self):
        changes=[lambda d:d.update(skins=[{}]),lambda d:d.update(animations=[{}]),lambda d:d['nodes'][0].update(scale=[0,1,1]),lambda d:d['nodes'][0].update(translation=[float('nan'),0,0]),lambda d:d['accessors'][0].update(count=4097),lambda d:d['accessors'][3].update(count=24579),lambda d:d['accessors'][0].update(byteOffset=999999),lambda d:d['meshes'][0]['primitives'][0].update(mode=1),lambda d:d['buffers'][0].update(uri='external.bin'),lambda d:d['nodes'][0].update(children=[0]),lambda d:d['accessors'][0].update(sparse={}),lambda d:d['materials'][0].update(alphaMode='BLEND')]
        for change in changes:
            with self.subTest(change=change),self.assertRaises(model.BuildError): self.parse(mutate(asset.make(),change))

    def test_output_validator_rejects_corruption(self):
        geometry,_=self.parse(asset.make())
        output=bytearray(model.build_bfres(static_template(),geometry,'unique'))
        struct.pack_into('>I',output,0xC,1)
        with self.assertRaises(model.BuildError): model.validate_bfres(output,'unique',24,36)

    def test_nonuniform_normal_transform_and_hierarchy(self):
        def change(doc):
            doc['nodes']=[{'translation':[1,0,0],'children':[1]},{'mesh':0,'scale':[2,3,4]}]
        geometry,_=self.parse(mutate(asset.make(),change))
        self.assertAlmostEqual(geometry[0][0][0],40,places=4)
        for normal in geometry[1]: self.assertAlmostEqual(sum(v*v for v in normal),1)

    def test_png_bounds_and_crc(self):
        for w,h in ((513,8),(64,513),(63,8),(64,7)):
            with self.assertRaises(model.BuildError): model.png(fixture.png(w,h,bytes(w*h*4)))
        image=bytearray(fixture.png(64,8,bytes(64*8*4))); image[-1]^=1
        with self.assertRaises(model.BuildError): model.png(image)

    def test_truncated_and_oversized(self):
        for data in (b'',asset.make()[:-1],asset.make()[:20]):
            with self.assertRaises(model.BuildError): self.parse(data)
        with self.assertRaises(model.BuildError): self.parse(b'x'*(model.MAX_SOURCE+1))

    def test_identity(self):
        geometry,_=self.parse(asset.make())
        for name in ('../bad','UPPER','a'*32,''):
            with self.assertRaises(model.BuildError): model.build_bfres(static_template(),geometry,name)

    def test_archive_and_cache_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            game=pathlib.Path(directory); archive=game/model.base.ARCHIVE; archive.parent.mkdir(parents=True)
            shader=b'original synthetic shader marker'
            archive.write_bytes(model.base.yaz0_stored(model.base.sarc({'Vbeso.bfres':static_template(),'vbeso.sharcfb':shader})))
            glb=game/'source.glb'; glb.write_bytes(asset.make()); out=str(game/'cache'/'unique.szs')
            one=model.build(str(game),str(glb),'unique',out)
            files=dict(model.base.shaderprep.sarc(model.base.shaderprep.yaz0(pathlib.Path(out).read_bytes())))
            self.assertEqual(set(files),{'unique.bfres','unique.sharcfb'}); self.assertEqual(files['unique.sharcfb'],shader)
            self.assertEqual(json.loads(pathlib.Path(out+'.json').read_text()),one)
            before = pathlib.Path(out).stat().st_mtime_ns
            from unittest.mock import patch
            with patch.object(model,'build_bfres',side_effect=AssertionError('valid cache was rebuilt')):
                two=model.build(str(game),str(glb),'unique',out)
            self.assertEqual(one,two); self.assertEqual(before,pathlib.Path(out).stat().st_mtime_ns)
            pathlib.Path(out).write_bytes(b'corrupt')
            rebuilt=model.build(str(game),str(glb),'unique',out); self.assertEqual(rebuilt,one)
            pathlib.Path(out).unlink()
            self.assertEqual(model.build(str(game),str(glb),'unique',out),one)
            pathlib.Path(out+'.json').write_text('{invalid')
            self.assertEqual(model.build(str(game),str(glb),'unique',out),one)
            # Player-local template changes invalidate the key even when source
            # geometry and identity stay unchanged.
            archive.write_bytes(model.base.yaz0_stored(model.base.sarc({'Vbeso.bfres':static_template(),'vbeso.sharcfb':shader+b'new'})))
            changed=model.build(str(game),str(glb),'unique',out)
            self.assertNotEqual(one['cache_key'],changed['cache_key'])
            glb.write_bytes(mutate(asset.make(),lambda doc:doc['nodes'][0].update(translation=[1,0,0])))
            changed_source=model.build(str(game),str(glb),'unique',out)
            self.assertNotEqual(changed['cache_key'],changed_source['cache_key'])
            other=model.build(str(game),str(glb),'other',out); self.assertNotEqual(one['cache_key'],other['cache_key'])


class CollisionMeshTests(unittest.TestCase):
    @staticmethod
    def original():
        data=asset.make(); length=struct.unpack_from('<I',data,12)[0]
        doc=json.loads(data[20:20+length]); binary=data[28+length:]
        collision=copy.deepcopy(doc['meshes'][0]); collision['name']='island_col'
        collision['primitives'][0]['attributes']={'POSITION':0}
        collision['primitives'][0]['material']=1
        doc['meshes'].append(collision); doc['materials'].append({'name':'col_grass'})
        doc['nodes'].append({'mesh':1,'translation':[2,0,0]})
        doc['scenes'][0]['nodes'].append(1)
        return doc,binary

    def parse(self, doc=None, binary=None, request=True):
        if doc is None: doc,binary=self.original()
        with tempfile.TemporaryDirectory() as directory:
            path=pathlib.Path(directory)/'island.glb';path.write_bytes(collision_mesh._pack(doc,binary))
            return collision_mesh.read_static(path,request)

    def test_explicit_collision_excluded_from_render(self):
        geometry,info,data=self.parse()
        self.assertEqual((info['vertices'],info['triangles']),(24,12))
        self.assertEqual((info['collision_vertices'],info['collision_triangles']),(8,12))
        self.assertLess(max(p[0] for p in geometry[0]),100)
        magic,version,v,t,*bounds=struct.unpack_from('>4sIII6f',data)
        self.assertEqual((magic,version,v,t),(b'WCOL',1,8,12))
        self.assertAlmostEqual(bounds[0],170,places=4)
        self.assertEqual(len(data),40+12*v+8*t)
        for i in range(t):self.assertEqual(struct.unpack_from('>4H',data,40+12*v+i*8)[3],1)
        doc,binary=self.original()
        with tempfile.TemporaryDirectory() as directory:
            path=pathlib.Path(directory)/'island.glb';path.write_bytes(collision_mesh._pack(doc,binary))
            with self.assertRaises(model.BuildError):model.read_glb(path) # skinned dependency remains strict
            self.assertIn('collision_triangles',model.inspect(path))

    def test_fallback_welds_render_positions(self):
        with tempfile.TemporaryDirectory() as directory:
            path=pathlib.Path(directory)/'render.glb';path.write_bytes(asset.make())
            _,info,data=collision_mesh.read_static(path,True)
            self.assertEqual((info['collision_vertices'],info['collision_triangles']),(8,12))
            self.assertEqual(hashlib.sha256(data).hexdigest(),info['collision_sha256'])
            self.assertIsNone(collision_mesh.read_static(path,False)[2])

    def test_materials_hierarchy_mirror_and_determinism(self):
        doc,binary=self.original()
        doc['nodes'][1]={'translation':[2,1,0],'children':[2]}
        doc['nodes'].append({'mesh':1,'scale':[-1,2,3]})
        second=copy.deepcopy(doc['meshes'][1]['primitives'][0]);second['material']=2
        doc['materials'].append({'name':'col_wall'});doc['meshes'][1]['primitives'].append(second)
        _,info,data=self.parse(doc,binary)
        self.assertEqual(info['collision_triangles'],24)
        self.assertEqual(self.parse(doc,binary)[2],data)
        self.assertAlmostEqual(info['collision_minimum'][1],100)
        self.assertAlmostEqual(info['collision_maximum'][1],420)
        v=info['collision_vertices'];triangles=[struct.unpack_from('>4H',data,40+12*v+i*8) for i in range(24)]
        self.assertEqual(set(t[3] for t in triangles),{1,5})
        # Mirroring keeps the original closed mesh's outward winding.
        points=[struct.unpack_from('>3f',data,40+i*12) for i in range(v)]
        signed_volume=sum(sum(points[a][k]*(points[b][(k+1)%3]*points[c][(k+2)%3]-points[b][(k+2)%3]*points[c][(k+1)%3]) for k in range(3)) for a,b,c,_ in triangles[:12])
        self.assertGreater(signed_volume,0)

    def test_reject_bad_collision_contract(self):
        changes=[lambda d:d['materials'][1].update(name='col_unknown'),
                 lambda d:d['nodes'][1].update(scale=[0,1,1]),
                 lambda d:d['nodes'][1].update(scale=[1667,1,1]),        # 1000.2 m wide: past one sea square
                 lambda d:d['nodes'][1].update(translation=[1000,0,0]),  # a coordinate past 100000 units
                 lambda d:d['nodes'][1].update(translation=[float('nan'),0,0]),
                 lambda d:d['meshes'][1]['primitives'][0].update(mode=1),
                 lambda d:d['accessors'][3].update(byteOffset=99999),
                 lambda d:d['accessors'][0].update(count=4097),
                 lambda d:d['nodes'][1].update(children=[1]),
                 lambda d:d['scenes'][0].update(nodes=[0])]
        for change in changes:
            doc,binary=self.original();change(doc)
            with self.subTest(change=change),self.assertRaises(model.BuildError):self.parse(doc,binary)
        for change in (lambda d:d['nodes'][1].update(scale=[1000,1,1]),lambda d:d['nodes'][1].update(translation=[201,0,0])):  # past the old 200 m
            doc,binary=self.original();change(doc);self.assertGreater(self.parse(doc,binary)[1]['collision_triangles'],0)
        with self.assertRaises(model.BuildError):collision_mesh.encode([(0,0,0),(1,0,0),(2,0,0)],[(0,1,2,0)],model)
        with self.assertRaises(model.BuildError):collision_mesh.encode([(0,0,0),(1,0,0),(0,1,0)],[(0,1,2,0)]*8193,model)

    def test_collision_extent_is_one_sea_square(self):
        # story_world_collision_mesh.h MaxExtent: 100000 units (1 km) along each axis, every coordinate within it
        self.assertEqual(collision_mesh.MAX_EXTENT,100000)
        above=struct.unpack('>f',struct.pack('>I',struct.unpack('>I',struct.pack('>f',100000.0))[0]+1))[0]  # the next float32
        h=50000.0
        square=[(-h,0,-h),(h,0,-h),(h,0,h),(-h,0,h),(0,40000,0)]
        body,info=collision_mesh.encode(square,[(0,2,1,1),(0,3,2,1),(0,1,4,0)],model)
        self.assertEqual((info['collision_minimum'],info['collision_maximum']),([-h,0,-h],[h,40000,h]))
        # the old 200 m mark: a 400 m island is one body
        collision_mesh.encode([(-20000,0,-20000),(20000,0,-20000),(20000,0,20000)],[(0,2,1,0)],model)
        collision_mesh.encode([(0,0,0),(100000,0,0),(0,0,100000)],[(0,2,1,0)],model)  # coordinates up to the bound
        for wide in ([(-h,0,-h),(above-h,0,-h),(h,0,h)],                  # extent just above along x
                     [(-h,0,-h),(h,0,above-h),(h,0,h)],                  # along z
                     [(0,-h,0),(1,above-h,0),(0,0,1)],                   # along y
                     [(0,0,0),(above,0,0),(0,0,1)],                       # a coordinate just above
                     [(0,0,0),(1,0,0),(0,0,-above)]):
            with self.subTest(wide=wide),self.assertRaises(model.BuildError):collision_mesh.encode(wide,[(0,2,1,0)],model)

    def test_original_island_reproducible_and_known_collision(self):
        path=ROOT/'examples/guest-mods/world-objects-w1/assets/make_fixtures.py'
        spec=importlib.util.spec_from_file_location('original_island',path)
        fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
        self.assertEqual(fixture.make(),path.with_name('island.glb').read_bytes())
        _,info,data=collision_mesh.read_static(path.with_name('island.glb'),True)
        self.assertEqual((info['collision_vertices'],info['collision_triangles'],len(data)),(24,40,648))
        self.assertEqual(info['collision_minimum'],[-1200.,-900.,-1900.])
        self.assertEqual(info['collision_maximum'],[1200.,800.,1000.])
        semantics=[struct.unpack_from('>4H',data,40+24*12+i*8)[3] for i in range(40)]
        self.assertEqual({v:semantics.count(v) for v in set(semantics)},{1:10,2:12,3:18})

    def test_floating_assets_and_object_envelope_semantic(self):
        path=ROOT/'examples/guest-mods/world-objects-w2/assets/make_fixtures.py'
        spec=importlib.util.spec_from_file_location('floating_assets',path)
        fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
        for name,counts in (('raft',(16,20)),('buoy',(18,32)),('flotsam',(16,20))):
            source=path.with_name(name+'.glb')
            self.assertEqual(fixture.make(name),source.read_bytes())
            _,info,data=collision_mesh.read_static(source,True)
            self.assertEqual((info['collision_vertices'],info['collision_triangles']),counts)
            points=[struct.unpack_from('>3f',data,40+i*12) for i in range(counts[0])]
            triangles=[struct.unpack_from('>4H',data,40+counts[0]*12+i*8) for i in range(counts[1])]
            envelope=[t for t in triangles if t[3]==6]
            if name=='buoy':
                self.assertFalse(envelope)
                self.assertEqual({t[3] for t in triangles},{4})
            else:
                self.assertEqual(len(envelope),8)
                # Every object-envelope face is vertical; no extra standable cap.
                for a,b,c,_ in envelope:
                    p,q,r=points[a],points[b],points[c]
                    u=[q[k]-p[k] for k in range(3)];v=[r[k]-p[k] for k in range(3)]
                    self.assertEqual(u[2]*v[0]-u[0]*v[2],0)
                self.assertEqual({t[3] for t in triangles},{2,6})
        doc,binary=self.original();doc['materials'][1]['name']='col_objects'
        _,_,data=self.parse(doc,binary)
        self.assertEqual(struct.unpack_from('>H',data,40+8*12+6)[0],6)

    def test_interactive_materials_and_original_tower(self):
        for name,semantic in [('col_climb',7),('col_hookshot',8)]:
            doc,binary=self.original();doc['materials'][1]['name']=name
            _,_,data=self.parse(doc,binary)
            self.assertEqual(struct.unpack_from('>H',data,40+8*12+6)[0],semantic)
        source=ROOT/'examples/guest-mods/world-objects-w3/assets/tower.glb'
        spec=importlib.util.spec_from_file_location('tower_fixture',source.with_name('make_fixtures.py'))
        generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator)
        self.assertEqual(source.read_bytes(),generator.make())
        _,info,data=collision_mesh.read_static(source,True)
        v,t=info['collision_vertices'],info['collision_triangles']
        triangles=[struct.unpack_from('>4H',data,40+v*12+i*8) for i in range(t)]
        self.assertEqual({x[3] for x in triangles},{0,2,7,8})
        self.assertEqual(sum(x[3]==7 for x in triangles),2)
        points=[struct.unpack_from('>3f',data,40+i*12) for i in range(v)]
        for a,b,c,semantic in triangles:
            if semantic!=7:continue
            p,q,r=points[a],points[b],points[c]
            u=[q[k]-p[k] for k in range(3)];w=[r[k]-p[k] for k in range(3)]
            self.assertEqual(u[2]*w[0]-u[0]*w[2],0)
            self.assertLess(u[0]*w[1]-u[1]*w[0],0)

    def test_collision_cache_rebuilds_damaged_or_missing_sidecar(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory);archive=root/model.base.ARCHIVE;archive.parent.mkdir(parents=True)
            archive.write_bytes(model.base.yaz0_stored(model.base.sarc({'Vbeso.bfres':static_template(),'vbeso.sharcfb':b'synthetic'})))
            source=root/'source.glb';source.write_bytes(asset.make());out=str(root/'cache/model.szs');col=str(root/'cache/model.wcol')
            plain=model.build(str(root),str(source),'unique',out)
            one=model.build(str(root),str(source),'unique',out,col)
            self.assertNotEqual(plain['cache_key'],one['cache_key'])
            expected=pathlib.Path(col).read_bytes()
            with patch.object(model,'build_bfres',side_effect=AssertionError('cache rebuilt')):
                self.assertEqual(model.build(str(root),str(source),'unique',out,col),one)
            for damage in ('missing','corrupt'):
                if damage=='missing':pathlib.Path(col).unlink()
                else:pathlib.Path(col).write_bytes(b'corrupt')
                self.assertEqual(model.build(str(root),str(source),'unique',out,col),one)
                self.assertEqual(pathlib.Path(col).read_bytes(),expected)


if __name__=='__main__': unittest.main()
