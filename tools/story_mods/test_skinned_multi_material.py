# SPDX-License-Identifier: MPL-2.0
"""Several materials for skinned mod models (skinned_actor_model.build_bfres_parts): original synthetic fixtures only.

The template here is made up: a BFRES 3.4.0.1 shaped like the skinned cut-out stand-in's (one model
`fobj01`, one skinned vertex buffer _p0/_n0/_u0/_i0/_w0 stride 64, one shape, one alpha-tested material
with the colour texture `Fobj01` and a second texture, render info, render state, shader assignment,
samplers and parameters).
No game data."""
import hashlib
import importlib.util
import json
import math
import os
import pathlib
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import skinned_actor_model as skin  # noqa: E402
import test_notebook_model as fixture  # noqa: E402
from skinned_actor_model import append_dictionary, append_string  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
base = skin.base
SKIN_STATE = (1, 0x00280240, 0x49749736, 3, 0x3F800000, 0x00CC0000, 0, 0x20010001, 0, 0, 0, 0)    # an opaque output material
CUTOUT_STATE = (2, 0x00280240, 0x49749736, 0xE, 0x3F008084, 0x00CC0000, 0, 0x20010001, 0, 0, 0, 0)  # the template's cut-out
LAYOUT = [('_p0', 0, 0x811), ('_n0', 16, 0x811), ('_u0', 32, 0x80D), ('_i0', 40, 0x10C), ('_w0', 48, 0x80D)]


