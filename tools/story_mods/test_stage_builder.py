#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Tests of the mod stage builder (stage_builder.py): the chunk files, the room table, the collision's group
levels and exits, the ground model reader and the recipe checks, on generated data. With WWHD_TEST_GAME=<game
folder> it also builds a one-room and a two-room stage from the player's files and reads the archives back."""
import json
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import stage_builder as sb  # noqa: E402
import notebook_model as base  # noqa: E402
import game_piece_model as gp  # noqa: E402


def archive(path):
    with open(path, 'rb') as f:
        return dict(base.shaderprep.sarc(base.shaderprep.yaz0(f.read())))


def write_glb(path, meshes, materials):
    """meshes: [(name, material index, positions in metres, triangles)] -> a GLB with one node per mesh"""
    binary = bytearray()
    views, accessors, gl_meshes, nodes = [], [], [], []

    def part(body):
        binary.extend(b'\0' * (-len(binary) % 4))
        views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(body)})
        binary.extend(body)
        return len(views) - 1
    for name, material, pos, tris in meshes:
        pv = part(struct.pack('<%df' % (3 * len(pos)), *[x for p in pos for x in p]))
        accessors.append({'bufferView': pv, 'componentType': 5126, 'count': len(pos), 'type': 'VEC3'})
        pa = len(accessors) - 1
        iv = part(struct.pack('<%dH' % (3 * len(tris)), *[i for t in tris for i in t]))
        accessors.append({'bufferView': iv, 'componentType': 5123, 'count': 3 * len(tris), 'type': 'SCALAR'})
        gl_meshes.append({'name': name, 'primitives': [{'attributes': {'POSITION': pa}, 'indices': len(accessors) - 1, 'material': material}]})
        nodes.append({'mesh': len(gl_meshes) - 1, 'name': name})
    doc = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}], 'nodes': nodes, 'meshes': gl_meshes,
           'materials': [{'name': m} for m in materials], 'buffers': [{'byteLength': len(binary)}], 'bufferViews': views, 'accessors': accessors}
    text = json.dumps(doc).encode()
    text += b' ' * (-len(text) % 4)
    binary.extend(b'\0' * (-len(binary) % 4))
    body = struct.pack('<I4s', len(text), b'JSON') + text + struct.pack('<I4s', len(binary), b'BIN\0') + bytes(binary)
    with open(path, 'wb') as f:
        f.write(b'glTF' + struct.pack('<II', 2, 12 + len(body)) + body)


def box_room(x0, x1):
    """a floor (two strips) and four walls facing inwards, metres; floor at y 0"""
    pos, tris = [], []

    def quad(a, b, c, d):
        k = len(pos)
        pos.extend((a, b, c, d))
        tris.extend(((k, k + 1, k + 2), (k, k + 2, k + 3)))
    mid = x1 - 3
    quad((x0, 0, -5), (x0, 0, 5), (mid, 0, 5), (mid, 0, -5))     # floor, up
    quad((mid, 0, -5), (mid, 0, 5), (x1, 0, 5), (x1, 0, -5))     # the far strip (the exit)
    quad((x0, 0, -5), (x1, 0, -5), (x1, 4, -5), (x0, 4, -5))     # walls
    quad((x0, 0, 5), (x0, 4, 5), (x1, 4, 5), (x1, 0, 5))
    quad((x0, 0, -5), (x0, 4, -5), (x0, 4, 5), (x0, 0, 5))
    quad((x1, 0, -5), (x1, 0, 5), (x1, 4, 5), (x1, 4, -5))
    return pos, tris


def template_dzs():
    rows = [(0x28, 0, 0, [0xC0, 1]), (0x50, 0, 0, [0xC1, 0])]
    return sb.write_dz([
        ('STAG', 1, struct.pack('>ffBBHI4x', 1.0, 10000.0, 3, (12 << 1), 0x8002, 0x00040000)),
        ('RTBL', 2, sb.rtbl(rows)),
        ('EnvR', 2, bytes(8) + bytes([1] * 8)),
        ('Pale', 1, b'P' * 0x2C),
        ('TRES', 1, b'T' * 0x20),
        ('SCLS', 1, sb.scls_entry('sea', 1, 34)),
        ('RCAM', 1, b'C' * 0x14),
    ])


