# SPDX-License-Identifier: MPL-2.0
"""Game pieces converter: the port's recipes, the selection rule (whole components only, never a cut)
and the collision rules (listed groups inside the volume; exits and other native fields dropped; ground
types mapped to world-object surfaces), on synthetic data. With WWHD_GAME_DIR set to a game folder, it
also builds every recipe from the player's files into a temporary folder and checks the counts."""
import hashlib
import json
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import game_piece_model as gp  # noqa: E402
import collision_mesh  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
RECIPES = os.path.join(ROOT, 'runtime', 'assets', 'game_pieces')


def recipes():
    for name in sorted(os.listdir(RECIPES)):
        if name.endswith('.recipe.json'):
            with open(os.path.join(RECIPES, name)) as f:
                yield name, json.load(f)


def shape(node, pos, tris, material='m'):
    s = gp.Shape()
    s.node, s.pos, s.tris, s.material = node, pos, tris, material
    return s


def dzb_groups(verts, tris, names, group_info, infos):
    """dzb() bytes with each group's info word (the water bit 0x100)"""
    head = 0x34
    ov = head
    ot = ov + 12 * len(verts)
    og = ot + 10 * len(tris)
    names_at = og + 0x34 * len(names)
    name_bytes, name_offsets = b'', []
    for n in names:
        name_offsets.append(names_at + len(name_bytes))
        name_bytes += n.encode() + b'\0'
    oi = names_at + len(name_bytes)
    oi += -oi % 4
    out = bytearray(oi + 16 * len(infos))
    struct.pack_into('>12I', out, 0, len(verts), ov, len(tris), ot, 0, 0, 0, 0, len(names), og, len(infos), oi)
    for i, v in enumerate(verts):
        struct.pack_into('>3f', out, ov + 12 * i, *v)
    for i, t in enumerate(tris):
        struct.pack_into('>5H', out, ot + 10 * i, *t)
    for i, at in enumerate(name_offsets):
        struct.pack_into('>I', out, og + 0x34 * i, at)
        struct.pack_into('>I', out, og + 0x34 * i + 0x30, group_info[i])
    out[names_at:names_at + len(name_bytes)] = name_bytes
    for i, info in enumerate(infos):
        struct.pack_into('>4I', out, oi + 16 * i, *info)
    return bytes(out)


def dzb(verts, tris, names, infos):
    """A minimal native collision file: header, vertices, triangles, groups, infos (no tree)."""
    head = 0x34
    ov = head
    ot = ov + 12 * len(verts)
    og = ot + 10 * len(tris)
    names_at = og + 0x34 * len(names)
    name_bytes, name_offsets = b'', []
    for n in names:
        name_offsets.append(names_at + len(name_bytes))
        name_bytes += n.encode() + b'\0'
    oi = names_at + len(name_bytes)
    oi += -oi % 4
    out = bytearray(oi + 16 * len(infos))
    struct.pack_into('>12I', out, 0, len(verts), ov, len(tris), ot, 0, 0, 0, 0, len(names), og, len(infos), oi)
    for i, v in enumerate(verts):
        struct.pack_into('>3f', out, ov + 12 * i, *v)
    for i, t in enumerate(tris):
        struct.pack_into('>5H', out, ot + 10 * i, *t)
    for i, at in enumerate(name_offsets):
        struct.pack_into('>I', out, og + 0x34 * i, at)
    out[names_at:names_at + len(name_bytes)] = name_bytes
    for i, info in enumerate(infos):
        struct.pack_into('>4I', out, oi + 16 * i, *info)
    return gp.read_dzb(bytes(out))


