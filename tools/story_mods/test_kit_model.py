# SPDX-License-Identifier: MPL-2.0
"""Kit models (kit_model.py): the recipe rules, welding, the collision (its *_col meshes, surfaces, the body limit)
on synthetic data. With WWHD_GAME_DIR set to a game folder, it also builds a small model with materials of two of
a kit room's models (Forest Haven's ground and its falling water) and checks the set: two parts, each lit with
its kit model's background light set, the second with the kit room's texture animation, and the collision."""
import json
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import kit_model as km  # noqa: E402
import collision_mesh  # noqa: E402
import game_piece_model as gp  # noqa: E402
import notebook_model as base  # noqa: E402


def glb(meshes):
    """a GLB (metres) from [(node name, [(material, positions, triangles)])]: positions, normals, uvs, indices"""
    binary, views, accessors, out_meshes, nodes, materials = bytearray(), [], [], [], [], []

    def add(data, kind, count, component, target, bounds=None):
        while len(binary) % 4:
            binary.append(0)
        views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(data), 'target': target})
        binary.extend(data)
        a = {'bufferView': len(views) - 1, 'componentType': component, 'count': count, 'type': kind}
        if bounds:
            a['min'], a['max'] = bounds
        accessors.append(a)
        return len(accessors) - 1
    for name, prims in meshes:
        ps = []
        for material, pos, tris in prims:
            if material not in materials:
                materials.append(material)
            lo = [min(p[k] for p in pos) for k in range(3)]
            hi = [max(p[k] for p in pos) for k in range(3)]
            at = {'POSITION': add(b''.join(struct.pack('<3f', *p) for p in pos), 'VEC3', len(pos), 5126, 34962, (lo, hi)),
                  'NORMAL': add(struct.pack('<3f', 0, 1, 0) * len(pos), 'VEC3', len(pos), 5126, 34962),
                  'TEXCOORD_0': add(b''.join(struct.pack('<2f', p[0], p[2]) for p in pos), 'VEC2', len(pos), 5126, 34962)}
            idx = add(b''.join(struct.pack('<3I', *t) for t in tris), 'SCALAR', 3 * len(tris), 5125, 34963)
            ps.append({'attributes': at, 'indices': idx, 'material': materials.index(material)})
        out_meshes.append({'name': name, 'primitives': ps})
        nodes.append({'name': name, 'mesh': len(out_meshes) - 1})
    doc = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}], 'nodes': nodes, 'meshes': out_meshes,
           'materials': [{'name': m} for m in materials], 'accessors': accessors, 'bufferViews': views, 'buffers': [{'byteLength': len(binary)}]}
    text = json.dumps(doc).encode()
    text += b' ' * (-len(text) % 4)
    while len(binary) % 4:
        binary.append(0)
    body = struct.pack('<I4s', len(text), b'JSON') + text + struct.pack('<I4s', len(binary), b'BIN\0') + bytes(binary)
    return struct.pack('<4sII', b'glTF', 2, 12 + len(body)) + body


def quad(x0, z0, x1, z1, y=0.0):
    return [(x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1)], [(0, 2, 1), (0, 3, 2)]


def grid(n, size=1.0):
    pos = [(i * size, 0.0, j * size) for j in range(n + 1) for i in range(n + 1)]
    tris = []
    for j in range(n):
        for i in range(n):
            a = j * (n + 1) + i
            tris += [(a, a + n + 1, a + 1), (a + 1, a + n + 1, a + n + 2)]
    return pos, tris


def recipe(path, **extra):
    r = {'kit_model_format': 1, 'id': 'island', 'glb': path, 'kit': {'stage': 'sea', 'room': 41}}
    r.update(extra)
    return r