def skin_template(state=CUTOUT_STATE):
    f = base.Bfres(b'FRES' + struct.pack('>I', 0x03040001) + bytes(0x68))
    put, rel = f.put, f.set_rel
    def block(size, align=4):
        return f.append(bytes(size), align)
    textures = []
    for name, (w, h) in (('Fobj01', (64, 64)), ('Ramp', (32, 8))):
        t = block(0xC0); f.b[t:t + 4] = b'FTEX'
        put('IIIIII', t + 4, 1, w, h, 1, 1, 0x1A)
        put('I', t + 4 + 0x20, w * h * 4); rel(t + 0xB0, f.append(bytes(w * h * 4), 0x100))
        rel(t + 0xA8, append_string(f, name))
        textures.append((name, t))
    rel(0x24, append_dictionary(f, textures)); put('H', 0x52, 2)
    fm = block(0x30); f.b[fm:fm + 4] = b'FMDL'
    sk = block(0x24); f.b[sk:sk + 4] = b'FSKL'; put('IHHH', sk + 4, 0x1200, 1, 1, 0)
    bone = block(0x40); put('HHHHHH', bone + 4, 0, 0xFFFF, 0, 0xFFFF, 0xFFFF, 0); put('3f', bone + 0x14, 1, 1, 1); put('f', bone + 0x2C, 1)
    rel(bone, append_string(f, 'Original_root')); rel(sk + 0x10, append_dictionary(f, [('Original_root', bone)])); rel(sk + 0x14, bone)
    rel(sk + 0x18, f.append(struct.pack('>H', 0), 4)); rel(sk + 0x1C, f.append(bytes(48), 4))
    rel(fm + 0xC, sk)
    fv = block(0x20); f.b[fv:fv + 4] = b'FVTX'; f.b[fv + 4], f.b[fv + 5] = len(LAYOUT), 1; put('I', fv + 8, 3); f.b[fv + 12] = 2
    attrs = block(12 * len(LAYOUT)); entries = []
    for i, (name, off, fmt) in enumerate(LAYOUT):
        rel(attrs + 12 * i, append_string(f, name)); put('HI', attrs + 12 * i + 6, off, fmt); entries.append((name, attrs + 12 * i))
    rel(fv + 0x10, attrs); rel(fv + 0x14, append_dictionary(f, entries))
    vb = block(24); put('IIIHH', vb, 0, 192, 0, 64, 1); rel(vb + 0x14, f.append(bytes(192), 0x100)); rel(fv + 0x18, vb)
    rel(fm + 0x10, fv)
    sh = block(0x40); f.b[sh:sh + 4] = b'FSHP'; put('H', sh + 0x14, 1); f.b[sh + 0x16] = 2; f.b[sh + 0x17] = 1; put('H', sh + 0x1A, 1); put('f', sh + 0x1C, 10.0)
    rel(sh + 4, append_string(f, 'Original_shape')); rel(sh + 0x20, fv)
    mesh = block(28); put('III', mesh, 4, 9, 3); put('H', mesh + 0xC, 1); rel(mesh + 0x10, f.append(struct.pack('>II', 0, 3), 4))
    ib = block(24); put('IIIHH', ib, 0, 12, 0, 0, 1); rel(ib + 0x14, f.append(bytes(12), 0x100)); rel(mesh + 0x14, ib)
    rel(sh + 0x24, mesh); rel(sh + 0x28, f.append(struct.pack('>H', 0), 4))
    rel(sh + 0x30, f.append(struct.pack('>6H', 0, 0, 0, 0, 0, 1), 4)); rel(sh + 0x34, f.append(struct.pack('>6f', 0, 0, 0, 1, 1, 1), 4))
    rel(sh + 0x38, f.append(b'\0\0', 4))
    rel(fm + 0x14, append_dictionary(f, [('Original_shape', sh)]))
    mat = block(0x4C); f.b[mat:mat + 4] = b'FMAT'
    rel(mat + 4, append_string(f, 'Original_material')); f.b[mat + 0x10], f.b[mat + 0x11] = 2, 2
    put('HH', mat + 0x12, 2, 1); put('H', mat + 0x16, 32)
    infos = []
    for info, value in (('draw_buffer_type', 1), ('sort_priority', 0)):
        at = block(12); put('HBB', at, 1, 0, 0); put('i', at + 8, value); rel(at + 4, append_string(f, info)); infos.append((info, at))
    rel(mat + 0x1C, append_dictionary(f, infos))
    words = block(48); struct.pack_into('>12I', f.b, words, *state); rel(mat + 0x20, words)
    assign = block(28); rel(assign, append_string(f, 'original')); rel(assign + 4, append_string(f, 'Original_program'))
    for offset, names in ((0x10, [n for n, _, _ in LAYOUT]), (0x14, ('_a0', '_a1'))):
        rel(assign + offset, append_dictionary(f, [(n, append_string(f, n)) for n in names]))
    rel(mat + 0x24, assign)
    refs = block(16)
    for i, (tname, t) in enumerate(textures):
        rel(refs + 8 * i, append_string(f, tname)); rel(refs + 8 * i + 4, t)
    rel(mat + 0x28, refs)
    samplers = block(48); entries = []
    for i, sname in enumerate(('_a0', '_a1')):
        put('I', samplers + 24 * i, 0x02021200); rel(samplers + 24 * i + 0xC, append_string(f, sname)); entries.append((sname, samplers + 24 * i))
    rel(mat + 0x2C, samplers); rel(mat + 0x30, append_dictionary(f, entries))
    pa = block(40); entries = []
    for i, pname in enumerate(('mat_color0', 'konst0')):
        put('HHI', pa + 20 * i, 0, i, 16 * i); rel(pa + 20 * i + 16, append_string(f, pname)); entries.append((pname, pa + 20 * i))
    rel(mat + 0x34, pa); rel(mat + 0x38, append_dictionary(f, entries))
    rel(mat + 0x3C, f.append(bytes(range(32)), 4)); rel(mat + 0x44, f.append(b'\0', 4))
    rel(fm + 0x18, append_dictionary(f, [('Original_material', mat)]))
    put('HHHH', fm + 0x20, 1, 1, 1, 0); put('I', fm + 0x28, 3)
    rel(fm + 4, append_string(f, 'fobj01'))
    rel(0x20, append_dictionary(f, [('fobj01', fm)])); put('H', 0x50, 1)
    f.append(b'', 0x1000); put('I', 0xC, len(f.b))
    return bytes(f.b)


def strip(y0, y1, z, influence):
    """two triangles: a 0.4 m wide strip from y0 to y1 at depth z, facing +Z"""
    pos = [(-.2, y0, z), (.2, y0, z), (.2, y1, z), (-.2, y1, z)]
    return pos, [(0., 0., 1.)] * 4, [(0., 1.), (1., 1.), (1., 0.), (0., 0.)], [influence(p[1]) for p in pos], [0, 1, 2, 0, 2, 3]


