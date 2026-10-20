#!/usr/bin/env python3
"""The game objects catalogue on the player's machine: which catalogued objects the player's own game has.

The port keeps only a recipe per group (runtime/assets/game_objects/<group>.recipe.json): neutral ids, our
own labels, descriptions and categories, and references into the game (the stage actor name, its process,
the object archive it loads, the fixed parameter word of each variant). This tool reads the player's game
locally, read-only, and says for each entry whether its archive is there (in content/Common/Pack/*.pack
or content/Common/Object/<archive>.szs); with --placements it also counts how often the game itself
places that name with each variant's parameter word in its stages and rooms (the room actor lists, .dzr
and .dzs in the stage archives), as evidence that the recipe's references are the game's.
Nothing it prints or writes holds game bytes: names the recipes already have, counts and file locations.

usage: game_objects.py --game <game dir with content/> [--recipe file ...] [--list] [--placements] [--json]
"""
import argparse, glob, json, os, re, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import shaderprep  # noqa: E402  (Yaz0 and SARC readers)

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_RECIPES = os.path.join(HERE, '..', '..', 'runtime', 'assets', 'game_objects')
NEUTRAL = re.compile(r'^[a-z0-9]+(-[a-z0-9]+)*$')
NATIVE = re.compile(r'^[A-Za-z0-9_]{1,7}$')


def load_recipes(files):
    """the entries of the recipe files; ValueError for anything the port would refuse"""
    entries, seen = [], set()
    for f in files:
        with open(f, encoding='utf-8') as fh:
            r = json.load(fh)
        if r.get('game_objects_format') != 1:
            raise ValueError('%s: unsupported game_objects_format' % f)
        group = r.get('group', '')
        if not NEUTRAL.match(group):
            raise ValueError('%s: invalid group' % f)
        for o in r.get('objects', []):
            i = o.get('id', '')
            if not NEUTRAL.match(i) or len(i) > 32 or i in seen:
                raise ValueError('%s: invalid or duplicate id %r' % (f, i))
            seen.add(i)
            n = o.get('native', {})
            if not NATIVE.match(n.get('name', '')) or not NATIVE.match(n.get('archive', '')):
                raise ValueError('%s: invalid native reference' % i)
            variants = o.get('variants', [])
            if not variants or any(not re.match(r'^0x[0-9A-Fa-f]{8}$', v.get('params', '')) for v in variants):
                raise ValueError('%s: variants need params 0x and 8 hex digits' % i)
            entries.append(dict(o, group=group))
    return entries


def archive_index(game):
    """archive name (without .szs) -> where it is: 'Pack/<pack>' or 'Object'; reads only the packs' headers"""
    base = os.path.join(game, 'content', 'Common')
    out = {}
    for path in sorted(glob.glob(os.path.join(base, 'Pack', '*.pack'))):
        with open(path, 'rb') as fh:
            head = fh.read(1 << 20)
        for name in sarc_names(head):
            if name.endswith('.szs'):
                out.setdefault(name[:-4], 'Pack/' + os.path.basename(path))
    for path in sorted(glob.glob(os.path.join(base, 'Object', '*.szs'))):
        out.setdefault(os.path.basename(path)[:-4], 'Object')
    return out


def sarc_names(d):
    """the member names of a big-endian SARC from its header bytes"""
    if len(d) < 0x20 or d[:4] != b'SARC':
        return []
    be = lambda f, o: struct.unpack_from('>' + f, d, o)[0]
    sfat = be('H', 4)
    if d[sfat:sfat + 4] != b'SFAT':
        return []
    count = be('H', sfat + 6)
    nodes = sfat + be('H', sfat + 4)
    sfnt = nodes + 16 * count
    if d[sfnt:sfnt + 4] != b'SFNT':
        return []
    names = sfnt + be('H', sfnt + 4)
    out = []
    for i in range(count):
        attr = be('I', nodes + 16 * i + 4)
        if attr & 0x01000000:
            at = names + (attr & 0xFFFF) * 4
            end = d.find(b'\0', at)
            if end < 0:
                break
            out.append(d[at:end].decode(errors='replace'))
    return out


