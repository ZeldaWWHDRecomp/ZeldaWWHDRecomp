# SPDX-License-Identifier: MPL-2.0
"""GLB export of game rooms and pieces (export_glb.py), on synthetic data: texture decoding (BC1, BC4 with the
texture's channel selection, tiled addresses), vertex attribute formats, bone transforms of normals, collision by
ground type and the GLB itself (metres, Y up, materials named as the game's, collision object, actor empties).
With WWHD_GAME_DIR set to a game folder, it also exports Outset (sea room 44) and one building from the player's
files into a temporary folder and checks them; nothing is kept."""
import contextlib
import io
import json
import math
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import export_glb as ex  # noqa: E402
import game_piece_model as gp  # noqa: E402
import notebook_model as base  # noqa: E402
from test_game_piece_model import dzb, dzb_groups, recipes  # noqa: E402


def read_glb(body):
    """(document, binary chunk, accessor reader) of GLB bytes"""
    magic, version, total = struct.unpack_from('<4sII', body, 0)
    assert magic == b'glTF' and version == 2 and total == len(body)
    jlen, kind = struct.unpack_from('<I4s', body, 12)
    assert kind == b'JSON'
    doc = json.loads(body[20:20 + jlen])
    blen, kind = struct.unpack_from('<I4s', body, 20 + jlen)
    assert kind == b'BIN\0'
    binary = body[28 + jlen:28 + jlen + blen]

    def accessor(i):
        a = doc['accessors'][i]
        v = doc['bufferViews'][a['bufferView']]
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
        flat = struct.unpack_from('<%d%s' % (a['count'] * n, {5126: 'f', 5125: 'I'}[a['componentType']]), binary, v['byteOffset'])
        return [flat[k * n:(k + 1) * n] for k in range(a['count'])] if n > 1 else list(flat)
    return doc, binary, accessor


def bc1(c0, c1, indices):
    return struct.pack('<HHI', c0, c1, sum(i << (2 * k) for k, i in enumerate(indices)))


