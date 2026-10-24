"""Several-material rigid models (multi_material.py, actor_model.read_parts): original synthetic fixtures only.

The template here is made up: a BFRES 3.4.0.1 shaped like the stand-in item's (one rigid model, one
vertex buffer _p0/_n0/_u0 stride 48, one shape, one material with a colour texture and a second texture,
render info, render state, shader assignment, samplers and parameters). No game data."""
import copy
import importlib.util
import json
import os
import pathlib
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import actor_model as model  # noqa: E402
import collision_mesh  # noqa: E402
import multi_material  # noqa: E402
import test_notebook_model as fixture  # noqa: E402
from skinned_actor_model import append_dictionary, append_string  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
base = model.base
OPAQUE_STATE = (1, 0x00280242, 0x49749736, 3, 0x3F800000, 0x00CC0000, 0, 0x20010001, 0, 0, 0, 0)
MASK_STATE = (2, 0x00280242, 0x49749736, 0xE, 0x3F008084, 0x00CC0000, 0, 0x20010001, 0, 0, 0, 0)
NAMES = dict(opaque='Original_opaque', mask='Original_mask')


def rich_template(opaque='Original_opaque', mask='Original_mask'):
    f = base.Bfres(b'FRES' + struct.pack('>I', 0x03040001) + bytes(0x68))
    put, rel = f.put, f.set_rel
    def block(size, align=4):
        return f.append(bytes(size), align)
    # textures: the colour texture and a second one (a toon ramp in the real template)
    textures = []
    for name, (w, h) in ((base.TEXTURE, (64, 64)), ('Ramp', (32, 8))):
        t = block(0xC0)
        f.b[t:t + 4] = b'FTEX'
        put('IIIIII', t + 4, 1, w, h, 1, 1, 0x1A)
        data = f.append(bytes(w * h * 4), 0x100)
        put('I', t + 4 + 0x20, w * h * 4)
        rel(t + 0xB0, data)
        rel(t + 0xA8, append_string(f, name))
        textures.append((name, t))
    rel(0x24, append_dictionary(f, textures))
    put('H', 0x52, 2)
    fm = block(0x30)
    f.b[fm:fm + 4] = b'FMDL'
    # skeleton: one root bone with user data (the converter drops it)
    sk = block(0x24); f.b[sk:sk + 4] = b'FSKL'; put('HHH', sk + 8, 1, 0, 0)
    bone = block(0x40); put('HHHHHH', bone + 4, 0, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 1); put('I', bone + 0x10, 0x03001001)
    put('3f', bone + 0x14, 2, 3, 4); put('4f', bone + 0x20, .2, .3, .4, 1); put('3f', bone + 0x30, 10, 20, 30)
    rel(bone, append_string(f, 'Original_root')); rel(bone + 0x3C, block(16))
    rel(sk + 0x10, append_dictionary(f, [('Original_root', bone)])); rel(sk + 0x14, bone)
    rel(fm + 0xC, sk)
    # vertex buffer
    fv = block(0x20); f.b[fv:fv + 4] = b'FVTX'; f.b[fv + 4], f.b[fv + 5] = 3, 1; put('I', fv + 8, 3)
    attrs = block(36); entries = []
    for i, (name, off, fmt) in enumerate((('_p0', 0, 0x811), ('_n0', 16, 0x811), ('_u0', 32, 0x80D))):
        rel(attrs + 12 * i, append_string(f, name)); put('HI', attrs + 12 * i + 6, off, fmt); entries.append((name, attrs + 12 * i))
    rel(fv + 0x10, attrs); rel(fv + 0x14, append_dictionary(f, entries))
    vb = block(24); put('IIIHH', vb, 0, 144, 0, 48, 1); rel(vb + 0x14, f.append(bytes(144), 0x100)); rel(fv + 0x18, vb)
    rel(fm + 0x10, fv)
    # shape and mesh
    sh = block(0x40); f.b[sh:sh + 4] = b'FSHP'; f.b[sh + 0x17] = 1; put('H', sh + 0x1A, 1); put('f', sh + 0x1C, 10.0)
    rel(sh + 4, append_string(f, 'Polygon0')); rel(sh + 0x20, fv)
    mesh = block(28); put('III', mesh, 4, 9, 3); put('H', mesh + 0xC, 1); rel(mesh + 0x10, f.append(struct.pack('>II', 0, 3), 4))
    ib = block(24); put('IIIHH', ib, 0, 12, 0, 0, 1); rel(ib + 0x14, f.append(bytes(12), 0x100)); rel(mesh + 0x14, ib)
    rel(sh + 0x24, mesh); rel(sh + 0x34, f.append(struct.pack('>6f', 0, 0, 0, 1, 1, 1), 4))
    rel(fm + 0x14, append_dictionary(f, [('Polygon0', sh)]))
    # materials: an opaque one and an alpha-tested cut-out one (same textures, shader archive and parameters)
    def material(name, index, state_words, program):
        mat = block(0x4C); f.b[mat:mat + 4] = b'FMAT'
        rel(mat + 4, append_string(f, name)); put('H', mat + 0xC, index)
        f.b[mat + 0x10], f.b[mat + 0x11] = 2, 2
        params = 3
        put('HH', mat + 0x12, params, 1); put('H', mat + 0x16, 64)
        infos = []
        for info, value in (('draw_buffer_type', 1), ('sort_priority', 10 * index)):
            at = block(12); put('HBB', at, 1, 0, 0); put('i', at + 8, value); rel(at + 4, append_string(f, info)); infos.append((info, at))
        rel(mat + 0x1C, append_dictionary(f, infos))
        state = block(48); struct.pack_into('>12I', f.b, state, *state_words); rel(mat + 0x20, state)
        assign = block(28); rel(assign, append_string(f, 'original')); rel(assign + 4, append_string(f, program))
        for offset, names in ((0x10, ('_p0', '_n0', '_u0')), (0x14, ('_a0', '_a1'))):
            rel(assign + offset, append_dictionary(f, [(n, append_string(f, n)) for n in names]))
        rel(mat + 0x24, assign)
        refs = block(16)
        for i, (tname, t) in enumerate(textures):
            rel(refs + 8 * i, append_string(f, tname)); rel(refs + 8 * i + 4, t)
        rel(mat + 0x28, refs)
        samplers = block(48); entries = []
        for i, sname in enumerate(('_a0', '_a1')):
            put('I', samplers + 24 * i, 0x02021200 if i == 0 else 0x02001212); rel(samplers + 24 * i + 0xC, append_string(f, sname)); entries.append((sname, samplers + 24 * i))
        rel(mat + 0x2C, samplers); rel(mat + 0x30, append_dictionary(f, entries))
        pa = block(20 * params); entries = []
        for i, pname in enumerate(('mat_color0', 'amb_color0', 'konst0')):
            put('HHI', pa + 20 * i, 0, i, 16 * i); rel(pa + 20 * i + 16, append_string(f, pname)); entries.append((pname, pa + 20 * i))
        rel(mat + 0x34, pa); rel(mat + 0x38, append_dictionary(f, entries))
        rel(mat + 0x3C, f.append(bytes(range(64)), 4)); rel(mat + 0x44, f.append(b'\0', 4))
        return mat
    cut = material(mask, 0, MASK_STATE, 'Original_cutout_program')
    mat = material(opaque, 1, OPAQUE_STATE, 'Original_program')
    put('H', sh + 0xE, 1)  # the shape template uses the opaque material
    rel(fm + 0x18, append_dictionary(f, [(mask, cut), (opaque, mat)]))
    put('HHHH', fm + 0x20, 1, 1, 2, 0); put('I', fm + 0x28, 3)
    rel(fm + 4, append_string(f, 'original'))
    rel(0x20, append_dictionary(f, [('original', fm)])); put('H', 0x50, 1)
    f.append(b'', 0x1000); put('I', 0xC, len(f.b))
    return bytes(f.b)