def kit_dzr():
    return sb.write_dz([('SCLS', 1, sb.scls_entry('sea', 1, 34)), ('FILI', 1, bytes.fromhex('5fec5f0200000000')), ('LBNK', 12, b'\xff' * 12),
                        ('PLYR', 1, bytes(0x20)), ('ACTR', 1, bytes(0x20)), ('LGTV', 1, b'L' * 0x1C)])


def recipe(rooms=1, doors=False):
    r = {'format': 1, 'name': 'Zabc123', 'template': 'Cave01', 'kind': 'cave', 'entrance': {'stage': 'sea', 'room': 34, 'point': 1}, 'rooms': [], 'doors': []}
    for k in range(rooms):
        r['rooms'].append({'room': k, 'kit': {'stage': 'Cave01', 'room': k}, 'ground': '/x.glb', 'spawns': [{'point': k, 'position': [0, 0, 0], 'angle': 90}],
                           'exits': [{'min': [100, -100, -500], 'max': [400, 100, 500], 'to': {'stage': 'sea', 'room': 34, 'point': 1}}]})
    if doors:
        r['doors'] = [{'position': [-1000, -200, 0], 'angle': 270, 'rooms': [0, 1]}]
    return r


class ChunkFiles(unittest.TestCase):
    def test_round_trip_and_room_table(self):
        d = template_dzs()
        chunks = sb.read_dz(d)
        self.assertEqual([c[0] for c in chunks], ['STAG', 'RTBL', 'EnvR', 'Pale', 'TRES', 'SCLS', 'RCAM'])
        t = sb.chunk(chunks, 'RTBL')
        self.assertEqual(sb.read_rtbl(d, t[3], t[1]), [(0x28, 0, 0, [0xC0, 1]), (0x50, 0, 0, [0xC1, 0])])
        for tag, count, data, at in chunks:
            self.assertEqual(at % 4, 0)

    def test_stage_file(self):
        r = recipe(2, doors=True)
        d = sb.build_dzs(template_dzs(), r)
        chunks = sb.read_dz(d)
        tags = [c[0] for c in chunks]
        self.assertNotIn('TRES', tags)          # the template's treasure is its own rooms'
        for kept in ('Pale', 'RCAM'):
            self.assertIn(kept, tags)
        stag = sb.chunk(chunks, 'STAG')[2]
        self.assertEqual(stag[9] >> 1, 15)      # save slot 15
        self.assertEqual(struct.unpack_from('>H', stag, 0xA)[0] & 3, 0)  # map mode 0
        self.assertEqual((struct.unpack_from('>I', stag, 0xC)[0] >> 16) & 7, 4)  # a cave
        scls = sb.chunk(chunks, 'SCLS')
        self.assertEqual(scls[1], 1)            # the stage-level exit list: the entrance
        self.assertEqual(scls[2][:12], sb.scls_entry('sea', 1, 34))
        envr = sb.chunk(chunks, 'EnvR')
        self.assertEqual((envr[1], envr[2][8:16]), (2, bytes([1] * 8)))
        door = sb.chunk(chunks, 'TGDR')[2]
        self.assertEqual(door[:6], b'door10')
        ax, = struct.unpack_from('>H', door, 0x18)
        self.assertEqual((ax & 0x3F, (ax >> 6) & 0x3F), (0, 1))
        sb.check_room_table(d, r['doors'])
        t = sb.chunk(chunks, 'RTBL')
        self.assertEqual(sb.read_rtbl(d, t[3], t[1]), [(0x28, 0, 0, [0xC0, 1]), (0x50, 0, 0, [0xC1, 0])])

    def test_room_table_check_finds_a_missing_neighbour(self):
        d = sb.write_dz([('RTBL', 2, sb.rtbl([(0, 0, 0, [0xC0, 1]), (0, 0, 0, [0xC1])]))])
        with self.assertRaises(sb.BuildError):
            sb.check_room_table(d, [{'rooms': [0, 1]}])

    def test_room_file(self):
        r = recipe(1)['rooms'][0]
        r['spawns'].append({'point': 5, 'position': [1, 2, 3], 'angle': 180, 'start': 'fall'})
        d = sb.build_dzr(kit_dzr(), 0, r)
        chunks = sb.read_dz(d)
        self.assertEqual([c[0] for c in chunks], ['FILI', 'LBNK', 'PLYR', 'SCLS', 'LGTV'])
        plyr = sb.chunk(chunks, 'PLYR')
        self.assertEqual(plyr[1], 2)
        e = plyr[2][0x20:0x40]
        self.assertEqual(e[:4], b'Link')
        self.assertEqual(struct.unpack_from('>I', e, 8)[0], 0xFFFFF000)  # start mode 15 (fall), room 0
        self.assertEqual(e[0x1D], 5)
        self.assertEqual(struct.unpack_from('>h', e, 0x1A)[0], -32768)
        scls = sb.chunk(chunks, 'SCLS')
        self.assertEqual(scls[2][:12], sb.scls_entry('sea', 1, 34))