def stage_actor_lists(game):
    """yield the .dzr/.dzs payloads of every stage and room archive (the stage dir and the packs)"""
    base = os.path.join(game, 'content', 'Common')

    def walk(label, szs):
        for n2, d2 in shaderprep.walk(szs, label):
            if n2.endswith('.dzr') or n2.endswith('.dzs'):
                yield d2
            elif n2.endswith('.bfres'):
                yield from bfres_dz(d2)
    for f in sorted(glob.glob(os.path.join(base, 'Stage', '*.szs'))):
        with open(f, 'rb') as fh:
            yield from walk(os.path.basename(f), fh.read())
    for p in sorted(glob.glob(os.path.join(base, 'Pack', 'szs_permanent*.pack')) + glob.glob(os.path.join(base, 'Pack', 'first_szs_permanent.pack'))):
        with open(p, 'rb') as fh:
            for name, data in shaderprep.sarc(fh.read()):
                if '_Room' in name or '_Stage' in name or name.startswith('Stage'):
                    yield from walk(name, data)


def bfres_dz(f):
    """the .dzr/.dzs files a HD room BFRES embeds (external files group)"""
    s32 = lambda o: struct.unpack_from('>i', f, o)[0]
    g = 0x20 + 4 * 11
    if len(f) < g + 4 or s32(g) == 0:
        return
    go = g + s32(g)
    for i in range(1, s32(go + 4) + 1):
        e = go + 8 + 16 * i
        no = e + 8 + s32(e + 8)
        name = f[no:f.index(b'\0', no)].decode(errors='replace')
        do = e + 12 + s32(e + 12)
        off = do + s32(do)
        size = struct.unpack_from('>I', f, do + 4)[0]
        if name.endswith('.dzr') or name.endswith('.dzs'):
            yield f[off:off + size]


def placements(game, wanted):
    """(native name, params) -> how often the game's actor lists place it"""
    counts = {}
    for d in stage_actor_lists(game):
        if len(d) < 4:
            continue
        n = struct.unpack_from('>I', d, 0)[0]
        if n > 256 or 4 + 12 * n > len(d):
            continue
        for i in range(n):
            t, c, o = struct.unpack_from('>4sII', d, 4 + 12 * i)
            if not (t.startswith(b'ACT') or t in (b'TRES', b'SCOB')):
                continue
            size = 0x20 if not t.startswith(b'SCOB') else 0x24
            for k in range(c):
                e = o + size * k
                if e + 12 > len(d):
                    break
                name = d[e:e + 8].split(b'\0')[0].decode(errors='replace')
                if name in wanted:
                    key = (name, struct.unpack_from('>I', d, e + 8)[0])
                    counts[key] = counts.get(key, 0) + 1
    return counts


def build(game, entries, with_placements=False):
    index = archive_index(game)
    counts = placements(game, {e['native']['name'] for e in entries}) if with_placements else {}
    out = {}
    for e in entries:
        where = index.get(e['native']['archive'])
        missing = [a for a in e['native'].get('also_archives', []) if a not in index]
        item = {'available': where is not None and not missing}
        if missing:
            item['missing'] = missing
        if where:
            item['archive'] = where
        if with_placements:
            item['placements'] = {v['id']: counts.get((e['native']['name'], int(v['params'], 16)), 0) for v in e['variants']}
        out[e['id']] = item
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True)
    ap.add_argument('--recipe', action='append', help="recipe files (default: the port's)")
    ap.add_argument('--list', action='store_true', help='print id, label, category and whether the game has it')
    ap.add_argument('--placements', action='store_true', help="also count the game's own placements of each variant")
    ap.add_argument('--json', action='store_true', help='one JSON result line (for the port)')
    args = ap.parse_args()
    files = args.recipe or sorted(glob.glob(os.path.join(DEFAULT_RECIPES, '*.recipe.json')))
    try:
        entries = load_recipes(files)
        found = build(args.game, entries, args.placements)
    except (OSError, KeyError, struct.error, ValueError) as error:
        if args.json:
            print(json.dumps({'ok': False, 'error': 'game objects: ' + str(error)}))
            return 1
        raise
    if args.json:
        print(json.dumps({'ok': True, 'game_objects_local': 1, 'objects': found}))
    if args.list or not args.json:
        for e in entries:
            f = found[e['id']]
            line = '%-16s %-26s %-18s %s' % (e['id'], e['label'], e['category'], ('in ' + f['archive']) if f['available'] else 'not in your game files')
            if 'placements' in f:
                line += '  (the game places ' + ', '.join('%s %d' % (k, n) for k, n in f['placements'].items()) + ')'
            print(line)
    return 0 if all(f['available'] for f in found.values()) else 2


if __name__ == '__main__':
    sys.exit(main())