class Recipes(unittest.TestCase):
    def test_port_recipes_follow_the_rules(self):
        seen = set()
        for name, r in recipes():
            with self.subTest(recipe=name):
                gp.check_recipe(r)
                self.assertEqual(name, r['set'] + '.recipe.json')
                for p in r['pieces']:
                    self.assertNotIn(p['id'], seen)
                    seen.add(p['id'])
                    # references only: no game bytes, no file paths other than the room archive's name
                    text = json.dumps(p)
                    self.assertNotIn('/', text)
        self.assertIn('outset-house-small', seen)

    def test_every_part_pins_its_source_textures(self):
        # each part names the room textures its materials sample; the build refuses another look
        pinned = {}
        for name, r in recipes():
            for p in r['pieces']:
                t = p['expect'].get('textures')
                self.assertIsInstance(t, list, p['id'])
                self.assertEqual(len(t), len(p['parts']), p['id'])
                self.assertTrue(all(x for x in t), p['id'])
                pinned[p['id']] = t
        # the sea rocks are Dragon Roost's cliff stone (its streaked look is the original's), the torii its own wood
        for rock in ('sea-rock-spire', 'sea-rock-pillar', 'sea-rock-stack'):
            self.assertEqual(pinned[rock], [['Txe_Edragon_gake']])
        self.assertIn('Txe_Edragon_tesuri', pinned['dragon-roost-torii'][0])
        self.assertEqual(pinned['shipwreck-hull'], [['wood0313_22']])
        r = next(r for _, r in recipes())
        bad = json.loads(json.dumps(r))
        bad['pieces'][0]['expect']['textures'] = [['../x']]
        with self.assertRaises(gp.BuildError):
            gp.check_recipe(bad)

    def test_bad_recipes_refused(self):
        _, good = next(recipes())
        for change in (lambda r: r.update(game_pieces_format=2), lambda r: r['source'].update(archive='../x.szs'),
                       lambda r: r['pieces'][0].update(volume=[1, 2, 3]), lambda r: r['pieces'][0]['parts'][0].update(light='actor'),
                       lambda r: r['pieces'][0]['expect'].update(triangles=[1, 2, 3])):
            r = json.loads(json.dumps(good))
            change(r)
            with self.assertRaises(gp.BuildError):
                gp.check_recipe(r)


class Selection(unittest.TestCase):
    def test_whole_components_only(self):
        # two separate quads: one inside the volume, one crossing it; plus a quad of another node
        inside = [(0, 0, 0), (10, 0, 0), (10, 0, 10), (0, 0, 10)]
        crossing = [(50, 0, 0), (150, 0, 0), (150, 0, 10), (50, 0, 10)]
        pos = inside + crossing
        a = shape('ie', pos, [(0, 1, 2), (0, 2, 3), (4, 5, 6), (4, 6, 7)])
        b = shape('jimen', inside, [(0, 1, 2)])
        taken, crossing_count = gp.select([a, b], ['ie'], [-1, -1, -1, 100, 100, 100])
        self.assertEqual(taken, {0: [(0, 1, 2), (0, 2, 3)]})
        self.assertEqual(crossing_count, 1)
        # a component joined through welded positions in another shape of the same node is one piece
        c = shape('ie', [(10, 0, 10), (200, 0, 10), (200, 0, 20)], [(0, 1, 2)])
        taken, crossing_count = gp.select([a, c], ['ie'], [-1, -1, -1, 100, 100, 100])
        self.assertNotIn(0, taken)
        self.assertEqual(crossing_count, 2)
        with self.assertRaises(gp.BuildError):
            gp.select([a], ['missing'], [-1, -1, -1, 100, 100, 100])