class Phase2(unittest.TestCase):
    def test_actors_doors_joins(self):
        r = recipe(3, doors=True)
        r['doors'][0].update(name='door10', params=0x0FFFF47F)
        r['joins'] = [[1, 2]]
        r['keys'] = True
        r['rooms'][0]['actors'] = [{'tag': 'TRES', 'name': 'takara2', 'params': 0xFF000080, 'position': [1, 2, 3], 'angle': 90, 'angle_z': 0x15FF},
                                   {'tag': 'ACTR', 'name': 'Bk', 'params': 0xFFFFFF04, 'position': [4, 5, 6], 'angle': 0, 'angle_x': 0, 'angle_z': 0xFF}]
        sb.check_recipe(r)
        d = sb.build_dzs(template_dzs(), r)
        sb.check_room_table(d, r['doors'], r['joins'])
        chunks = sb.read_dz(d)
        self.assertEqual(sb.chunk(chunks, 'STAG')[2][9] & 1, 1)  # the key counter
        door = sb.chunk(chunks, 'TGDR')[2]
        self.assertEqual(struct.unpack_from('>I', door, 8)[0], 0x0FFFF47F)
        t = sb.chunk(chunks, 'RTBL')
        rows = sb.read_rtbl(d, t[3], t[1])
        self.assertEqual([[e & 0x3F for e in row[3]] for row in rows], [[0, 1], [1, 0, 2], [2, 1]])
        dzr = sb.build_dzr(kit_dzr(), 2, r['rooms'][0])
        rc = sb.read_dz(dzr)
        tres = sb.chunk(rc, 'TRES')[2]
        self.assertEqual(tres[:7], b'takara2')
        self.assertEqual(struct.unpack_from('>HhH', tres, 0x18), (2, 16384, 0x15FF))  # x: the room, y: the heading, z: the item
        actr = sb.chunk(rc, 'ACTR')[2]
        self.assertEqual((actr[:2], struct.unpack_from('>I', actr, 8)[0]), (b'Bk', 0xFFFFFF04))
        with self.assertRaises(sb.BuildError):
            sb.check_room_table(d, [], [[0, 2]])  # 0 and 2 aren't joined

    def test_kit_collision(self):
        pos, tris = box_room(-10, 10)
        prims = [{'collision': False, 'material': 'rock', 'positions': [tuple(100 * x for x in p) for p in pos], 'triangles': tris}]
        verts, ctris, _ = sb.collision_from(prims, [{'min': [-1000, -10, -500], 'max': [690, 10, 500]}])  # a game exit in the kit
        kit = sb.build_dzb(verts, ctris, 0)
        groups0, out0 = sb.read_dzb_summary(kit)
        self.assertIn(0, [e for _, e in out0])
        dzb, taken = sb.kit_collision(kit, 5, [{'min': [700, -10, -500], 'max': [1000, 10, 500]}])
        groups, out = sb.read_dzb_summary(dzb)
        self.assertEqual(taken, [2])
        self.assertEqual(groups[0][3], 5)            # the room
        self.assertEqual(groups[2][4] & 0xFF, 5)     # the room table row
        self.assertEqual(sorted(e for _, e in out if e != sb.NO_EXIT), [0, 0])  # the kit's own exit is gone
        self.assertEqual([g[0] for g in groups], [g[0] for g in groups0])


