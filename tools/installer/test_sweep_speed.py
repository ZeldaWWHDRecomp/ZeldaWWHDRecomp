"""The models step's speed settings: actor groups split after a stop, several processes and the merge of what they
recorded (synthetic files of the runtime's formats; no game data)."""
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zlib

import model_sweep
import sweep_merge

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import shaderprep_format  # noqa: E402  (the shader prep's own reader of recipe databases)


def recipe_words(key_byte, shaders, groups):
    """A version 2 recipe record (gfx/vulkan/pipeline_recipe.cpp Layout::Indexed) with a valid pipeline state."""
    key = [key_byte * 0x01010101] * 52
    words = key + [0]
    for index in shaders:
        words += [index, 0]                       # shader index, no descriptors
    words += [0, 0, 0] + [0]                      # rendering, no formats
    words += [0] + [0]                            # bindings, attributes
    state = {'assembly': [0, 3, 0], 'viewport': [0, 1, 1], 'raster': [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x3f800000],
             'multisample': [0, 1, 0, 0, 0, 0], 'depth': [0, 1, 1, 1, 0, 0], 'front_stencil': [0] * 7,
             'back_stencil': [0] * 7, 'depth_bounds': [0, 0], 'blend': [0, 0, 0], 'blend_constants': [0] * 4}
    for name in shaderprep_format.VK_GROUPS:
        words += state[name]
    words += [0] + [4, 0, 1, 4, 8]                # attachments, dynamics
    return words + [len(groups)] + list(groups)


def spirv_module(seed):
    words = [0x07230203, 0x00010300, 0, 8, 0, (1 << 16) | 17, (1 << 16) | seed, (1 << 16) | 3]
    data = struct.pack('<%dI' % len(words), *words)
    packed = zlib.compress(data, 1)
    return struct.pack('<II', len(words), len(packed)) + packed + b'\0' * (-len(packed) % 4)