class Collision(unittest.TestCase):
    def test_groups_volume_and_fields(self):
        # room 44's origin; a floor (wood), a wall, a door polygon with an exit, a shadow floor, and a group not listed
        o = (-200000.0, 0.0, 300000.0)
        v = [(o[0] + x, y, o[2] + z) for x, y, z in [(0, 0, 0), (0, 0, 100), (100, 0, 0), (0, 100, 0), (100, 100, 0), (900, 0, 0), (900, 0, 100), (1000, 0, 0)]]
        floor_info = (0x07FFE0FF, (2 << 16) | 0xFF, 0xFFFFFFFF, 0)
        door_info = (0x07FFE0FF & ~(0x3F << 13) | (4 << 13), 0xFF, 0xFFFFFFFF, 0)
        roof_info = (0x07FFE0FF, (16 << 16) | (1 << 12) | 0xFF, 0xFFFFFFFF, 0)
        tris = [(0, 1, 2, 0, 0), (0, 2, 3, 1, 1), (2, 4, 3, 1, 2), (0, 1, 2, 2, 3), (5, 6, 7, 0, 4)]
        col = dzb(v, tris, ['house', 'house_KIRI', 'house_KAGE', 'house_yane', 'house'], [floor_info, door_info, roof_info])
        piece = {'volume': [-10, -10, -10, 500, 500, 500], 'collision': {'groups': ['house', 'house_KIRI', 'house_KAGE']}}
        body, info, count = gp.piece_collision(col, piece, o, (50, 0, 50))
        self.assertEqual(count, 3)  # the floor, the door polygon (a wall, no exit) and the shadow group's wall; not the far triangle, not the unlisted group
        n_vertices, n_tris = struct.unpack_from('>II', body, 8)
        self.assertEqual(n_tris, 3)
        records = [struct.unpack_from('>4H', body, 40 + 12 * n_vertices + 8 * i) for i in range(n_tris)]
        surfaces = sorted(r[3] for r in records)
        self.assertEqual(surfaces, sorted([collision_mesh.SEMANTICS['col_wood'], collision_mesh.SEMANTICS['col_wall'], collision_mesh.SEMANTICS['col_wall']]))
        # positions are relative to the piece's origin
        first = struct.unpack_from('>3f', body, 40)
        self.assertEqual(first, (-50.0, 0.0, -50.0))
        self.assertEqual(info['collision_triangles'], 3)
        self.assertEqual(gp.piece_collision(col, {'volume': piece['volume'], 'collision': {'groups': []}}, o, (0, 0, 0))[2], 0)

    def test_surfaces_follow_the_game_classes(self):
        # metal is the game's metal class (raw 17, 20, 24), never raw 9 (damage); the wood class has several codes
        self.assertEqual({c for c, s in gp.SURFACES.items() if s == 'col_metal'}, {17, 20, 24})
        self.assertNotIn(9, gp.SURFACES)
        self.assertEqual({c for c, s in gp.SURFACES.items() if s == 'col_wood'}, {2, 12, 13, 14, 16, 26})
        self.assertEqual((gp.SURFACES[3], gp.SURFACES[4], gp.SURFACES[11]), ('col_stone', 'col_grass', 'col_sand'))
        o = (0.0, 0.0, 0.0)
        v = [(0, 0, 0), (0, 0, 100), (100, 0, 0)]
        for code, want in ((20, 'col_metal'), (9, 'col_stone'), (16, 'col_wood')):
            col = dzb(v, [(0, 1, 2, 0, 0)], ['g'], [(0x07FFE0FF, (code << 16) | 0xFF, 0xFFFFFFFF, 0)])
            body, _, _ = gp.piece_collision(col, {'volume': [-1, -1, -1, 200, 200, 200], 'collision': {'groups': ['g']}}, o, o)
            self.assertEqual(struct.unpack_from('>4H', body, 40 + 12 * 3)[3], collision_mesh.SEMANTICS[want])

    def test_island_rules_void_exits_water(self):
        # a hole's floor with an exit (void group) becomes a void, a wall of it stays a wall; another exit triangle
        # is left out when the recipe strips exits; a water group's triangles become swimmable water
        v = [(0, 0, 0), (0, 0, 100), (100, 0, 0), (0, 100, 0), (500, 0, 0), (500, 0, 100), (600, 0, 0), (900, 50, 0), (900, 50, 100), (1000, 50, 0)]
        exit_floor = (0x07FFE0FF & ~(0x3F << 13) | (0 << 13), (1 << 16) | (5 << 21) | 0xFF, 0xFFFFFFFF, 0)
        plain = (0x07FFE0FF, (3 << 16) | 0xFF, 0xFFFFFFFF, 0)
        tris = [(0, 1, 2, 0, 0), (0, 2, 3, 1, 1), (4, 5, 6, 0, 2), (7, 8, 9, 1, 3)]
        names = ['Hole_soko', 'Hole_kabe', 'door', 'pond']
        col = gp.read_dzb(dzb_groups(v, tris, names, [0, 0, 0, 0x100], [exit_floor, plain]))
        piece = {'volume': [-10, -10, -10, 1100, 500, 500], 'collision': {'groups': names, 'void': ['Hole_soko', 'Hole_kabe'], 'strip_exits': True}}
        body, info, count = gp.piece_collision(col, piece, (0, 0, 0), (0, 0, 0))
        nv, nt = struct.unpack_from('>II', body, 8)
        got = sorted(struct.unpack_from('>4H', body, 40 + 12 * nv + 8 * i)[3] for i in range(nt))
        sem = collision_mesh.SEMANTICS
        self.assertEqual(got, sorted([sem['col_void'], sem['col_wall'], sem['col_water']]))  # the door's exit floor is left out
        self.assertEqual(gp.distant_identity('gp' + 'a' * 29, 3), 'gd' + 'a' * 27 + '03')
        recipe = {'game_pieces_format': 1, 'set': 'isle', 'source': {'archive': 'sea_Room5.szs', 'room': 5},
                  'pieces': [{'id': 'isle', 'volume': [-10, -10, -10, 10, 10, 10], 'origin_y': 0, 'parts': [{'model': 0, 'nodes': ['a'], 'light': 'bg0'}],
                              'collision': {'groups': ['a'], 'void': ['a'], 'strip_exits': True}, 'distant': {'archive': 'sea_LOD05.szs', 'triangles': 120},
                              'expect': {'triangles': [1], 'collision_triangles': 1}}]}
        gp.check_recipe(recipe)
        for change in (lambda r: r['pieces'][0]['distant'].update(archive='sea_LOD06.szs'), lambda r: r['pieces'][0]['distant'].update(triangles=2049),
                       lambda r: r['pieces'][0]['collision'].update(void=['b']), lambda r: r['pieces'][0].update(origin_y=50),
                       lambda r: r['pieces'][0]['collision'].update(strip_exits='yes')):
            bad = json.loads(json.dumps(recipe))
            change(bad)
            with self.assertRaises(gp.BuildError):
                gp.check_recipe(bad)
        # an island's water: a second part keeps its texture animation; a first part never does
        water = json.loads(json.dumps(recipe))
        water['pieces'][0]['parts'].append({'model': 1, 'nodes': ['w'], 'light': 'bg1', 'animated': True})
        water['pieces'][0]['expect']['triangles'] = [1, 1]
        gp.check_recipe(water)
        for change in (lambda r: r['pieces'][0]['parts'][0].update(animated=True), lambda r: r['pieces'][0]['parts'][1].update(animated='yes')):
            bad = json.loads(json.dumps(water))
            change(bad)
            with self.assertRaises(gp.BuildError):
                gp.check_recipe(bad)