def pack(doc, binary):
    return collision_mesh._pack(doc, bytes(binary))


def glb(materials, primitives, images=None, extra=None):
    """materials: list of glTF material dicts (baseColorTexture filled in by order); primitives: list of
    (material index, triangle count) - each primitive a fan of distinct triangles."""
    binary, views, accessors = bytearray(), [], []
    def part(body):
        binary.extend(b'\0' * (-len(binary) % 4)); views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(body)}); binary.extend(body)
        return len(views) - 1
    def acc(fmt, rows, kind, component=5126):
        flat = [x for row in rows for x in (row if isinstance(row, tuple) else (row,))]
        accessors.append({'bufferView': part(struct.pack('<%d%s' % (len(flat), fmt), *flat)), 'componentType': component, 'count': len(rows), 'type': kind})
        return len(accessors) - 1
    prims = []
    for k, (material, count) in enumerate(primitives):
        pos, nrm, uv = [], [], []
        for t in range(count):
            x = 0.5 * t + k
            pos += [(x, 0.0, 0.0), (x + 0.4, 0.0, 0.0), (x, 0.4 + 0.1 * k, 0.0)]
            nrm += [(0.0, 0.0, 1.0)] * 3
            uv += [(0.0, 0.0), (2.0, 0.0), (0.0, 2.0)]
        prims.append({'attributes': {'POSITION': acc('f', pos, 'VEC3'), 'NORMAL': acc('f', nrm, 'VEC3'), 'TEXCOORD_0': acc('f', uv, 'VEC2')},
                      'indices': acc('H', list(range(3 * count)), 'SCALAR', 5123), 'material': material})
    images = images or [fixture.png(64, 8, bytes([(40 * i) & 255, 200, (255 - 40 * i) & 255, 255 if i % 2 == 0 else 0] * 512)) for i in range(len(materials))]
    image_refs = [{'bufferView': part(body), 'mimeType': 'image/png'} for body in images]
    out_materials = []
    for i, m in enumerate(materials):
        m = copy.deepcopy(m)
        m.setdefault('pbrMetallicRoughness', {}).setdefault('baseColorTexture', {'index': min(i, len(images) - 1)})
        out_materials.append(m)
    doc = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': [{'mesh': 0}],
           'meshes': [{'primitives': prims}], 'buffers': [{'byteLength': 0}], 'bufferViews': views, 'accessors': accessors,
           'materials': out_materials, 'textures': [{'source': i} for i in range(len(images))], 'images': image_refs}
    if extra:
        extra(doc)
    binary.extend(b'\0' * (-len(binary) % 4))
    doc['buffers'][0]['byteLength'] = len(binary)
    return pack(doc, binary)