class Map(unittest.TestCase):
    def test_room_picture(self):
        pos, tris = box_room(-10, 10)
        prims = [{'collision': False, 'material': 'rock', 'positions': [tuple(100 * x for x in p) for p in pos], 'triangles': tris}]
        verts, ctris, _ = sb.collision_from(prims, [])
        floors = sb.floor_triangles(sb.build_dzb(verts, ctris, 0))
        self.assertEqual(len(floors), 4)  # the floor's two strips, not the walls
        x0, z0, w, h, rows = sb.room_map(floors, 20.0)
        self.assertEqual((w, h), (102, 52))
        px = lambda x, y: rows[y * (4 * w + 1) + 1 + 4 * x:y * (4 * w + 1) + 5 + 4 * x]
        self.assertEqual(px(0, 0)[3], 0)               # outside the floor: clear
        self.assertEqual(px(1, 25), b'\x60\x60\x60\xff')  # its edge: dark
        self.assertEqual(px(50, 25)[3], 0xFF)          # inside
        self.assertGreater(px(50, 25)[0], 0x60)
        self.assertEqual(sb.png(w, h, rows)[:8], b'\x89PNG\r\n\x1a\n')


class Collision(unittest.TestCase):
    def test_groups_exits_water(self):
        pos, tris = box_room(-10, 10)
        prims = [{'collision': False, 'material': 'rock', 'positions': [tuple(100 * x for x in p) for p in pos], 'triangles': tris,
                  'normals': None, 'uvs': None, 'colors': None},
                 {'collision': False, 'material': 'col_water', 'positions': [(0, -50, 0), (100, -50, 0), (0, -50, 100)],
                  'triangles': [(0, 2, 1)], 'normals': None, 'uvs': None, 'colors': None}]
        exits = [{'min': [700, -10, -500], 'max': [1000, 10, 500]}]
        verts, ctris, taken = sb.collision_from(prims, exits)
        self.assertEqual(taken, [2])            # the far floor strip, not the walls beside it
        d = sb.build_dzb(verts, ctris, 3)
        groups, out = sb.read_dzb_summary(d)
        self.assertEqual([g[0] for g in groups], ['R03_check', 'ground', 'floor', 'water'])
        # a triangle's room is its group's grandparent's
        for grp, _ in out:
            self.assertEqual(groups[groups[groups[grp][1]][1]][3], 3)
        self.assertEqual(groups[2][4], 3)            # the room table row
        self.assertEqual(groups[3][4], 3 | 0x100)    # the water bit
        self.assertEqual(sorted(e for _, e in out if e != sb.NO_EXIT), [0, 0])
        self.assertEqual(sum(1 for g, _ in out if g == 3), 1)

    def test_surface_codes(self):
        codes = sb.surface_codes()
        self.assertEqual(len(codes), len(sb.SURFACES))
        for code, cls in zip(codes, sb.SURFACE_CLASS):
            self.assertEqual(gp.ATTRIBUTE_CLASS[code], cls)

    def test_unknown_collision_material(self):
        prims = [{'collision': True, 'material': 'col_lava', 'positions': [(0, 0, 0), (1, 0, 0), (0, 0, 1)], 'triangles': [(0, 2, 1)]},
                 {'collision': False, 'material': 'rock', 'positions': [], 'triangles': []}]
        with self.assertRaises(sb.BuildError):
            sb.collision_from(prims, [])


class Ground(unittest.TestCase):
    def test_glb(self):
        with tempfile.TemporaryDirectory() as d:
            pos, tris = box_room(-10, 10)
            path = os.path.join(d, 'room.glb')
            write_glb(path, [('room', 0, pos, tris), ('room_col', 1, pos, tris)], ['rock', 'col_grass'])
            prims = sb.read_glb(path)
            self.assertEqual([p['collision'] for p in prims], [False, True])
            self.assertEqual(prims[0]['positions'][0], (-1000.0, 0.0, -500.0))  # metres x 100
            self.assertAlmostEqual(prims[0]['normals'][0][1], 1.0)
            verts, ctris, taken = sb.collision_from(prims, [])
            self.assertTrue(all(t[3] == sb.SURFACES.index('col_grass') for t in ctris))