@unittest.skipUnless(os.environ.get('WWHD_GAME_DIR'), 'set WWHD_GAME_DIR to build the recipes from a game folder')
class FromGame(unittest.TestCase):
    def test_build_every_recipe(self):
        game = os.environ['WWHD_GAME_DIR']
        with tempfile.TemporaryDirectory() as d:
            for name, r in recipes():
                path = os.path.join(RECIPES, name)
                meta = gp.build(game, path, 'gp' + '0' * 29, os.path.join(d, r['set'] + '.szs'))
                self.assertEqual(len(meta['pieces']), len(r['pieces']))
                self.assertLessEqual(meta['archive_bytes'], r['memory']['archive_bytes'])
                again = gp.build(game, path, 'gp' + '0' * 29, os.path.join(d, r['set'] + '.szs'))
                self.assertEqual(again['archive_sha256'], meta['archive_sha256'])
                for piece, recipe_piece in zip(meta['pieces'], r['pieces']):
                    # an animated part (an island's water) brings its texture animation, named as its model
                    for part, recipe_part in zip(piece['parts'], recipe_piece['parts']):
                        self.assertEqual(part.get('animation'), part['model'] if recipe_part.get('animated') else None)
                    if 'distant' not in recipe_piece:
                        self.assertNotIn('distant', piece)
                        continue
                    # an island's distant view: its own small archive next to the set's, the recipe's triangle count
                    dd = piece['distant']
                    self.assertTrue(dd['archive'].startswith('gd') and len(dd['archive']) == 31)
                    self.assertEqual(dd['triangles'], recipe_piece['distant']['triangles'])
                    with open(os.path.join(d, dd['file']), 'rb') as f:
                        body = f.read()
                    self.assertEqual(body[:4], b'Yaz0')
                    self.assertLess(len(body), 256 * 1024)
                    self.assertEqual(hashlib.sha256(body).hexdigest(), dd['archive_sha256'])
                    # the island's origin is its sea level; its distant view stands in the same place
                    self.assertEqual(piece['origin'][1], 0)
                    for k in range(3):
                        self.assertLess(abs(dd['minimum'][k] - piece['minimum'][k]), 600)
                        self.assertLess(abs(dd['maximum'][k] - piece['maximum'][k]), 600)
                    # holes lead nowhere: their floors are drops, no exit is kept
                    if recipe_piece['collision'].get('void'):
                        self.assertGreater(piece['collision']['collision_triangles'], 0)


if __name__ == '__main__':
    unittest.main()
