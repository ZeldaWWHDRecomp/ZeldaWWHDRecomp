#!/usr/bin/env python3
"""tools/story_mods/game_objects.py on a synthetic game folder (no game data): the recipe checks, which archives
the game has (a pack's header, content/Common/Object), the game's own placements of each variant (a stage
archive's actor list), the --list and --json output and its exit code."""
import json, os, struct, subprocess, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import game_objects as go  # noqa: E402


def sarc(files):
    """a big-endian SARC of {name: bytes}"""
    names = b''
    nodes = []
    data = b''
    for name, payload in files.items():
        off = len(names)
        names += name.encode() + b'\0'
        names += b'\0' * ((-len(names)) % 4)
        nodes.append((0x01000000 | (off // 4), len(data), len(data) + len(payload)))
        data += payload + b'\0' * ((-len(payload)) % 8)
    sfat = b'SFAT' + struct.pack('>HHI', 0xC, len(nodes), 0x65) + b''.join(struct.pack('>IIII', 0, a, s, e) for a, s, e in nodes)
    sfnt = b'SFNT' + struct.pack('>HH', 8, 0) + names
    head_len = 0x14 + len(sfat) + len(sfnt)
    pad = (-head_len) % 0x10
    doff = head_len + pad
    return b'SARC' + struct.pack('>HHIIHH', 0x14, 0xFEFF, doff + len(data), doff, 0x100, 0) + sfat + sfnt + b'\0' * pad + data


def dzr(actors):
    """a room actor list (ACTR chunk) of (name, params)"""
    body = b''.join(name.encode().ljust(8, b'\0') + struct.pack('>I', params) + b'\0' * 20 for name, params in actors)
    return struct.pack('>I', 1) + b'ACTR' + struct.pack('>II', len(actors), 16) + body


class GameObjectsTool(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = os.path.join(self.tmp.name, 'content', 'Common')
        for d in ('Pack', 'Object', 'Stage'):
            os.makedirs(os.path.join(base, d))
        with open(os.path.join(base, 'Pack', 'first_szs_permanent.pack'), 'wb') as f:
            f.write(sarc({'Oyashi.szs': b'Yaz0', 'Kn.szs': b'Yaz0'}))
        with open(os.path.join(base, 'Object', 'Table.szs'), 'wb') as f:
            f.write(b'Yaz0')
        with open(os.path.join(base, 'Stage', 'sea_Room11.szs'), 'wb') as f:
            f.write(sarc({'room.dzr': dzr([('Oyashi', 0xFFFFFFFF), ('Oyashi', 0xFFFFFFFF), ('Table', 0xFFFFFF01), ('Kn', 0)])}))
        self.game = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_port_recipes(self):
        entries = go.load_recipes(sorted(os.path.join(go.DEFAULT_RECIPES, f) for f in os.listdir(go.DEFAULT_RECIPES) if f.endswith('.recipe.json')))
        ids = [e['id'] for e in entries]
        for i in ('palm', 'tree-large', 'table'):
            self.assertIn(i, ids)
        self.assertEqual(len(ids), len(set(ids)))

    def test_recipe_refusals(self):
        bad = os.path.join(self.tmp.name, 'bad.recipe.json')
        for broken in ({'game_objects_format': 2, 'group': 'x', 'objects': []},
                       {'game_objects_format': 1, 'group': 'x', 'objects': [{'id': 'Bad', 'native': {'name': 'A', 'archive': 'A'}, 'variants': [{'params': '0x00000000'}]}]},
                       {'game_objects_format': 1, 'group': 'x', 'objects': [{'id': 'a', 'native': {'name': 'A/..', 'archive': 'A'}, 'variants': [{'params': '0x00000000'}]}]},
                       {'game_objects_format': 1, 'group': 'x', 'objects': [{'id': 'a', 'native': {'name': 'A', 'archive': 'A'}, 'variants': [{'params': '1'}]}]}):
            with open(bad, 'w') as f:
                json.dump(broken, f)
            with self.assertRaises(ValueError):
                go.load_recipes([bad])

    def test_archives_and_placements(self):
        index = go.archive_index(self.game)
        self.assertEqual(index['Oyashi'], 'Pack/first_szs_permanent.pack')
        self.assertEqual(index['Table'], 'Object')
        self.assertNotIn('Lwood', index)
        entries = go.load_recipes(sorted(os.path.join(go.DEFAULT_RECIPES, f) for f in os.listdir(go.DEFAULT_RECIPES) if f.endswith('.recipe.json')))
        found = go.build(self.game, entries, with_placements=True)
        self.assertTrue(found['palm']['available'])
        self.assertFalse(found['tree-large']['available'])
        self.assertEqual(found['palm']['placements'], {'palm': 2})
        self.assertEqual(found['table']['placements'], {'table': 0, 'stool': 1, 'stool-open': 0})

    def test_command_line(self):
        tool = os.path.join(HERE, 'game_objects.py')
        out = subprocess.run([sys.executable, '-I', tool, '--game', self.game, '--json'], capture_output=True, text=True)
        self.assertEqual(out.returncode, 2)  # not every object is in this game
        doc = json.loads(out.stdout.splitlines()[0])
        self.assertTrue(doc['ok'] and doc['objects']['palm']['available'] and not doc['objects']['tree-large']['available'])
        out = subprocess.run([sys.executable, '-I', tool, '--game', self.game, '--list'], capture_output=True, text=True)
        self.assertIn('not in your game files', out.stdout)
        self.assertIn('Palm tree', out.stdout)
        self.assertNotIn('Oyashi', out.stdout.replace('Pack/first_szs_permanent.pack', ''))  # native names stay in the recipes


if __name__ == '__main__':
    unittest.main()