def seam_weights(y):
    """root below 0.25 m, tip above 0.75 m, blended between: half and half at the 0.5 m seam"""
    t = min(1., max(0., (y - .25) / .5))
    return ((0, 1), (1 - t, t))


def skinned_glb(materials, primitives, images=None, clips=False, extra=None):
    """materials: glTF material dicts (base colour textures filled in by order); primitives: (material,
    (positions, normals, uvs, influences, indices)) - every primitive skinned to the two-bone rig root/tip"""
    binary, views, accessors = bytearray(), [], []
    def view(body):
        binary.extend(b'\0' * (-len(binary) % 4)); views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(body)}); binary.extend(body)
        return len(views) - 1
    def acc(rows, kind, component=5126):
        flat = [v for row in rows for v in (row if isinstance(row, (tuple, list)) else (row,))]
        code = {5126: 'f', 5123: 'H', 5121: 'B'}[component]
        accessors.append({'bufferView': view(struct.pack('<%d%s' % (len(flat), code), *flat)), 'componentType': component, 'count': len(rows), 'type': kind})
        return len(accessors) - 1
    prims = []
    for material, (pos, nrm, uv, influences, indices) in primitives:
        joints = [(j[0], j[1], 0, 0) for j, _ in influences]
        weights = [(w[0], w[1], 0., 0.) for _, w in influences]
        prims.append({'attributes': {'POSITION': acc(pos, 'VEC3'), 'NORMAL': acc(nrm, 'VEC3'), 'TEXCOORD_0': acc(uv, 'VEC2'),
                                     'JOINTS_0': acc(joints, 'VEC4', 5121), 'WEIGHTS_0': acc(weights, 'VEC4')},
                      'indices': acc(indices, 'SCALAR', 5123), 'material': material})
    images = images or [fixture.png(64, 8, bytes([(60 * i) & 255, 200, (255 - 60 * i) & 255, 255 if i % 2 == 0 else 0] * 512)) for i in range(len(materials))]
    image_refs = [{'bufferView': view(body), 'mimeType': 'image/png'} for body in images]
    binds = acc([(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1), (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -.5, 0, 1)], 'MAT4')
    out_materials = []
    for i, m in enumerate(materials):
        m = json.loads(json.dumps(m))
        m.setdefault('pbrMetallicRoughness', {}).setdefault('baseColorTexture', {'index': min(i, len(images) - 1)})
        out_materials.append(m)
    doc = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0, 1]}],
           'nodes': [{'mesh': 0, 'skin': 0}, {'name': 'root', 'children': [2]}, {'name': 'tip', 'translation': [0, .5, 0]}],
           'skins': [{'joints': [1, 2], 'skeleton': 1, 'inverseBindMatrices': binds}],
           'meshes': [{'primitives': prims}], 'materials': out_materials,
           'textures': [{'source': i} for i in range(len(images))], 'images': image_refs,
           'buffers': [{'byteLength': 0}], 'bufferViews': views, 'accessors': accessors}
    if clips:
        times = acc([0., .5, 1.], 'SCALAR')
        values = acc([(0., 0., 0., 1.), (0., 0., math.sin(.4), math.cos(.4)), (0., 0., 0., 1.)], 'VEC4')
        doc['animations'] = [{'name': 'bend', 'samplers': [{'input': times, 'output': values}], 'channels': [{'sampler': 0, 'target': {'node': 2, 'path': 'rotation'}}]}]
    if extra:
        extra(doc)
    binary.extend(b'\0' * (-len(binary) % 4))
    doc['buffers'][0]['byteLength'] = len(binary)
    return skin.pack_glb(doc, bytes(binary))


LOWER = strip(0., .5, 0., seam_weights)
UPPER = strip(.5, 1., 0., seam_weights)
LEAF = strip(.9, 1.2, .05, lambda y: ((1, 1), (1., 0.)))
THREE = [{'name': 'lower'}, {'name': 'upper'}, {'name': 'leaf', 'alphaMode': 'MASK', 'alphaCutoff': 0.25, 'doubleSided': True}]