THREE = [{'name': 'sand'}, {'name': 'leaves', 'alphaMode': 'MASK', 'alphaCutoff': 0.25, 'doubleSided': True}, {'name': 'wood', 'doubleSided': True}]


class MultiMaterialTests(unittest.TestCase):
    def parse(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, 'model.glb'); pathlib.Path(path).write_bytes(data)
            return model.read_parts(path)

    def static(self, data, collision=True):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, 'model.glb'); pathlib.Path(path).write_bytes(data)
            return collision_mesh.read_static(path, collision)

    def test_parts_materials_and_info(self):
        parts, info = self.parse(glb(THREE, [(0, 2), (1, 3), (2, 1), (0, 1)]))
        self.assertEqual([p.material for p in parts.parts], [0, 1, 2, 0])
        self.assertEqual([(m.kind, m.double_sided) for m in parts.materials], [('opaque', False), ('mask', True), ('opaque', True)])
        self.assertAlmostEqual(parts.materials[1].cutoff, 0.25)
        self.assertEqual(info, {'vertices': 21, 'triangles': 7, 'drawn_triangles': 11, 'texture_width': 64, 'texture_height': 8,
                                'materials': 3, 'textures': 3, 'texture_bytes': 3 * 64 * 8 * 4, 'cutout_materials': 1})
        self.assertAlmostEqual(parts.parts[1].pos[2][1], 50, places=4)  # metres -> game units

    def test_bfres_structure(self):
        parts, _ = self.parse(glb(THREE, [(0, 2), (1, 3), (2, 1), (0, 1)]))
        out = multi_material.build_bfres(rich_template(), parts, 'mod_multi', **NAMES)
        f = base.Bfres(out)
        self.assertEqual(f.u32(0xC), len(out)); self.assertEqual(len(out) % 0x1000, 0)
        (name, fm), = f.dict(f.rel(0x20))
        self.assertEqual(name, 'mod_multi'); self.assertEqual(f.cstr(f.rel(fm + 4)), 'mod_multi')
        self.assertEqual(struct.unpack_from('>HHH', out, fm + 0x20), (4, 4, 3))
        textures = dict(f.dict(f.rel(0x24)))
        self.assertEqual(sorted(textures), ['Ramp', 'mm_colour00', 'mm_colour01', 'mm_colour02'])
        self.assertNotIn(base.TEXTURE, textures)  # the template's own colour texture is not carried along
        materials = f.dict(f.rel(fm + 0x18))
        self.assertEqual([n for n, _ in materials], ['mm_material00', 'mm_material01', 'mm_material02'])
        for i, (_, at) in enumerate(materials):
            refs = f.rel(at + 0x28)
            self.assertEqual(f.cstr(f.rel(refs)), 'mm_colour%02d' % i)
            self.assertEqual(f.rel(refs + 4), textures['mm_colour%02d' % i])
            self.assertEqual(f.rel(refs + 12), textures['Ramp'])
            self.assertEqual(f.u16(at + 0xC), i)
            surface = textures['mm_colour%02d' % i] + 4
            self.assertEqual((f.u32(surface + 4), f.u32(surface + 8), f.u32(surface + 0x14), f.u32(surface + 0x30)), (64, 8, base.FMT_RGBA8_SRGB, 1))
            # the shader assignment, samplers and parameters are the template's
            assign = f.rel(at + 0x24)
            self.assertEqual([n for n, _ in f.dict(f.rel(at + 0x38))], ['mat_color0', 'amb_color0', 'konst0'])
            self.assertEqual(bytes(f.b[f.rel(at + 0x3C):f.rel(at + 0x3C) + 64]), bytes(range(64)))
        states = [struct.unpack_from('>12I', out, f.rel(at + 0x20)) for _, at in materials]
        self.assertEqual(states[0], OPAQUE_STATE)
        self.assertEqual(states[1][:5], (2, 0x00280242, 0x49749736, 0xE, struct.unpack('>I', struct.pack('>f', 0.25))[0]))
        self.assertEqual(states[1][5:], MASK_STATE[5:])  # culling stays on: double-sided faces get a back copy
        programs = [f.cstr(f.rel(f.rel(at + 0x24) + 4)) for _, at in materials]
        self.assertEqual(programs, ['Original_program', 'Original_cutout_program', 'Original_program'])
        self.assertEqual(states[2], OPAQUE_STATE)
        shapes = f.dict(f.rel(fm + 0x14))
        self.assertEqual([f.u16(at + 0xE) for _, at in shapes], [0, 1, 2, 0])
        drawn = multi_material.drawn_parts(parts)
        self.assertEqual([len(p.indices) // 3 for p in drawn], [2, 6, 2, 1])
        back = drawn[1]; n = len(parts.parts[1].pos)
        self.assertEqual(back.indices[9:12], [parts.parts[1].indices[0] + n, parts.parts[1].indices[2] + n, parts.parts[1].indices[1] + n])
        self.assertEqual(back.nrm[n], tuple(-v for v in parts.parts[1].nrm[0]))
        for (_, shape), part in zip(shapes, drawn):
            fv = f.rel(shape + 0x20); vb = f.rel(fv + 0x18); data = f.rel(vb + 0x14)
            self.assertEqual(f.u32(fv + 8), len(part.pos))
            for i, p in enumerate(part.pos):
                for a, b in zip(struct.unpack_from('>3f', out, data + 48 * i), p): self.assertAlmostEqual(a, b, places=3)
            mesh = f.rel(shape + 0x24); ib = f.rel(f.rel(mesh + 0x14) + 0x14)
            self.assertEqual(list(struct.unpack_from('>%dI' % len(part.indices), out, ib)), part.indices)
            box = struct.unpack_from('>6f', out, f.rel(shape + 0x34))
            for k in range(3):
                self.assertAlmostEqual(box[k] - box[k + 3], min(p[k] for p in part.pos), places=3)
        skeleton = f.rel(fm + 0xC); bone = f.rel(skeleton + 0x14)
        self.assertEqual(struct.unpack_from('>3f4f3f', out, bone + 0x14), (1, 1, 1, 0, 0, 0, 1, 0, 0, 0))
        self.assertEqual((f.u16(bone + 0xE), f.s32(bone + 0x3C)), (0, 0))

    def test_shared_image_one_texture(self):
        two = [{'name': 'a'}, {'name': 'b', 'alphaMode': 'MASK'}]
        data = glb(two, [(0, 1), (1, 1)], images=[fixture.png(64, 8, bytes(64 * 8 * 4))],
                   extra=lambda d: d['materials'][1]['pbrMetallicRoughness'].update(baseColorTexture={'index': 0}))
        parts, info = self.parse(data)
        self.assertEqual(([m.image for m in parts.materials], info['textures']), ([0, 0], 1))
        out = multi_material.build_bfres(rich_template(), parts, 'shared', **NAMES)
        f = base.Bfres(out)
        self.assertEqual(sorted(n for n, _ in f.dict(f.rel(0x24))), ['Ramp', 'mm_colour00'])

    def test_refusals_with_clear_messages(self):
        cases = [
            (glb([{'name': 'glass', 'alphaMode': 'BLEND'}], [(0, 1)]), 'blended transparency'),
            (glb([{'name': 'm%d' % i} for i in range(17)], [(i, 1) for i in range(17)]), 'at most 16'),
            (glb([{}], [(0, 1)] * 33), '1 to 32'),
            (glb([{}, {}], [(0, 1)]), 'every material must be used'),
            (glb([{'alphaCutoff': 0.5}], [(0, 1)]), 'alphaCutoff needs alphaMode MASK'),
            (glb([{'alphaMode': 'MASK', 'alphaCutoff': 1.5}], [(0, 1)]), 'between 0 and 1'),
            (glb([{'doubleSided': 1}], [(0, 1)]), 'doubleSided'),
            (glb([{'name': 'tinted', 'pbrMetallicRoughness': {'baseColorFactor': [1, 0, 0, 1]}}], [(0, 1)]), 'base colour factor'),
            (glb([{}] * 5, [(i, 1) for i in range(5)], images=[fixture.png(512, 128, bytes(512 * 128 * 4))] * 5), '1 MiB'),
            (glb([{}], [(0, 1)], extra=lambda d: d['meshes'][0]['primitives'][0].update(material=3)), "model's materials"),
        ]
        for data, words in cases:
            with self.subTest(words=words):
                with self.assertRaises(model.BuildError) as raised:
                    self.parse(data)
                self.assertIn(words, str(raised.exception))
        # the per-model texture budget: exactly 1 MiB passes
        self.parse(glb([{}] * 4, [(i, 1) for i in range(4)], images=[fixture.png(256, 256, bytes(256 * 256 * 4))] * 4))
        # vertex and triangle limits count every primitive together
        with self.assertRaises(model.BuildError) as raised:
            self.parse(glb([{}, {}], [(0, 700), (1, 700)]))
        self.assertIn('4096 vertices', str(raised.exception))
        # double-sided faces count twice
        self.parse(glb([{}], [(0, 700)]))
        with self.assertRaises(model.BuildError) as raised:
            self.parse(glb([{'doubleSided': True}], [(0, 700)]))
        self.assertIn('double-sided faces count twice', str(raised.exception))

    def test_single_plain_material_keeps_the_original_conversion(self):
        source = ROOT / 'examples/guest-mods/world-objects-w1/assets/island.glb'
        geometry, info, _ = collision_mesh.read_static(source, True)
        self.assertIsInstance(geometry, tuple)
        self.assertEqual(set(info) - {k for k in info if k.startswith('collision_')}, {'vertices', 'triangles', 'texture_width', 'texture_height'})
        # one cut-out material alone uses the several-material writer
        geometry, info, _ = self.static(glb([{'alphaMode': 'MASK'}], [(0, 2)]), False)
        self.assertNotIsInstance(geometry, tuple)
        self.assertEqual(info['cutout_materials'], 1)

    def test_collision_from_visible_materials(self):
        names = [{'name': 'col_sand'}, {'name': 'grass'}, {'name': 'col_wood', 'alphaMode': 'MASK'}]
        geometry, info, data = self.static(glb(names, [(0, 2), (1, 1), (2, 1)]))
        v, t = info['collision_vertices'], info['collision_triangles']
        self.assertEqual(t, 4)
        semantics = [struct.unpack_from('>4H', data, 40 + 12 * v + 8 * i)[3] for i in range(t)]
        self.assertEqual(semantics, [3, 3, 0, 2])
        with self.assertRaises(model.BuildError):
            self.static(glb([{'name': 'col_magma'}, {}], [(0, 1), (1, 1)]))
        # lava is a surface: its triangle carries the lava surface (14)
        geometry, info, data = self.static(glb([{'name': 'col_lava'}, {}], [(0, 1), (1, 1)]))
        v, t = info['collision_vertices'], info['collision_triangles']
        self.assertEqual([struct.unpack_from('>4H', data, 40 + 12 * v + 8 * i)[3] for i in range(t)], [14, 0])

    def test_collision_mesh_alongside_several_materials(self):
        def add_collision(doc):
            doc['meshes'].append({'name': 'thing_col', 'primitives': [{'attributes': {'POSITION': 0}, 'indices': 3, 'material': len(doc['materials'])}]})
            doc['materials'].append({'name': 'col_grass'})
            doc['nodes'].append({'mesh': 1}); doc['scenes'][0]['nodes'].append(1)
        geometry, info, data = self.static(glb(THREE, [(0, 1), (1, 1), (2, 1)], extra=add_collision))
        self.assertEqual((len(geometry.parts), len(geometry.materials), info['materials']), (3, 3, 3))
        self.assertEqual(info['collision_triangles'], 1)

    def test_archive_build_and_cache(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as directory:
            game = pathlib.Path(directory); archive = game / multi_material.ARCHIVE; archive.parent.mkdir(parents=True)
            template = rich_template(multi_material.OPAQUE, multi_material.MASK)
            archive.write_bytes(base.yaz0_stored(base.sarc({multi_material.BFRES: template, multi_material.SHADERS: b'synthetic shader'})))
            source = game / 'source.glb'; source.write_bytes(glb(THREE, [(0, 1), (1, 1), (2, 1)]))
            out = str(game / 'cache' / 'multi.szs')
            one = model.build(str(game), str(source), 'multi', out)
            self.assertEqual((one['materials'], one['cutout_materials']), (3, 1))
            files = dict(base.shaderprep.sarc(base.shaderprep.yaz0(pathlib.Path(out).read_bytes())))
            self.assertEqual(set(files), {'multi.bfres', 'multi.sharcfb'}); self.assertEqual(files['multi.sharcfb'], b'synthetic shader')
            with patch.object(multi_material, 'build_bfres', side_effect=AssertionError('valid cache was rebuilt')):
                self.assertEqual(model.build(str(game), str(source), 'multi', out), one)

    def test_output_validator_rejects_corruption(self):
        parts, _ = self.parse(glb(THREE, [(0, 1), (1, 1), (2, 1)]))
        out = bytearray(multi_material.build_bfres(rich_template(), parts, 'checked', **NAMES))
        f = base.Bfres(out); fm = f.dict(f.rel(0x20))[0][1]
        shape = f.dict(f.rel(fm + 0x14))[1][1]
        struct.pack_into('>H', out, shape + 0xE, 2)
        with self.assertRaises(model.BuildError):
            multi_material.validate_bfres(bytes(out), 'checked', parts)

    def test_template_shape_checked(self):
        parts, _ = self.parse(glb(THREE, [(0, 1), (1, 1), (2, 1)]))
        with self.assertRaises(model.BuildError):
            multi_material.build_bfres(fixture.template(), parts, 'plain', **NAMES)  # no material: not a usable template

    def test_example_island_reproducible(self):
        path = ROOT / 'examples/guest-mods/world-objects-w5/assets/make_fixtures.py'
        spec = importlib.util.spec_from_file_location('multi_island', path)
        generator = importlib.util.module_from_spec(spec); spec.loader.exec_module(generator)
        self.assertEqual(generator.make(), path.with_name('island.glb').read_bytes())
        geometry, info, data = collision_mesh.read_static(path.with_name('island.glb'), True)
        self.assertEqual((info['materials'], info['cutout_materials'], info['textures']), (6, 2, 6))
        self.assertEqual([m.name for m in geometry.materials], ['sand', 'grass', 'rock', 'wood', 'fence', 'bush'])
        v, t = info['collision_vertices'], info['collision_triangles']
        semantics = {struct.unpack_from('>4H', data, 40 + 12 * v + 8 * i)[3] for i in range(t)}
        self.assertEqual(semantics, {0, 1, 2, 3})
        out = multi_material.build_bfres(rich_template(), geometry, 'island', **NAMES)
        f = base.Bfres(out); fm = f.dict(f.rel(0x20))[0][1]
        self.assertEqual(struct.unpack_from('>HHH', out, fm + 0x20), (6, 6, 6))


if __name__ == '__main__':
    unittest.main()