def shader_entry(seed):
    """A shader table entry: words, packed size, digest, zlib bytes padded to 4."""
    words = [0x07230203, 0x00010300, 0, 8, 0, (1 << 16) | 17, (1 << 16) | seed]
    h = 1469598103934665603
    for w in words:
        h = ((h ^ w) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    packed = zlib.compress(struct.pack('<%dI' % len(words), *words), 1)
    return struct.pack('<IIII', len(words), len(packed), h & 0xFFFFFFFF, h >> 32) + packed + b'\0' * (-len(packed) % 4)


def database(entries, records, fingerprint=b'F' * 48):
    body = struct.pack('<I', len(entries)) + b''.join(entries)
    for words in records:
        body += struct.pack('<I', 4 * len(words)) + struct.pack('<%dI' % len(words), *words)
    return b'WWVKPR02' + fingerprint + struct.pack('<II', len(records), zlib.crc32(body)) + body


def spirv_cache(modules, records, fingerprint=7):
    body = struct.pack('<Q', len(modules)) + b''.join(modules)
    for key, index in records:
        body += struct.pack('<QIIQQII', key, 0, 100, key, key, 0, index)
    return (b'WWVKSC04' + struct.pack('<QQQQ', fingerprint, len(body), sweep_merge._fnv(body), len(records)) + body)


def metal_record(text):
    packed = zlib.compress(text.encode())
    return struct.pack('<III', 2, len(text), len(packed)) + packed


class MergeTests(unittest.TestCase):
    def test_recipes_union_with_groups_and_one_shader_table(self):
        a, b, c = shader_entry(1), shader_entry(2), shader_entry(3)
        base = database([a, b], [recipe_words(1, [0, 1], []), recipe_words(2, [1, 0], [11])])
        # a part: the base's recipes (one filed under one more archive), one new recipe with a new shader
        part1 = database([b, a, c], [recipe_words(1, [1, 0], []), recipe_words(2, [0, 1], [11, 12]),
                                     recipe_words(3, [2, 1], [13])])
        part2 = database([a, b], [recipe_words(1, [0, 1], []), recipe_words(2, [1, 0], [14]), recipe_words(4, [0, 0], [])])
        other = database([a], [recipe_words(9, [0, 0], [])], fingerprint=b'G' * 48)  # another device: left out
        merged = sweep_merge.merge_recipes(base, [part1, part2, other])
        header, table, records = sweep_merge.read_recipes(merged)
        self.assertEqual(table, [a, b, c])
        self.assertEqual([(r[0][0], [table[i] for i in r[2]], r[3]) for r in records],
                         [(1, [a, b], []), (2, [b, a], [11, 12, 14]), (3, [c, a], [13]), (4, [a, a], [])])
        # the shader prep's own reader takes it (checksum, layout, pipeline state)
        self.assertEqual(len(shaderprep_format.vulkan_database(merged)), 4)

    def test_recipe_groups_stop_at_the_runtimes_limit(self):
        a = shader_entry(1)
        base = database([a], [recipe_words(1, [0, 0], list(range(1, 8)))])
        part = database([a], [recipe_words(1, [0, 0], [20, 21])])
        _, _, records = sweep_merge.read_recipes(sweep_merge.merge_recipes(base, [part]))
        self.assertEqual(records[0][3], list(range(1, 8)) + [20])

    def test_spirv_records_by_key_with_shared_modules(self):
        m1, m2, m3 = spirv_module(1), spirv_module(2), spirv_module(3)
        base = spirv_cache([m1], [(10, 0)])
        part = spirv_cache([m2, m1, m3], [(10, 1), (11, 0), (12, 2), (13, 1)])
        merged = sweep_merge.merge_spirv(base, [part, spirv_cache([m3], [(99, 0)], fingerprint=8)])
        fingerprint, modules, records = sweep_merge.read_spirv(merged)
        self.assertEqual(fingerprint, 7)
        self.assertEqual(modules, [m1, m2, m3])
        self.assertEqual([(k, i) for k, _, i in records], [(10, 0), (11, 1), (12, 2), (13, 0)])
        length, checksum = struct.unpack_from('<QQ', merged, 16)
        self.assertEqual((length, checksum), (len(merged) - 40, sweep_merge._fnv(merged[40:])))

    def test_metal_file_gets_each_parts_new_records_once(self):
        base = metal_record('a') + metal_record('b')
        part1 = base + metal_record('c') + metal_record('d')
        part2 = base + metal_record('d') + metal_record('e') + metal_record('f')[:5]  # a torn last record
        merged = sweep_merge.merge_metal(base, [part1, part2])
        self.assertEqual(merged, base + metal_record('c') + metal_record('d') + metal_record('e'))

    def test_placements_as_one_process_reads_them(self):
        with tempfile.TemporaryDirectory() as folder:
            p0, p1 = Path(folder, '0'), Path(folder, '1')
            # entry 0 and 2 read by part 0, entry 1 by part 1; "Kb" placed six times: the first four in reading order
            p0.write_text('0 Kb 1 0 0\n0 Kb 2 0 0\n0 Sp 1 0 0\n2 Kb 5 0 0\n2 Kb 6 0 0\n2 Sp 1 0 0\n')
            p1.write_text('1 Kb 3 0 0\n1 Kb 2 0 0\n1 Kb 4 0 0\n')
            self.assertEqual(sweep_merge.merge_placements([p0, p1, Path(folder, 'missing')]),
                             ['Kb 1 0 0', 'Kb 2 0 0', 'Sp 1 0 0', 'Kb 3 0 0', 'Kb 4 0 0'])
            p0.write_text('0 spawn sea 1 0\n1 spawn sea 1 3\n')
            p1.write_text('1 spawn sea 2 0\n')
            self.assertEqual(sweep_merge.merge_places([p0, p1]), ['spawn sea 1 0', 'spawn sea 1 3', 'spawn sea 2 0'])


class GroupGame:
    """A game that shows the actors in groups as dsweep.cpp does (actor-groups.txt caps) and stops at a group holding
    one of the stoppers."""

    def __init__(self, actors, stoppers, size):
        self.actors, self.stoppers, self.size, self.calls = actors, stoppers, size, 0

    def __call__(self, command, env, stdout, stderr, cwd):
        self.calls += 1
        state = Path(env['WWHD_DSWEEP_STATE'])
        lines = (state / 'progress.txt').read_text().splitlines() if (state / 'progress.txt').exists() else []
        done = {l[11:] for l in lines if l.startswith('actor-done ')}
        skip = set((state / 'skip.txt').read_text().split()) if (state / 'skip.txt').exists() else set()
        caps = {}
        if (state / 'actor-groups.txt').exists():
            for line in (state / 'actor-groups.txt').read_text().splitlines():
                cap, name = line.split(' ', 1)
                caps[name] = min(int(cap), caps.get(name, 99))
        todo = [a for a in self.actors if a not in done and a not in skip]
        with (state / 'progress.txt').open('a') as out:
            out.write('start\n')
            while todo:
                cap = caps.get(todo[0], self.size)
                group = []
                while todo and len(group) < cap and caps.get(todo[0], self.size) == cap:
                    group.append(todo.pop(0))
                out.write('actor-group %d\n' % len(group) + ''.join('actor %s\n' % a for a in group))
                if any(a in self.stoppers for a in group):
                    return _Proc(134)
                out.write(''.join('actor-done %s\n' % a for a in group))
            out.write('sweep-done\n')
        return _Proc(0)


class _Proc:
    def __init__(self, code):
        self.returncode = code

    def poll(self):
        return self.returncode

    def terminate(self):
        pass

    def wait(self, timeout=None):
        return self.returncode

    def kill(self):
        pass


class SpeedTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.game = self.root / 'game'
        (self.game / 'content' / 'Common' / 'Object').mkdir(parents=True)
        self.cache = self.root / 'cache'

    def sweep(self, popen, **kwargs):
        return model_sweep.run('/synthetic/exe', self.game, self.cache, 'vulkan', {'game': 'g', 'preparation': 'p'},
                               {'WWHD_SHADER_CACHE': str(self.root / 'none.bin')}, popen=popen, sleep=lambda s: None,
                               **kwargs)

    def test_a_stopped_group_is_split_down_to_the_actor_that_stops_the_game(self):
        actors = ['A%02d:00000000:0:0' % i for i in range(20)]
        game = GroupGame(actors, {actors[5], actors[13]}, 8)
        self.assertEqual(self.sweep(game), 'done')
        state = model_sweep.state_dir(self.cache, 'vulkan')
        self.assertEqual((state / 'skip.txt').read_text().splitlines(), [actors[5], actors[13]])
        done = [l[11:] for l in (state / 'progress.txt').read_text().splitlines() if l.startswith('actor-done ')]
        self.assertEqual(sorted(done), sorted(set(actors) - {actors[5], actors[13]}))
        # 8 -> 4 -> 2 -> 1 for each of the two, then the last attempt
        self.assertEqual(game.calls, 9)

    def test_stopped_group_needs_more_than_one_actor_left(self):
        self.assertEqual(model_sweep.stopped_group(['start', 'actor-group 2', 'actor a', 'actor b']), ['a', 'b'])
        self.assertIsNone(model_sweep.stopped_group(['start', 'actor-group 1', 'actor a']))
        self.assertIsNone(model_sweep.stopped_group(['start', 'actor-group 2', 'actor a', 'actor b', 'actor-done a',
                                                     'actor-done b', 'visit x']))
        self.assertIsNone(model_sweep.stopped_group(['actor-group 2', 'actor a', 'actor b', 'start', 'model x']))

    def test_speed_settings_reach_the_game(self):
        seen = []

        def game(command, env, stdout, stderr, cwd):
            seen.append(env)
            Path(env['WWHD_DSWEEP_STATE'], 'progress.txt').write_text('start\nsweep-done\n')
            return _Proc(0)
        with mock.patch.dict(os.environ, {'WWHD_SETUP_MODEL_SWEEP_ACTOR_GROUP': '8',
                                          'WWHD_SETUP_MODEL_SWEEP_RECORD_ONLY': 'yes'}):
            self.assertEqual(self.sweep(game), 'done')
        self.assertEqual(seen[0]['WWHD_DSWEEP_ACTOR_GROUP'], '8')
        self.assertNotIn('WWHD_DSWEEP_VISIT_IDLE', seen[0])
        self.assertNotIn('WWHD_DSWEEP_RECORD_ONLY', seen[0])  # not a number: ignored

    def test_process_count(self):
        with mock.patch.dict(os.environ, {'WWHD_SETUP_MODEL_SWEEP_PROCESSES': ''}):
            self.assertEqual(model_sweep.processes(2), 1)  # one unless set
        with mock.patch.dict(os.environ, {'WWHD_SETUP_MODEL_SWEEP_PROCESSES': '9'}):
            self.assertEqual(model_sweep.processes(), model_sweep.MAX_PROCESSES)
        with mock.patch.dict(os.environ, {'WWHD_SETUP_MODEL_SWEEP_PROCESSES': 'auto'}), \
                mock.patch.object(os, 'cpu_count', return_value=16), \
                mock.patch.object(os, 'sysconf', side_effect=lambda n: 4096 if n == 'SC_PAGE_SIZE' else (64 << 30) // 4096):
            self.assertEqual(model_sweep.processes(1), 4)
            self.assertEqual(model_sweep.processes(2), 2)
        with mock.patch.dict(os.environ, {'WWHD_SETUP_MODEL_SWEEP_PROCESSES': 'auto'}), \
                mock.patch.object(os, 'cpu_count', return_value=8), \
                mock.patch.object(os, 'sysconf', side_effect=lambda n: 4096 if n == 'SC_PAGE_SIZE' else (8 << 30) // 4096):
            self.assertEqual(model_sweep.processes(1), 2)
        with mock.patch.dict(os.environ, {'WWHD_SETUP_MODEL_SWEEP_PROCESSES': 'auto', 'ANDROID_ROOT': '/system'}):
            self.assertEqual(model_sweep.processes(1), 1)

    def test_parts_split_the_work_and_merge_lists_caches_and_skips(self):
        live = self.root / 'live'
        (live / 'vulkan-shaders').mkdir(parents=True)
        base_metal = metal_record('base')
        (live / 'shaders.bin').write_bytes(base_metal)
        a = shader_entry(1)
        (live / 'vk-1.bin.recipes').write_bytes(database([a], [recipe_words(1, [0, 0], [])]))
        cache_env = {'WWHD_SHADER_CACHE': str(live / 'shaders.bin'), 'WWHD_VK_SHADER_CACHE': str(live / 'vulkan-shaders'),
                     'WWHD_VK_PIPELINE_CACHE_DIR': str(live)}
        objects = self.game / 'content' / 'Common' / 'Object'
        for name in ('Aa', 'Bb', 'Cc', 'Dd', 'Ee'):
            (objects / (name + '.szs')).write_bytes(b'Yaz0')
        calls = []

        def game(command, env, stdout, stderr, cwd):
            state = Path(env['WWHD_DSWEEP_STATE'])
            part, count = map(int, env['WWHD_DSWEEP_PART'].split('/'))
            calls.append((part, 'WWHD_DSWEEP_ACTORS' in env, env.get('WWHD_DSWEEP_FILE_SELECT'), env['WWHD_DSWEEP_VISIT']))
            self.assertTrue(env['WWHD_SHADER_CACHE'].startswith(str(state)))
            progress = state / 'progress.txt'
            lines = progress.read_text().splitlines() if progress.exists() else []
            out = ['start']
            if 'WWHD_DSWEEP_ACTORS' not in env:  # the archives, every count-th, and their placements in order
                names = Path(env['WWHD_DSWEEP_ARCHIVES']).read_text().split()
                for j, name in enumerate(names):
                    if j % count == part:
                        out.append('archive-done ' + name)
                        with (state / 'actors-indexed.txt').open('a') as f:
                            f.write('%d Kb %s 0 0\n%d Sp %d 0 0\n' % (j, name, j, j))
                if part == 1 and not (state / 'skip.txt').exists():  # one stop on the way: skipped, then resumed
                    progress.write_text('\n'.join(lines + out + ['actor-group 1', 'actor Zz:0:0:0']) + '\n')
                    return _Proc(134)
            else:
                actors = (state / 'actors.txt').read_text().splitlines()
                out += ['actor-done ' + a for j, a in enumerate(actors) if j % count == part]
                with open(env['WWHD_SHADER_CACHE'], 'ab') as f:
                    f.write(metal_record('part%d' % part))
                recipes = Path(env['WWHD_VK_PIPELINE_CACHE_DIR'], 'vk-1.bin.recipes')
                recipes.write_bytes(sweep_merge.merge_recipes(recipes.read_bytes(), [
                    database([shader_entry(2)], [recipe_words(10 + part, [0, 0], [part + 1])])]))
            progress.write_text('\n'.join(lines + out + ['sweep-done']) + '\n')
            return _Proc(0)
        result = model_sweep.run('/synthetic/exe', self.game, self.cache, 'vulkan', {'game': 'g', 'preparation': 'p'},
                                 cache_env, popen=game, sleep=lambda s: None, processes=2)
        self.assertEqual(result, 'done')
        state = model_sweep.state_dir(self.cache, 'vulkan')
        self.assertEqual((state / 'skip.txt').read_text().splitlines(), ['Zz:0:0:0'])
        self.assertIn('sweep-done', (state / 'progress.txt').read_text().splitlines())
        self.assertFalse((state / 'parts').exists())
        # stage one: archives only; stage two: actors and visits, the file select in part 0
        self.assertEqual(sorted(set(calls)), [(0, False, None, ''), (0, True, '1', str(state / 'visits.txt')),
                                              (1, False, None, ''), (1, True, None, str(state / 'visits.txt'))])
        merged = (live / 'shaders.bin').read_bytes()
        self.assertEqual(merged, base_metal + metal_record('part0') + metal_record('part1'))
        _, table, records = sweep_merge.read_recipes((live / 'vk-1.bin.recipes').read_bytes())
        self.assertEqual([(r[0][0], r[3]) for r in records], [(1, []), (10, [1]), (11, [2])])


if __name__ == '__main__':
    unittest.main()