class Textures(unittest.TestCase):
    def test_bc1_linear_surface(self):
        red, blue = 0xF800, 0x001F
        # 8 x 4 texels: two blocks in a row, linear (mode 1), pitch 2 blocks
        data = bc1(red, blue, [0] * 16) + bc1(red, blue, [1] * 8 + [0] * 8)
        rgba = ex.decode_surface(data, 8, 4, 0x31, 1, 0, 2)
        self.assertEqual(rgba[0:4], bytes((255, 0, 0, 255)))
        self.assertEqual(rgba[4 * 4:4 * 4 + 4], bytes((0, 0, 255, 255)))  # texel (4, 0): second block, index 1
        self.assertEqual(rgba[(2 * 8 + 4) * 4:(2 * 8 + 4) * 4 + 4], bytes((255, 0, 0, 255)))  # (4, 2): index 0
        # three-colour mode (c0 <= c1): index 3 is transparent black
        self.assertEqual(ex.colour_block(bc1(blue, red, [3] * 16))[0], b'\0\0\0\0')
        # BC3's colour block always uses four colours
        self.assertEqual(ex.colour_block(bc1(blue, red, [3] * 16), True)[0][3], 255)

    def test_bc4_with_the_textures_channel_selection(self):
        block = bytes((200, 40)) + (0).to_bytes(6, 'little')  # every texel index 0: 200
        rgba = ex.decode_surface(block, 4, 4, 0x34, 1, 0, 1, (0, 0, 0, 0))  # comp_sel RRRR: grey with alpha
        self.assertEqual(rgba[:4], bytes((200, 200, 200, 200)))
        rgba = ex.decode_surface(block * 2, 4, 4, 0x35, 1, 0, 1, (0, 0, 0, 1))  # BC5 RRRG: intensity and alpha
        self.assertEqual(rgba[:4], bytes((200, 200, 200, 200)))
        self.assertEqual(ex.value_block(bytes((10, 250)) + (7 << 0).to_bytes(6, 'little'))[0], 255)  # 6-value mode: 7 is 255

    def test_tiled_addresses(self):
        # block-compressed elements: the same addresses as the game pieces converter's
        for bpp in (64, 128):
            for mode in (2, 4):
                for x, y in ((0, 0), (5, 3), (33, 17), (63, 40)):
                    self.assertEqual(ex.surface_address(x, y, 64, bpp, mode, 0x300), gp.tiled_address(x, y, 64, bpp, mode, 0x300))
        # every element size: four macro tiles map their elements onto distinct, aligned addresses of the surface
        for bpp in (8, 16, 32, 64, 128):
            seen = {ex.surface_address(x, y, 64, bpp, 4, 0) for x in range(64) for y in range(32)}
            self.assertEqual(len(seen), 64 * 32)
            self.assertTrue(all(a % (bpp // 8) == 0 and a < 64 * 32 * bpp // 8 for a in seen))

    def test_png_and_shrink(self):
        w, h, rgba = ex.shrink(4, 2, bytes([10, 20, 30, 255] * 4 + [30, 40, 50, 255] * 4), 2)
        self.assertEqual((w, h), (2, 1))
        self.assertEqual(rgba[:4], bytes((20, 30, 40, 255)))
        self.assertEqual(base.decode_png(ex.png(w, h, rgba)), (2, 1, rgba))


class Geometry(unittest.TestCase):
    def test_vertex_formats(self):
        n = (511 & 0x3FF) | ((-511 & 0x3FF) << 10) | (0 << 20)
        self.assertEqual(ex.attribute(struct.pack('>I', n), 0, 0x20B), (1.0, -1.0, 0.0))
        self.assertEqual(ex.attribute(bytes((255, 0, 51, 255)), 0, 0x00A), (1.0, 0.0, 0.2, 1.0))
        self.assertEqual(ex.attribute(struct.pack('>2e', 0.5, 2.0), 0, 0x808), (0.5, 2.0))
        self.assertIsNone(ex.attribute(b'\0' * 16, 0, 0x123))

    def test_normals_follow_a_scaled_bone(self):
        m = [[2, 0, 0], [0, 1, 0], [0, 0, 1]]  # stretched along X: a 45 degree normal leans towards X less
        it, det = ex.inverse_transpose(m)
        self.assertGreater(det, 0)
        v = [it[j][0] * 1 + it[j][1] * 1 for j in range(3)]
        self.assertAlmostEqual(v[0] / v[1], 0.5)
        self.assertIsNone(ex.inverse_transpose([[0, 0, 0], [0, 1, 0], [0, 0, 1]])[0])

    def test_collision_by_ground_type(self):
        # grass floor (raw code 4), a wall, a water group, all in a sea room 100000 units off
        o = 100000.0
        verts = [(o, 0, 0), (o, 0, 100), (o + 100, 0, 0), (o, 0, 0), (o, 100, 0), (o, 0, 100)]
        infos = [(0x3F << 13, 4 << 16, 0, 0), (0x3F << 13, 0, 0, 0)]
        d = dzb_groups(verts, [(0, 1, 2, 0, 0), (3, 4, 5, 1, 0), (0, 1, 2, 1, 1)], ['ground', 'sea'], [0, 0x100], infos)
        parts = ex.collision_parts(gp.read_dzb(d), (o, 0, 0), (0, 0, 0))
        got = {p.material: len(p.tris) for p in parts}
        self.assertEqual(got, {'col_grass': 1, 'col_wall': 1, 'col_water': 1})
        grass = next(p for p in parts if p.material == 'col_grass')
        self.assertEqual(sorted(grass.pos), [(0, 0, 0), (0, 0, 100), (100, 0, 0)])
        # a piece's rules: an exit left out, a void group's floor a void
        d = dzb(verts, [(0, 1, 2, 0, 0), (0, 1, 2, 1, 1)], ['ground', 'hole'], [(5 << 13, 4 << 16, 0, 0), (0x3F << 13, 0, 0, 0)])
        parts = ex.collision_parts(d, (o, 0, 0), (0, 0, 0), lambda g, pts, exit_id: exit_id == 0x3F, {'hole'})
        self.assertEqual({p.material: len(p.tris) for p in parts}, {'col_void': 1})


class File(unittest.TestCase):
    def test_glb(self):
        p = ex.Part('model/ie', 'A_ie_a_v')
        p.pos = [(0, 0, 0), (100, 0, 0), (0, 250, -100)]
        p.normals = [(0, 0, 1)] * 3
        p.uv0 = [(0, 0), (1, 0), (0, 1)]
        p.colours = [(1, 0.5, 0.25, 1)] * 3
        p.tris = [(0, 1, 2)]
        q = ex.Part('model1/umi', 'SC_01_mizu')
        q.pos, q.tris = list(p.pos), [(0, 2, 1)]
        col = ex.Part('collision', 'col_wood')
        col.pos, col.tris = list(p.pos), [(0, 1, 2)]
        textures = {'Txa_ie_a': (2, 2, bytes([200, 100, 50, 255]) * 4), 'Txa_cut': (2, 2, bytes([1, 2, 3, 0]) * 4)}
        materials = {'A_ie_a_v': ([('_a0', 'Txa_ie_a'), ('_a1', 'Txa_cut')], {'draw_buffer_type': [1]}), 'SC_01_mizu': ([], {})}
        actors = [{'name': 'swood', 'kind': 'ACTR', 'parameters': '00000000', 'position': [500.0, 0.0, -300.0], 'angle': 90.0}]
        body, summary = ex.write_glb('sea_room44', [p, q], materials, textures, [col], actors, {'wwhd': {'room': 44}})
        doc, binary, accessor = read_glb(body)
        self.assertEqual(summary['triangles'], 2)
        self.assertEqual(summary['materials'], 2)
        self.assertEqual(summary['textures'], 2)
        self.assertEqual(summary['collision_triangles'], 1)
        self.assertEqual(summary['actors'], 1)
        # materials carry the game's names (kit materials) and the sampler -> texture table
        names = [m['name'] for m in doc['materials']]
        self.assertEqual(names, ['A_ie_a_v', 'SC_01_mizu', 'col_wood'])
        ie = doc['materials'][0]
        self.assertEqual(ie['extras']['wwhd']['samplers'], {'_a0': 'Txa_ie_a', '_a1': 'Txa_cut'})
        self.assertEqual(doc['images'][doc['textures'][ie['pbrMetallicRoughness']['baseColorTexture']['index']]['source']]['name'], 'Txa_ie_a')
        self.assertNotIn('alphaMode', ie)  # opaque colour texture
        for image in doc['images']:
            v = doc['bufferViews'][image['bufferView']]
            self.assertEqual(base.decode_png(binary[v['byteOffset']:v['byteOffset'] + v['byteLength']])[:2], (2, 2))
        # metres: 100 game units each; axes unchanged (Y up)
        mesh = next(m for m in doc['meshes'] if m['name'] == 'model/ie')
        prim = mesh['primitives'][0]
        self.assertEqual(accessor(prim['attributes']['POSITION']), [(0, 0, 0), (1, 0, 0), (0, 2.5, -1)])
        self.assertEqual(accessor(prim['attributes']['COLOR_0'])[0], (1, 0.5, 0.25, 1))
        self.assertEqual(accessor(prim['indices']), [0, 1, 2])
        self.assertNotIn('NORMAL', doc['meshes'][1]['primitives'][0]['attributes'])
        # objects: the room, its models, their nodes, the collision object and the actors' empties
        nodes = {n['name']: n for n in doc['nodes']}
        self.assertEqual(doc['scenes'][0]['nodes'], [0])
        self.assertEqual(nodes['sea_room44']['extras'], {'wwhd': {'room': 44}})
        self.assertIn('ie', nodes and [doc['nodes'][c]['name'] for c in nodes['model']['children']])
        self.assertIn('mesh', nodes['sea_room44_col'])
        a = nodes['swood 00000000']
        self.assertEqual(a['translation'], [5.0, 0.0, -3.0])
        self.assertAlmostEqual(a['rotation'][1], math.sin(math.pi / 4))

    def test_cut_out_and_actor_reader(self):
        p = ex.Part('model/tree', 'leaf')
        p.pos, p.tris, p.uv0 = [(0, 0, 0), (1, 0, 0), (0, 1, 0)], [(0, 1, 2)], [(0, 0)] * 3
        textures = {'Txa_leaf': (1, 1, bytes((0, 255, 0, 0)))}
        body, _ = ex.write_glb('x', [p], {'leaf': ([('_a0', 'Txa_leaf')], {'draw_buffer_type': [1]})}, textures)
        m = read_glb(body)[0]['materials'][0]
        self.assertEqual((m['alphaMode'], m['alphaCutoff'], m['doubleSided']), ('MASK', 0.5, True))
        entry = b'swood\0\0\0' + struct.pack('>I3f3hH', 0x12, 100500.0, 10.0, 20.0, 0, 0x4000, 0, 0xFFFF)
        dzr = struct.pack('>I4sII', 1, b'ACTR', 1, 16) + entry
        got = ex.room_actors(dzr, (100000.0, 0, 0), (0, 0, 0))
        self.assertEqual(got, [{'name': 'swood', 'kind': 'ACTR', 'parameters': '00000012', 'position': [500.0, 10.0, 20.0], 'angle': 90.0}])
        self.assertEqual(ex.room_actors(b'\0\0', (0, 0, 0), (0, 0, 0)), [])


class Pieces(unittest.TestCase):
    def test_every_catalogue_piece_is_found(self):
        for name, r in recipes():
            for p in r['pieces']:
                recipe, piece = ex.find_piece(p['id'])
                self.assertEqual(piece['id'], p['id'])
        with self.assertRaises(ex.BuildError):
            ex.find_piece('no-such-piece')


@unittest.skipUnless(os.environ.get('WWHD_GAME_DIR'), 'set WWHD_GAME_DIR to export from a game folder')
class FromGame(unittest.TestCase):
    def test_outset_room(self):
        game = os.environ['WWHD_GAME_DIR']
        body, summary = ex.export_room(game, 'sea', 44, collision=True, actors=True, max_texture=256)
        doc, _, accessor = read_glb(body)
        self.assertGreater(summary['triangles'], 5000)
        self.assertGreater(summary['collision_triangles'], 1000)
        self.assertGreater(summary['actors'], 0)
        self.assertEqual(summary['warnings'], [])
        self.assertTrue({'col_grass', 'col_sand', 'col_wood'} <= set(summary['surfaces']))
        # the materials are the kit room's own: every kit material name is a material of the file
        import stage_builder
        kit = {m['material'] for m in stage_builder.kit_room_materials(game, 'sea', 44)['materials']}
        self.assertTrue(kit <= {m['name'] for m in doc['materials']})
        # Outset lies inside its square, in metres (a square is 1000 m)
        for m in doc['meshes']:
            for prim in m['primitives']:
                a = doc['accessors'][prim['attributes']['POSITION']]
                self.assertTrue(all(-500.5 <= v <= 500.5 for v in a['min'] + a['max']))

    def test_building_matches_its_game_piece(self):
        game = os.environ['WWHD_GAME_DIR']
        for name, r in recipes():
            for p in r['pieces']:
                if p['id'] not in ('outset-house-ladder', 'tingle-tower'):
                    continue
                body, summary = ex.export_piece(game, p['id'], collision=True)
                read_glb(body)
                self.assertEqual(summary['triangles'], sum(p['expect']['triangles']))
                self.assertEqual(summary['collision_triangles'], p['expect']['collision_triangles'])
                self.assertEqual(sorted(summary['texture_names']), sorted({t for part in p['expect']['textures'] for t in part}))

    def test_command_line_writes_only_the_file(self):
        game = os.environ['WWHD_GAME_DIR']
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, 'house.glb')
            sys.argv = ['export_glb.py', '--game', game, '--piece', 'outset-house-small', '--out', out, '--max-texture', '128', '--json']
            with contextlib.redirect_stdout(io.StringIO()) as printed:
                ex.main()
            self.assertTrue(json.loads(printed.getvalue())['ok'])
            self.assertEqual(os.listdir(d), ['house.glb'])


if __name__ == '__main__':
    unittest.main()