def parse(data):
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, 'model.glb')
        pathlib.Path(path).write_bytes(data)
        return skin.read_glb(path)


def euler_matrix(rx, ry, rz, t):
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    r = [[cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz], [cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz], [-sy, sx * cy, cx * cy]]
    return [r[0] + [t[0]], r[1] + [t[1]], r[2] + [t[2]], [0, 0, 0, 1]]


def multiply(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def posed_shapes(data, pose):
    """CPU skinning of the converted model, read back from the BFRES only: each bone's rest rotation and
    translation (with `pose` (bone -> extra Euler XYZ) added), the inverse binds of the skeleton and every
    shape's vertex records (two skin matrix indices and weights)."""
    f = base.Bfres(data); fm = f.dict(f.rel(0x20))[0][1]; sk = f.rel(fm + 12)
    count = f.u16(sk + 8); bones = f.rel(sk + 20); inverse = f.rel(sk + 28); palette = f.rel(sk + 24)
    world, skinning = [], []
    for i in range(count):
        at = bones + 64 * i
        parent = struct.unpack_from('>h', data, at + 6)[0]
        rotation = struct.unpack_from('>3f', data, at + 32); translation = struct.unpack_from('>3f', data, at + 48)
        extra = pose.get(i, (0, 0, 0))
        local = euler_matrix(*(a + b for a, b in zip(rotation, extra)), translation)
        world.append(multiply(world[parent], local) if parent >= 0 else local)
    for k in range(count):
        bone = struct.unpack_from('>H', data, palette + 2 * k)[0]
        rows = struct.unpack_from('>12f', data, inverse + 48 * bone)
        skinning.append(multiply(world[bone], [list(rows[0:4]), list(rows[4:8]), list(rows[8:12]), [0, 0, 0, 1]]))
    shapes = []
    for _, shape in f.dict(f.rel(fm + 0x14)):
        fv = f.rel(shape + 0x20); vb = f.rel(fv + 0x18); at = f.rel(vb + 0x14); out = []
        for v in range(f.u32(fv + 8)):
            p = struct.unpack_from('>3f', data, at + 64 * v); j = struct.unpack_from('>2I', data, at + 64 * v + 40); w = struct.unpack_from('>2f', data, at + 64 * v + 48)
            out.append(tuple(sum(w[n] * (sum(skinning[j[n]][r][c] * p[c] for c in range(3)) + skinning[j[n]][r][3]) for n in range(2)) for r in range(3)))
        shapes.append(out)
    return shapes


class SkinnedMultiMaterialTests(unittest.TestCase):
    def test_read_parts_and_info(self):
        model, info = parse(skinned_glb(THREE, [(0, LOWER), (1, UPPER), (2, LEAF)]))
        self.assertIn('parts', model)
        self.assertEqual([p.material for p in model['parts'].parts], [0, 1, 2])
        self.assertEqual([(m.kind, m.double_sided) for m in model['parts'].materials], [('opaque', False), ('opaque', False), ('mask', True)])
        self.assertEqual((info['kind'], info['bones'], info['materials'], info['cutout_materials'], info['textures']), ('skinned', 2, 3, 1, 3))
        self.assertEqual((info['vertices'], info['triangles'], info['drawn_triangles']), (12, 6, 8))
        self.assertEqual(model['influences'][0][2], [(0, .5), (1, .5)])  # the seam: half root, half tip

    def test_one_plain_material_keeps_the_original_conversion(self):
        model, _ = parse(skinned_glb([{}], [(0, LOWER)]))
        self.assertNotIn('parts', model)
        self.assertIn('geometry', model)
        # one cut-out or double-sided material alone uses the several-material writer
        for material in ({'alphaMode': 'MASK'}, {'doubleSided': True}):
            model, _ = parse(skinned_glb([material], [(0, LOWER)]))
            self.assertIn('parts', model)

    def test_bfres_structure(self):
        model, info = parse(skinned_glb(THREE, [(0, LOWER), (1, UPPER), (2, LEAF), (0, LEAF)]))
        out = skin.build_bfres(skin_template(), model, 'skin_multi')
        f = base.Bfres(out)
        self.assertEqual(f.u32(0xC), len(out)); self.assertEqual(len(out) % 0x1000, 0)
        (name, fm), = f.dict(f.rel(0x20))
        self.assertEqual((name, f.cstr(f.rel(fm + 4))), ('skin_multi', 'skin_multi'))
        self.assertEqual(struct.unpack_from('>HHH', out, fm + 0x20), (4, 4, 3))
        textures = dict(f.dict(f.rel(0x24)))
        self.assertEqual(sorted(textures), ['Ramp', 'mm_colour00', 'mm_colour01', 'mm_colour02'])
        self.assertEqual(f.u16(0x52), 4)
        sk = f.rel(fm + 12); palette = f.rel(sk + 24)
        self.assertEqual(struct.unpack_from('>HH', out, sk + 8), (2, 2))
        materials = f.dict(f.rel(fm + 0x18))
        self.assertEqual([n for n, _ in materials], ['mm_material00', 'mm_material01', 'mm_material02'])
        states = []
        for i, (_, at) in enumerate(materials):
            refs = f.rel(at + 0x28)
            self.assertEqual((f.cstr(f.rel(refs)), f.rel(refs + 4)), ('mm_colour%02d' % i, textures['mm_colour%02d' % i]))
            self.assertEqual((f.cstr(f.rel(refs + 8)), f.rel(refs + 12)), ('Ramp', textures['Ramp']))
            self.assertEqual(f.cstr(f.rel(f.rel(at + 0x24) + 4)), 'Original_program')  # every material: the template's skinned program
            states.append(struct.unpack_from('>12I', out, f.rel(at + 0x20)))
        self.assertEqual(states[0], SKIN_STATE)
        self.assertEqual(states[1], SKIN_STATE)
        cutoff = struct.unpack('>I', struct.pack('>f', 0.25))[0]
        self.assertEqual(states[2][:5], (2, 0x00280242, 0x49749736, 0xE, cutoff))  # cut-out, double-sided: back faces culled
        self.assertEqual(states[2][5:], CUTOUT_STATE[5:])
        shapes = f.dict(f.rel(fm + 0x14))
        drawn = skin.drawn_parts(model)
        self.assertEqual([f.u16(at + 0xE) for _, at in shapes], [0, 1, 2, 0])
        self.assertEqual([len(p.indices) // 3 for p in drawn], [2, 2, 4, 2])
        for index, ((_, shape), part) in enumerate(zip(shapes, drawn)):
            self.assertEqual(struct.unpack_from('>HHH', out, shape + 0x10), (0, index, 2))  # bone 0, own vertex buffer, two palette entries
            self.assertEqual((out[shape + 0x16], f.rel(shape + 0x28)), (2, palette))   # two weights, the one skin palette
            self.assertAlmostEqual(struct.unpack_from('>f', out, shape + 0x1C)[0], info['animation_radius'], places=3)
            fv = f.rel(shape + 0x20); vb = f.rel(fv + 0x18); data = f.rel(vb + 0x14)
            self.assertEqual((f.u16(fv + 6), f.u32(fv + 8), out[fv + 12], f.u32(vb + 4)), (index, len(part.pos), 2, 64 * len(part.pos)))
            for v, (p, used) in enumerate(zip(part.pos, part.influences)):
                for a, b in zip(struct.unpack_from('>3f', out, data + 64 * v), p): self.assertAlmostEqual(a, b, places=3)
                self.assertEqual(struct.unpack_from('>2I', out, data + 64 * v + 40), tuple(j for j, _ in used))
                for a, (_, b) in zip(struct.unpack_from('>2f', out, data + 64 * v + 48), used): self.assertAlmostEqual(a, b, places=6)
            mesh = f.rel(shape + 0x24)
            self.assertEqual(list(struct.unpack_from('>%dI' % len(part.indices), out, f.rel(f.rel(mesh + 0x14) + 0x14))), part.indices)
        # the double-sided leaf: a mirrored back copy with the same skin influences
        leaf = drawn[2]; n = len(LEAF[0])
        self.assertEqual(leaf.influences[n:], leaf.influences[:n])
        self.assertEqual(leaf.nrm[n], (-0.0, -0.0, -1.0))
        self.assertEqual(leaf.indices[6:9], [n + 0, n + 2, n + 1])

    def test_material_border_deforms_together(self):
        model, _ = parse(skinned_glb(THREE, [(0, LOWER), (1, UPPER), (2, LEAF)], clips=True))
        out = skin.build_bfres(skin_template(), model, 'seam')
        rest = posed_shapes(out, {})
        for shape, part in zip(rest, skin.drawn_parts(model)):  # the rest pose reproduces the authored positions
            for a, b in zip(shape, part.pos):
                for x, y in zip(a, b): self.assertAlmostEqual(x, y, places=2)
        bent = posed_shapes(out, {1: (.3, .2, .7), 0: (0., .5, 0.)})
        lower, upper = bent[0], bent[1]
        # the seam ring (y 0.5 m) is in both primitives with the same influences: it stays closed
        for a, b in ((lower[2], upper[1]), (lower[3], upper[0])):
            for x, y in zip(a, b): self.assertAlmostEqual(x, y, places=3)
        self.assertGreater(max(abs(x - y) for x, y in zip(upper[2], rest[1][2])), 10)  # and the pose really moved the tip
        # every vertex stays within the converter's animation envelope
        f = base.Bfres(out); shape = f.dict(f.rel(f.dict(f.rel(0x20))[0][1] + 0x14))[0][1]
        radius = struct.unpack_from('>f', out, shape + 0x1C)[0]
        self.assertTrue(all(math.sqrt(sum(c * c for c in p)) <= radius for shape in bent for p in shape))

    def test_reproducible_bytes(self):
        data = skinned_glb(THREE, [(0, LOWER), (1, UPPER), (2, LEAF)])
        model, _ = parse(data)
        one = skin.build_bfres(skin_template(), model, 'repro')
        two = skin.build_bfres(skin_template(), parse(data)[0], 'repro')
        self.assertEqual(one, two)
        # no clips and identity bind rotations: only exact float arithmetic, the same bytes on every platform
        self.assertEqual(hashlib.sha256(one).hexdigest(), REPRODUCIBLE_SHA256)

    def test_refusals_with_clear_messages(self):
        def missing_weights(doc):
            doc['meshes'][0]['primitives'][1]['attributes'].pop('WEIGHTS_0')
        cases = [
            (skinned_glb([{'name': 'm%d' % i} for i in range(17)], [(i, LOWER) for i in range(17)]), 'at most 16'),
            (skinned_glb([{}] * 5, [(i, LOWER) for i in range(5)], images=[fixture.png(512, 128, bytes(512 * 128 * 4))] * 5), '1 MiB'),
            (skinned_glb([{}], [(0, LOWER)] * 33), '1 to 32'),
            (skinned_glb([{'name': 'glass', 'alphaMode': 'BLEND'}, {}], [(0, LOWER), (1, UPPER)]), 'blended transparency'),
            (skinned_glb([{}, {}], [(0, LOWER)]), 'every material must be used'),
            (skinned_glb([{}, {}], [(0, LOWER), (1, UPPER)], extra=missing_weights), 'primitive 1: POSITION, NORMAL, TEXCOORD_0, JOINTS_0 and WEIGHTS_0'),
            (skinned_glb([{}, {}], [(0, LOWER), (1, strip(.5, 1., 0., lambda y: ((0, 2), (.5, .5))))]), 'primitive 1: invalid skin weights/joints'),
        ]
        for data, words in cases:
            with self.subTest(words=words):
                with self.assertRaises(skin.BuildError) as raised:
                    parse(data)
                self.assertIn(words, str(raised.exception))
        # exactly 1 MiB of textures and 16 materials pass
        parse(skinned_glb([{}] * 4, [(i, LOWER) for i in range(4)], images=[fixture.png(256, 256, bytes(256 * 256 * 4))] * 4))
        parse(skinned_glb([{'name': 'm%d' % i} for i in range(16)], [(i, LOWER) for i in range(16)]))

    def test_template_must_be_a_cutout(self):
        model, _ = parse(skinned_glb(THREE, [(0, LOWER), (1, UPPER), (2, LEAF)]))
        with self.assertRaises(skin.BuildError) as raised:
            skin.build_bfres(skin_template(SKIN_STATE), model, 'opaque_template')
        self.assertIn('not a cut-out', str(raised.exception))

    def test_output_validator_rejects_corruption(self):
        model, _ = parse(skinned_glb(THREE, [(0, LOWER), (1, UPPER), (2, LEAF)]))
        out = bytearray(skin.build_bfres(skin_template(), model, 'checked'))
        f = base.Bfres(out); fm = f.dict(f.rel(0x20))[0][1]
        fv = f.rel(f.dict(f.rel(fm + 0x14))[1][1] + 0x20); data = f.rel(f.rel(fv + 0x18) + 0x14)
        struct.pack_into('>I', out, data + 40, 1)  # a vertex now names another skin matrix
        with self.assertRaises(skin.BuildError):
            skin.validate_parts(bytes(out), 'checked', model)

    def test_example_reed_walker(self):
        path = ROOT / 'examples/guest-mods/sdk-v3-skinned-materials/assets/make_reed_walker.py'
        spec = importlib.util.spec_from_file_location('reed_walker', path)
        generator = importlib.util.module_from_spec(spec); spec.loader.exec_module(generator)
        self.assertEqual(generator.make(), path.with_name('reed_walker.glb').read_bytes())
        model, info = skin.read_glb(str(path.with_name('reed_walker.glb')))
        self.assertEqual([m.name for m in model['parts'].materials], ['cloth', 'bark', 'leaf'])
        self.assertEqual((info['materials'], info['cutout_materials'], info['bones']), (3, 1, 7))
        self.assertEqual([c['name'] for c in info['clips']], ['idle', 'walk'])
        out = skin.build_bfres(skin_template(), model, 'reed_walker')
        f = base.Bfres(out); fm = f.dict(f.rel(0x20))[0][1]
        self.assertEqual(struct.unpack_from('>HHH', out, fm + 0x20), (3, 3, 3))
        self.assertEqual([n for n, _ in f.dict(f.rel(0x28))], ['idle', 'walk'])
        # the waist ring (y 0.85 m) closes in every pose of both clips' extremes
        cloth, bark = model['parts'].parts[0], model['parts'].parts[1]
        ring = [(i, j) for i, p in enumerate(cloth.pos) for j, q in enumerate(bark.pos) if abs(p[1] - 85) < 1e-3 and p == q and cloth.uv[i] == bark.uv[j]]
        self.assertEqual(len(ring), generator.SEGMENTS + 1)
        for pose in ({1: (.32, .22, 0)}, {1: (-.12, -.22, .1), 0: (0, .3, 0)}):
            posed = posed_shapes(out, pose)
            for i, j in ring:
                for x, y in zip(posed[0][i], posed[1][j]): self.assertAlmostEqual(x, y, places=3)

    def test_archive_build(self):
        with tempfile.TemporaryDirectory() as directory:
            game = pathlib.Path(directory); archive = game / skin.PARTS_ARCHIVE; archive.parent.mkdir(parents=True)
            archive.write_bytes(base.yaz0_stored(base.sarc({'Fobj01.bfres': skin_template(), 'fobj01.sharcfb': b'original synthetic shader'})))
            source = game / 'source.glb'; source.write_bytes(skinned_glb(THREE, [(0, LOWER), (1, UPPER), (2, LEAF)], clips=True))
            out = str(game / 'cache' / 'multi.szs')
            info = skin.build(str(game), str(source), 'multi', out)
            self.assertEqual((info['materials'], info['cutout_materials'], info['triangles']), (3, 1, 6))
            self.assertIn('multi_material', info['converter_sources'])
            files = dict(base.shaderprep.sarc(base.shaderprep.yaz0(pathlib.Path(out).read_bytes())))
            self.assertEqual(set(files), {'multi.bfres', 'multi.sharcfb'})
            self.assertEqual(json.loads(pathlib.Path(out + '.json').read_text())['archive_sha256'], hashlib.sha256(pathlib.Path(out).read_bytes()).hexdigest())


REPRODUCIBLE_SHA256 = 'd535e98bce708bb8e94eb06818b8a2d22b5114cf7116fb7c84fe63a56e6e5f64'

if __name__ == '__main__':
    unittest.main()