class Recipe(unittest.TestCase):
    def test_checks(self):
        sb.check_recipe(recipe(2, doors=True))
        bad = []
        r = recipe(2)
        r['rooms'][1]['room'] = 3
        bad.append(r)
        r = recipe(2)
        r['rooms'][1]['spawns'][0]['point'] = 0
        bad.append(r)
        r = recipe(1)
        r['rooms'][0]['exits'][0]['to'] = {'stage': 'Zabc123', 'room': 0, 'point': 9}
        bad.append(r)
        r = recipe(2, doors=True)
        r['doors'][0]['rooms'] = [1, 1]
        bad.append(r)
        r = recipe(1)
        r['name'] = 'Cave01'
        bad.append(r)
        r = recipe(1)
        r['entrance']['stage'] = 'Zabc123'
        bad.append(r)
        for r in bad:
            with self.assertRaises(sb.BuildError):
                sb.check_recipe(r)


@unittest.skipUnless(os.environ.get('WWHD_TEST_GAME'), 'set WWHD_TEST_GAME to a game folder to build from the player\'s files')
class FromGameFiles(unittest.TestCase):
    def test_build(self):
        game = os.environ['WWHD_TEST_GAME']
        with tempfile.TemporaryDirectory() as d:
            for rooms, x in ((1, (-36, -10)), (2, None)):
                r = recipe(rooms, doors=rooms == 2)
                if rooms == 2:  # the second room: the template's own room 1 as it is, with a chest and an enemy
                    r['rooms'][1]['kit_ground'] = True
                    r['rooms'][1]['actors'] = [{'tag': 'TRES', 'name': 'takara2', 'params': 0xFF000000, 'position': [0, 0, 0], 'angle_z': 0x15FF}]
                for k, room in enumerate(r['rooms']):
                    x0, x1 = x or ((-36, -10) if k == 0 else (-10, 16))
                    pos, tris = box_room(x0, x1)
                    room['ground'] = os.path.join(d, 'room%d.glb' % k)
                    write_glb(room['ground'], [('room', 0, pos, tris)], ['rock'])
                    if room.get('kit_ground'):  # an exit on the kit's floor near its spawn
                        room['ground'] = ''
                        room['exits'][0]['min'], room['exits'][0]['max'] = [-1100, -300, -300], [-500, -100, 300]
                        room['spawns'][0]['position'] = [-800, -200, 0]
                        continue
                    room['spawns'][0]['position'] = [100 * (x0 + 2), 0, 0]
                    room['exits'][0]['min'] = [100 * (x1 - 3), -10, -500]
                    room['exits'][0]['max'] = [100 * x1, 10, 500]
                path = os.path.join(d, 'recipe.json')
                with open(path, 'w') as f:
                    json.dump(r, f)
                out = os.path.join(d, 'out%d' % rooms)
                meta = sb.build(game, path, out)
                self.assertFalse(meta['cached'])
                self.assertEqual(sorted(meta['files']), ['Zabc123_Room%d.szs' % k for k in range(rooms)] + ['Zabc123_Stage.szs'])
                for k in range(rooms):
                    files = archive(os.path.join(out, 'Zabc123_Room%d.szs' % k))
                    self.assertIn('Room%d.bfres' % k, files)
                    self.assertIn('model.sharcfb', files)
                    src = base.Bfres(files['Room%d.bfres' % k])
                    if not r['rooms'][k].get('kit_ground'):
                        self.assertEqual([n for n, _ in src.dict(src.rel(0x20))], ['model'])
                    emb = sb.embedded_files(src)
                    groups, tris = sb.read_dzb_summary(emb['room.dzb'])
                    self.assertEqual(groups[0][3], k)
                    self.assertEqual(sb.chunk(sb.read_dz(emb['room.dzr']), 'PLYR')[1], 1)
                stage = archive(os.path.join(out, 'Zabc123_Stage.szs'))
                bf = [n for n in stage if n.endswith('.bfres')][0]
                dzs = sb.embedded_files(base.Bfres(stage[bf]))['stage.dzs']
                sb.check_room_table(dzs, r['doors'])
                self.assertTrue(sb.build(game, path, out)['cached'])  # unchanged inputs: the cache


if __name__ == '__main__':
    unittest.main()