def with_texture(data, material, image_name='own', extras=None, size=(64, 8)):
    """the GLB `data` with an embedded PNG base colour texture on `material` (and optional material extras)"""
    import actor_model  # noqa: F401  (the PNG reader the converter uses)
    jlen = struct.unpack_from('<I', data, 12)[0]
    doc = json.loads(data[20:20 + jlen])
    binary = bytearray(data[28 + jlen:])
    w, h = size
    rows = b''.join(b'\0' + bytes([200, 40, 30, 255]) * w for _ in range(h))
    import zlib

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
    while len(binary) % 4:
        binary.append(0)
    doc['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(png)})
    binary.extend(png)
    while len(binary) % 4:
        binary.append(0)
    doc.setdefault('images', []).append({'name': image_name, 'mimeType': 'image/png', 'bufferView': len(doc['bufferViews']) - 1})
    doc.setdefault('textures', []).append({'source': len(doc['images']) - 1})
    doc['buffers'][0]['byteLength'] = len(binary)
    for m in doc['materials']:
        if m['name'] == material:
            m['pbrMetallicRoughness'] = {'baseColorTexture': {'index': len(doc['textures']) - 1}}
            if extras:
                m['extras'] = extras
    text = json.dumps(doc).encode()
    text += b' ' * (-len(text) % 4)
    body = struct.pack('<I4s', len(text), b'JSON') + text + struct.pack('<I4s', len(binary), b'BIN\0') + bytes(binary)
    return struct.pack('<4sII', b'glTF', 2, 12 + len(body)) + body


class Names:
    """kit_materials.Names without a game: a kit room with these materials"""
    def __init__(self, materials, mapping=None, default=None):
        import kit_materials
        self.materials, self.mapping, self.default = {m: None for m in materials}, dict(mapping or {}), default
        self.stage, self.room = 'sea', 13
        self.resolve = kit_materials.Names.resolve.__get__(self)
        self.unknown = kit_materials.Names.unknown.__get__(self)


class KitModelTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def write(self, name, data):
        path = os.path.join(self.dir.name, name)
        with open(path, 'wb') as f:
            f.write(data)
        return path

    def test_recipe_rules(self):
        path = self.write('m.glb', b'')
        km.check_recipe(recipe(path, materials={'grass': 'm_kusa_v'}, material='m_iwa_v'))
        bad = [recipe(path, kit_model_format=2), recipe('relative.glb'), recipe(path, id='Bad Id'), recipe(path, kit={'stage': 'sea'}),
               recipe(path, kit={'stage': 'toolongname', 'room': 1}), recipe(path, kit={'stage': 'sea', 'room': 64}),
               recipe(path, kit={'stage': 'sea', 'room': True}), recipe(path, materials={'a': 1}), recipe(path, extra=1),
               recipe(path, materials={str(i): 'm' for i in range(17)})]
        for r in bad:
            with self.assertRaises(km.BuildError, msg=str(r)):
                km.check_recipe(r)

    def test_weld_keeps_triangles(self):
        s = gp.Shape()
        s.records = [b'a', b'b', b'c', b'a', b'c', b'd']
        s.pos = [(0, 0, 0), (1, 0, 0), (0, 0, 1), (0, 0, 0), (0, 0, 1), (1, 0, 1)]
        s.tris = [(0, 1, 2), (3, 4, 5)]
        km.weld(s)
        self.assertEqual(s.records, [b'a', b'b', b'c', b'd'])
        self.assertEqual(s.tris, [(0, 1, 2), (0, 2, 3)])

    def test_collision_from_col_meshes(self):
        ground, ground_tris = quad(-2, -2, 2, 2)
        wall, wall_tris = [(-2, 0, 2), (2, 0, 2), (2, 2, 2), (-2, 2, 2)], [(0, 1, 2), (0, 2, 3)]
        path = self.write('m.glb', glb([('visible', [('grass', ground, ground_tris)]),
                                        ('island_col', [('col_grass', ground, ground_tris), ('col_stone', wall, wall_tris)])]))
        import stage_builder as sb
        body, info = km.kit_collision(sb.read_glb(path))
        mesh = struct.unpack_from('>4sIII', body, 0)
        self.assertEqual(mesh[:2], (b'WCOL', 1))
        self.assertEqual((mesh[2], mesh[3]), (6, 4))  # the shared edge's two corners welded
        self.assertEqual(info['collision_triangles'], 4)
        semantics = [struct.unpack_from('>4H', body, 40 + 12 * mesh[2] + 8 * i)[3] for i in range(4)]
        self.assertEqual(sorted(semantics), sorted([collision_mesh.SEMANTICS['col_grass']] * 2 + [collision_mesh.SEMANTICS['col_stone']] * 2))
        # an unknown collision material is refused
        bad = self.write('bad.glb', glb([('visible', [('grass', ground, ground_tris)]), ('island_col', [('col_magma', ground, ground_tris)])]))
        with self.assertRaises(km.BuildError):
            km.kit_collision(sb.read_glb(bad))

    def test_collision_body_limit(self):
        import stage_builder as sb
        pos, tris = grid(64)  # 8192 triangles: the limit
        path = self.write('ok.glb', glb([('visible', [('grass', *quad(0, 0, 1, 1))]), ('g_col', [('col_grass', pos, tris)])]))
        _, info = km.kit_collision(sb.read_glb(path))
        self.assertEqual(info['collision_triangles'], collision_mesh.MAX_TRIANGLES)
        pos, tris = grid(65)
        path = self.write('big.glb', glb([('visible', [('grass', *quad(0, 0, 1, 1))]), ('g_col', [('col_grass', pos, tris)])]))
        with self.assertRaisesRegex(km.BuildError, 'simpler'):
            km.kit_collision(sb.read_glb(path))

    def test_collision_extent_one_sea_square(self):
        # a kit model's collision may be as large as a world object's: one sea square (1 km, 100000 units)
        import stage_builder as sb
        self.assertEqual(km.MAX_EXTENT, collision_mesh.MAX_EXTENT)
        self.assertEqual(km.MAX_EXTENT, 100000)
        for half in (200, 500):  # a 400 m island (past the old 200 m) and a whole square
            path = self.write('wide%d.glb' % half, glb([('visible', [('grass', *quad(0, 0, 1, 1))]), ('g_col', [('col_grass', *quad(-half, -half, half, half))])]))
            _, info = km.kit_collision(sb.read_glb(path))
            self.assertEqual((info['collision_minimum'][0], info['collision_maximum'][0]), (-half * 100.0, half * 100.0))
        path = self.write('wider.glb', glb([('visible', [('grass', *quad(0, 0, 1, 1))]), ('g_col', [('col_grass', *quad(-500, -500, 500.01, 500))])]))
        with self.assertRaisesRegex(km.BuildError, 'extent'):
            km.kit_collision(sb.read_glb(path))

    def test_material_names(self):
        """texture-free materials are game materials by name (Blender's .001 copies too); an unknown name is refused with
        the kit room's names and where they come from; unnamed and col_* ones take the default"""
        import stage_builder as sb
        ground, tris = quad(-2, -2, 2, 2)
        names = Names(['Txe_gake_v', 'Txe_hai_v'], default='Txe_gake_v')
        path = self.write('m.glb', glb([('visible', [('Txe_hai_v', ground, tris), ('Txe_gake_v.001', ground, tris), ('', ground, tris),
                                                     ('rock', ground, tris)])]))
        prims = sb.read_glb(path)
        kinds = km.classify(prims[:3], km.glb_materials(path), names, {}, {})
        self.assertEqual(kinds, {'Txe_hai_v': ('kit', 'Txe_hai_v'), 'Txe_gake_v.001': ('kit', 'Txe_gake_v'), '': ('kit', 'Txe_gake_v')})
        with self.assertRaises(km.BuildError) as e:
            km.classify(prims, km.glb_materials(path), names, {}, {})
        for words in ('"rock"', 'It has: Txe_gake_v, Txe_hai_v', 'export_glb', 'list_kit_materials', 'world-objects.md#game-materials'):
            self.assertIn(words, str(e.exception))
        # a mapping names it; or the model's "material" takes every other texture-free material
        self.assertEqual(km.classify(prims, km.glb_materials(path), Names(['Txe_gake_v', 'Txe_hai_v'], {'rock': 'Txe_hai_v'}, 'Txe_gake_v'), {}, {})['rock'],
                         ('kit', 'Txe_hai_v'))
        self.assertEqual(km.classify(prims, km.glb_materials(path), names, {'material': 'Txe_gake_v'}, {})['rock'], ('kit', 'Txe_gake_v'))

    def test_no_game_textures(self):
        """a material with a texture of its own is the model's own; one that is a game material may not carry a texture:
        named as one (also through the mapping), an export_glb material (its extras) renamed, or its image named as a game texture"""
        import stage_builder as sb
        ground, tris = quad(-2, -2, 2, 2)
        base_glb = glb([('visible', [('Txe_hai_v', ground, tris), ('cloth', ground, tris)])])
        names = Names(['Txe_gake_v', 'Txe_hai_v'], {'roof': 'Txe_gake_v'}, 'Txe_gake_v')
        ok = self.write('ok.glb', with_texture(base_glb, 'cloth'))
        kinds = km.classify(sb.read_glb(ok), km.glb_materials(ok), names, {}, {})
        self.assertEqual(kinds['cloth'][0], 'own')
        self.assertEqual(kinds['Txe_hai_v'], ('kit', 'Txe_hai_v'))
        cases = {'named': with_texture(base_glb, 'Txe_hai_v'),
                 'exported': with_texture(base_glb, 'cloth', extras={'wwhd': {'kit_material': 'Txe_hai_v', 'samplers': {}}}),
                 'image': with_texture(base_glb, 'cloth', image_name='Txe_Edragon_hai'),
                 'mapped': with_texture(glb([('visible', [('roof', ground, tris)])]), 'roof')}
        for label, data in cases.items():
            path = self.write(label + '.glb', data)
            with self.assertRaisesRegex(km.BuildError, 'never carries the game', msg=label):
                km.classify(sb.read_glb(path), km.glb_materials(path), names, {}, {'Txe_Edragon_hai': 0})

    def test_own_textures_budget(self):
        """the model's own textures: 1 MiB decoded together (game materials don't count), the PNG rules of mod models"""
        import stage_builder as sb
        ground, tris = quad(-2, -2, 2, 2)
        data = glb([('visible', [('a', ground, tris), ('b', ground, tris), ('Txe_hai_v', ground, tris)])])
        data = with_texture(with_texture(data, 'a', size=(512, 256)), 'b', image_name='b', size=(256, 512))
        path = self.write('two.glb', data)
        kinds = km.classify(sb.read_glb(path), km.glb_materials(path), Names(['Txe_hai_v']), {}, {})
        parts, total = km.own_parts(sb.read_glb(path), kinds)
        self.assertEqual((total, len(parts.materials), len(parts.parts)), (2 * 512 * 256 * 4, 2, 2))
        data = with_texture(data, 'Txe_hai_v', image_name='c', size=(64, 8))  # now a third own texture: over 1 MiB
        path = self.write('three.glb', data)
        kinds = km.classify(sb.read_glb(path), km.glb_materials(path), Names(['Txe_gake_v']), {}, {})
        with self.assertRaisesRegex(km.BuildError, '1 MiB'):
            km.own_parts(sb.read_glb(path), kinds)
        bad = self.write('odd.glb', with_texture(glb([('visible', [('a', ground, tris)])]), 'a', size=(60, 8)))
        kinds = km.classify(sb.read_glb(bad), km.glb_materials(bad), Names(['Txe_gake_v']), {}, {})
        with self.assertRaisesRegex(km.BuildError, 'multiples of 64'):
            km.own_parts(sb.read_glb(bad), kinds)

    def test_example_package_has_no_game_data(self):
        """the game-materials example package: no game archive or model in any file, no texture on a game material,
        only the roof's own picture, and the release guard passes on it"""
        import subprocess
        root = os.path.join(HERE, '..', '..', 'examples', 'guest-mods', 'world-objects-kit')
        manifest = json.load(open(os.path.join(root, 'manifest.json')))
        game_names = {'Txe_gake_v', 'Txe_hai_v', 'Txe_ita_v'}
        for dirpath, _, files in os.walk(root):
            for f in files:
                with open(os.path.join(dirpath, f), 'rb') as fh:
                    head = fh.read(4)
                self.assertNotIn(head, (b'Yaz0', b'SARC', b'FRES', b'Gfx2'), f)  # archives, models, textures of the game
        for model in manifest['models']:
            self.assertEqual(model['kind'], 'kit')
            materials = km.glb_materials(os.path.join(root, model['source']))
            for name, entry in materials.items():
                if name in game_names:
                    self.assertIsNone(entry['png'], name)
                self.assertFalse(entry['export'], name)
            own = [n for n, e in materials.items() if e['png'] is not None]
            self.assertEqual(own, ['cloth'] if model['id'] == 'lookout' else [])
        # the release guard on the package's files (its empty content/ folder left out: the guard refuses any folder of
        # that name, the name of a game file tree)
        import shutil
        copy = os.path.join(self.dir.name, 'package')
        shutil.copytree(root, copy, ignore=shutil.ignore_patterns('content', '__pycache__'))
        guard = os.path.join(HERE, '..', 'release', 'guard.py')
        result = subprocess.run([sys.executable, guard, copy], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_example_models_reproducible(self):
        import subprocess
        root = os.path.join(HERE, '..', '..', 'examples', 'guest-mods', 'world-objects-kit', 'assets')
        subprocess.run([sys.executable, os.path.join(root, 'make_models.py'), self.dir.name], check=True)
        for name in ('rock.glb', 'rock-far.glb', 'lookout.glb'):
            with open(os.path.join(root, name), 'rb') as a, open(os.path.join(self.dir.name, name), 'rb') as b:
                self.assertEqual(a.read(), b.read(), name)

    @unittest.skipUnless(os.environ.get('WWHD_GAME_DIR'), 'set WWHD_GAME_DIR to a game folder to build from its files')
    def test_dragon_roost_materials(self):
        """the example's models from Dragon Roost Island's materials: one part of game materials (rock, ash, planks) lit
        with the room's first background light set; the mixed lookout with its own-texture part first (light 0); the
        distant model's game textures at most 256 a side; a misspelt name, and a GLB made from export_glb, refused"""
        game = os.environ['WWHD_GAME_DIR']
        assets = os.path.join(HERE, '..', '..', 'examples', 'guest-mods', 'world-objects-kit', 'assets')
        built = {}
        for name, far in (('rock', False), ('lookout', False), ('rock-far', True)):
            r = {'kit_model_format': 1, 'id': name, 'glb': os.path.abspath(os.path.join(assets, name + '.glb')), 'kit': {'stage': 'sea', 'room': 13}}
            if far:
                r['far'] = True
            rp = self.write(name + '.json', json.dumps(r).encode())
            built[name] = km.build(game, rp, 'gk' + ('%029x' % len(built)), os.path.join(self.dir.name, 'cache', name + '.szs'))
        rock = built['rock']['pieces'][0]
        self.assertEqual([(p['light'], p['kind'], sorted(p['materials'])) for p in rock['parts']], [(1, 'kit', ['Txe_gake_v', 'Txe_hai_v', 'Txe_ita_v'])])
        self.assertEqual(built['rock']['own_texture_bytes'], 0)
        self.assertLessEqual(built['rock']['archive_bytes'], 12 * 1024 * 1024)
        look = built['lookout']['pieces'][0]['parts']
        self.assertEqual([(p['light'], p['kind']) for p in look], [(0, 'own'), (1, 'kit')])
        self.assertEqual(look[0]['materials'], ['cloth'])
        self.assertEqual(built['lookout']['own_texture_bytes'], 128 * 128 * 4)
        self.assertLess(built['rock-far']['texture_bytes'], built['rock']['texture_bytes'] // 8)
        # a misspelt game material: the room's names in the message
        doc = open(os.path.join(assets, 'rock.glb'), 'rb').read().replace(b'"Txe_hai_v"', b'"Txe_hia_v"')
        r = {'kit_model_format': 1, 'id': 'typo', 'glb': self.write('typo.glb', doc), 'kit': {'stage': 'sea', 'room': 13}}
        with self.assertRaisesRegex(km.BuildError, '"Txe_hia_v" is not a material of the kit room \\(sea room 13\\). It has: .*Txe_hai_v'):
            km.build(game, self.write('typo.json', json.dumps(r).encode()), 'gk' + 'f' * 29, os.path.join(self.dir.name, 'cache', 'typo.szs'))
        # a model made in Blender from the editor's export of Dragon Roost carries the game's textures: refused
        import export_glb
        body, _ = export_glb.export_room(game, 'sea', 13, max_texture=64)
        r = {'kit_model_format': 1, 'id': 'export', 'glb': self.write('export.glb', body), 'kit': {'stage': 'sea', 'room': 13}}
        with self.assertRaisesRegex(km.BuildError, 'never carries the game'):
            km.build(game, self.write('export.json', json.dumps(r).encode()), 'gk' + 'e' * 29, os.path.join(self.dir.name, 'cache', 'export.szs'))

    @unittest.skipUnless(os.environ.get('WWHD_GAME_DIR'), 'set WWHD_GAME_DIR to a game folder to build from its files')
    def test_build_from_the_game(self):
        game = os.environ['WWHD_GAME_DIR']
        ground, ground_tris = grid(4, 2.0)
        fall, fall_tris = [(0, 0, 0), (2, 0, 0), (2, -6, 0), (0, -6, 0)], [(0, 1, 2), (0, 2, 3)]
        path = self.write('island.glb', glb([('island', [('grass', ground, ground_tris), ('waterfall', fall, fall_tris)]),
                                             ('island_col', [('col_grass', ground, ground_tris)])]))
        r = recipe(path, materials={'grass': 'm_kusa_v', 'waterfall': 'SC_00_m_v_x'})
        rp = self.write('r.json', json.dumps(r).encode())
        out = os.path.join(self.dir.name, 'cache', 'gk' + '0' * 29 + '.szs')
        meta = km.build(game, rp, 'gk' + '0' * 29, out)
        piece = meta['pieces'][0]
        self.assertEqual(meta['set'], 'kit:island')
        self.assertEqual([p['light'] for p in piece['parts']], [1, 2])          # model: BG0, model1: BG1
        self.assertEqual(piece['parts'][1].get('animation'), piece['parts'][1]['model'])  # the falling water moves
        self.assertEqual([p['triangles'] for p in piece['parts']], [32, 2])
        self.assertEqual(piece['collision']['collision_triangles'], 32)
        self.assertTrue(os.path.isfile(os.path.join(os.path.dirname(out), piece['collision_file'])))
        with open(out, 'rb') as f:
            members = dict(base.shaderprep.sarc(base.shaderprep.yaz0(f.read())))
        self.assertEqual(sorted(members), sorted([meta['identity'] + '.bfres'] + [m + '.sharcfb' for m in meta['models']]))
        # a second build of the same input is the kept one
        self.assertEqual(km.build(game, rp, 'gk' + '0' * 29, out)['archive_sha256'], meta['archive_sha256'])
        # a material the kit room doesn't have is refused with the kit room's names
        r['materials']['grass'] = 'no_such_material'
        rp = self.write('r2.json', json.dumps(r).encode())
        with self.assertRaisesRegex(km.BuildError, 'It has: .*m_kusa_v'):
            km.build(game, rp, 'gk' + '1' * 29, out + '2')


if __name__ == '__main__':
    unittest.main()
