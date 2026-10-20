#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Mod stages: a package's new stage, built on the player's machine from the player's own game files.

A mod stage is a stage of its own name (seven characters, given by the port) whose archives the game loads
through its own stage code: <name>_Stage.szs and one <name>_Room<n>.szs per room. This tool writes them
from a recipe (the port writes it from the package's manifest, docs/mod-stages.md) into the player's
stage cache; the port serves them to the game from there. Nothing from the game is in this file or in a
package: the stage's lighting, camera, door models and events, the rooms' materials, textures and shader
archives all come from the player's game files at build time.

  Stage archive: the template stage's archive (its Stage.bfres keeps the door and key models, the event
  list, the base light and any sky) with a new stage.dzs: the stage settings (STAG: save slot 15, which
  only the game's test stages use; the map off), the room table (RTBL: each room with itself and every
  room behind one of its doors, which the door event needs), the room offsets (MULT, all zero), the
  stage's exit list (SCLS: entry 0 is the game entrance the stage saves as), its doors (TGDR) and one
  environment entry per room (EnvR: the kit room's); the template's lighting and camera chunks are kept.

  Room archive (graft): the kit room's archive kept whole (its textures and its material and texture
  animations) with its models replaced by the package's ground model, drawn with the kit room's
  materials and shader archive, and with a new room.dzb (collision) and room.dzr (room data: the kit
  room's file info, layer banks and light vector; the spawns and the room's exit list).

  Collision (room.dzb): three group levels, as the game's rooms have them (the game takes a triangle's
  room from its group's grandparent): the room, its ground, and the triangles' groups (one for the
  ground, one for swimmable water with the game's water bit). An exit is a box: the floor triangles
  inside it take that exit (Link takes exits from the ground he stands on).

usage: stage_builder.py --game GAME --recipe RECIPE.json --out DIR      prints the metadata (one JSON line)

The recipe (format 1; positions in game units, angles in degrees):
  {"format": 1, "name": "Zab12cd", "template": "Cave01", "kind": "cave"|"dungeon",
   "entrance": {"stage": "sea", "room": 34, "point": 1},
   "rooms": [{"room": 0, "kit": 0, "ground": "/abs/room.glb", "material": "<kit material>",
              "materials": {"<glb material>": "<kit material>"},
              "spawns": [{"point": 0, "position": [x, y, z], "angle": 90, "start": "stand"}],
              "exits": [{"min": [x, y, z], "max": [x, y, z], "to": {"stage": "sea", "room": 34, "point": 1}}]}],
   "doors": [{"position": [x, y, z], "angle": 90, "rooms": [0, 1]}]}
"""
import argparse
import hashlib
import json
import math
import os
import re
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notebook_model as base  # noqa: E402  (Bfres, sarc, yaz0_stored, BuildError)
import game_piece_model as gp  # noqa: E402  (room_files, read_model, Writer, write_model, ATTRIBUTE_CLASS)
from skinned_actor_model import append_dictionary, append_string  # noqa: E402

VERSION = 2
FORMAT = 1
BuildError = base.BuildError
MAX_ROOMS = 16
MAX_SPAWNS = 8
MAX_EXITS = 8
MAX_DOORS = 16
MAX_TRIANGLES = 20000      # per room, collision and visible model each
MAX_VERTICES = 65535
MAX_GLB = 16 * 1024 * 1024
MAX_EXTENT = 100000.0      # game units from the room origin
SAVE_SLOT = 15             # the stage settings' save slot: only the game's test stages use it
KINDS = {'cave': 4, 'dungeon': 1}  # STAG stage type
STARTS = {'stand': 0x0, 'fall': 0xF}  # PLYR start mode (the player's parameter bits 12-15)
NAME = re.compile(r'Z[a-z0-9]{6}')
STAGE = re.compile(r'[A-Za-z0-9_]{1,7}')
# The surfaces (world-object collision material names, collision_mesh.SEMANTICS) and the attribute class the
# game gives each (story_world_collision_mesh.h NativeResultIds; the raw code is found in the game's conversion).
SURFACES = ('col_stone', 'col_grass', 'col_wood', 'col_sand', 'col_metal', 'col_wall', 'col_objects', 'col_climb',
            'col_hookshot', 'col_water', 'col_shallow', 'col_waterfall', 'col_ice', 'col_void')
SURFACE_CLASS = (3, 4, 2, 11, 20, 0, 0, 0, 2, 0, 19, 23, 15, 8)
WATER, VOID, CLIMB, OBJECTS, HOOKSHOT = 9, 13, 7, 6, 8
NO_EXIT = 0x3F
DOOR_MODEL = 'door10'
# the template's Stage.bfres models a door needs: the knob door, the boss door, the small-key lock, the stop bars
DOOR_MODELS = {'door10': ['door10'], 'door20': ['door20']}
LOCK_MODELS = {4: 'key10', 0: 'stop10'}
# stage.dzs chunks the builder writes itself, and template chunks it leaves out (actors, paths, treasure:
# they belong to the template's own rooms)
WRITTEN = {'STAG', 'RTBL', 'MULT', 'SCLS', 'TGDR', 'EnvR'}
DROPPED = {'TRES', 'ACTR', 'TGOB', 'SCOB', 'TGSC', 'DOOR', 'PLYR', 'RPAT', 'RPPN', 'PATH', 'PPNT', 'ACT0', 'ACT1', 'ACT2', 'ACT3',
           'ACT4', 'ACT5', 'ACT6', 'ACT7', 'ACT8', 'ACT9', 'ACTa', 'ACTb', 'TRE0', 'SCO0', 'Door', 'MECO', 'MEMA', 'SOND'}


def require(ok, message):
    if not ok:
        raise BuildError(message)


# ---- the dz chunk files (stage.dzs, room.dzr) -----------------------------------------------------------

def read_dz(d):
    """[(tag, count, bytes)] in table order; a chunk's bytes run to the next chunk (by offset)"""
    require(len(d) >= 4, 'chunk file too short')
    n, = struct.unpack_from('>I', d, 0)
    require(n <= 64 and 4 + 12 * n <= len(d), 'chunk table malformed')
    raw = [struct.unpack_from('>4sII', d, 4 + 12 * i) for i in range(n)]
    ends = sorted({o for _, _, o in raw} | {len(d)})
    out = []
    for tag, count, o in raw:
        require(o <= len(d), 'chunk outside the file')
        end = next(e for e in ends if e > o) if o < len(d) else o
        out.append((tag.decode('ascii', 'replace'), count, bytes(d[o:end]), o))
    return out


def write_dz(chunks):
    """chunks: [(tag, count, data)], data bytes or a function(offset) -> bytes for chunks holding file
    offsets (RTBL). 4-byte aligned, padded with 0xFF as the game's files are."""
    head = 4 + 12 * len(chunks)
    body, table = bytearray(), b''
    for tag, count, data in chunks:
        while (head + len(body)) % 4:
            body.append(0xFF)
        at = head + len(body)
        if callable(data):
            data = data(at)
        table += struct.pack('>4sII', tag.encode('ascii'), count, at)
        body += data
    return struct.pack('>I', len(chunks)) + table + bytes(body)


def chunk(chunks, tag):
    return next((c for c in chunks if c[0] == tag), None)


def scls_entry(stage, start, room, wipe=0):
    return struct.pack('>8sBbBB', stage.encode('ascii'), start & 0xFF, room, wipe, 0xFF)


def rtbl(rows):
    """rows: per room (0..n-1): (reverb, b2, b3, [entries]) -> a function(offset) -> chunk bytes"""
    def data(at):
        n = len(rows)
        row_at = at + 4 * n
        list_at = row_at + 8 * n
        out = bytearray()
        lists = bytearray()
        for k, (reverb, b2, b3, entries) in enumerate(rows):
            out += struct.pack('>I', row_at + 8 * k)
        for k, (reverb, b2, b3, entries) in enumerate(rows):
            out += struct.pack('>BBBBI', len(entries), reverb, b2, b3, list_at + len(lists))
            lists += bytes(entries)
        return bytes(out + lists)
    return data


def read_rtbl(dzs, chunk_offset, count):
    """the template's room table rows: [(reverb, b2, b3, entries)] (offsets in the file)"""
    rows = []
    for k in range(count):
        row, = struct.unpack_from('>I', dzs, chunk_offset + 4 * k)
        n, reverb, b2, b3, lst = struct.unpack_from('>BBBBI', dzs, row)
        rows.append((reverb, b2, b3, list(dzs[lst:lst + n])))
    return rows


def room_entries(room, neighbours):
    """a room's row: itself (drawn, 0xC0 as the game's rows have it) and the rooms behind its doors"""
    return [0xC0 | room] + sorted(n for n in set(neighbours) if n != room)


# ---- the stage file (stage.dzs) ----------------------------------------------------------------------------

def build_dzs(template_dzs, recipe):
    chunks = read_dz(template_dzs)
    stag = chunk(chunks, 'STAG')
    require(stag and stag[1] == 1 and len(stag[2]) >= 0x14, 'the template stage has no stage settings')
    s = bytearray(stag[2][:0x14])
    s[9] = (SAVE_SLOT << 1) | (1 if recipe.get('keys') else 0)  # the key counter
    mm, = struct.unpack_from('>H', s, 0xA)
    struct.pack_into('>H', s, 0xA, (mm & ~3) | (1 if recipe.get('map_mode') == 1 else 0))  # 0 the sea chart, 1 the dungeon view
    t, = struct.unpack_from('>I', s, 0xC)
    struct.pack_into('>I', s, 0xC, (t & ~(7 << 16)) | (KINDS[recipe['kind']] << 16))
    rooms = recipe['rooms']
    n = len(rooms)
    old_rtbl = chunk(chunks, 'RTBL')
    template_rows = read_rtbl(template_dzs, old_rtbl[3], old_rtbl[1]) if old_rtbl else []
    neighbours = {r['room']: [] for r in rooms}
    for a, b in [d['rooms'] for d in recipe.get('doors', [])] + [tuple(j) for j in recipe.get('joins', [])]:
        neighbours[a].append(b)
        neighbours[b].append(a)
    rows = []
    for r in rooms:
        kit = r['kit']['room'] if r['kit']['stage'] == recipe['template'] else -1
        reverb, b2, b3 = template_rows[kit][:3] if 0 <= kit < len(template_rows) else template_rows[0][:3] if template_rows else (0, 0, 0)
        rows.append((reverb, b2, b3, room_entries(r['room'], neighbours[r['room']])))
    mult = b''.join(struct.pack('>ffhBB', 0.0, 0.0, 0, r['room'], 0) for r in rooms)
    envr_old = chunk(chunks, 'EnvR')
    envr = b''
    for r in rooms:  # a kit room of another stage is lit by the template's first environment
        kit = r['kit']['room'] if r['kit']['stage'] == recipe['template'] else 0
        e = envr_old[2][8 * kit:8 * kit + 8] if envr_old and kit < envr_old[1] else b''
        envr += e if len(e) == 8 else bytes(8)
    e = recipe['entrance']
    scls = scls_entry(e['stage'], e['point'], e['room'])
    doors = b''
    for d in recipe.get('doors', []):
        a, b = d['rooms']
        x, y, z = d['position']
        doors += struct.pack('>8sI3fhhhH4B', d.get('name', DOOR_MODEL).encode(), d.get('params', 0x0FFFF0FF), x, y, z, a | (b << 6), angle16(d['angle']), -193, 0x00FF,
                             10, 10, 10, 0xFF)  # z -193 = 0xFF3F: no view room, as the game's knob doors
    out = [('STAG', 1, bytes(s)), ('RTBL', n, rtbl(rows)), ('MULT', n, mult), ('EnvR', n, envr), ('SCLS', 1, scls)]
    if doors:
        out.append(('TGDR', len(recipe['doors']), doors))
    for tag, count, data, _ in chunks:
        if tag in WRITTEN or tag in DROPPED:
            continue
        out.append((tag, count, data))
    return write_dz(out)


def angle16(degrees):
    v = int(round(degrees * 65536.0 / 360.0)) & 0xFFFF
    return v - 0x10000 if v >= 0x8000 else v


# ---- the room file (room.dzr) -------------------------------------------------------------------------------

def build_dzr(kit_dzr, room, recipe_room):
    kit = read_dz(kit_dzr)
    fili = chunk(kit, 'FILI')
    require(fili and fili[1] >= 1, 'the kit room has no file info (FILI)')
    out = [('FILI', 1, fili[2][:8])]
    lbnk = chunk(kit, 'LBNK')
    if lbnk:
        out.append(('LBNK', lbnk[1], lbnk[2][:lbnk[1]]))
    spawns = b''
    for s in recipe_room['spawns']:
        x, y, z = s['position']
        param = 0xFFFF0000 | (STARTS[s.get('start', 'stand')] << 12) | room
        spawns += struct.pack('>8sI3fhhBBH', b'Link', param, x, y, z, 0, angle16(s.get('angle', 0)), 0xFF, s['point'], 0xFFFF)
    out.append(('PLYR', len(recipe_room['spawns']), spawns))
    exits = recipe_room.get('exits', [])
    if exits:
        out.append(('SCLS', len(exits), b''.join(scls_entry(x['to']['stage'], x['to']['point'], x['to']['room'], x.get('wipe', 0)) for x in exits)))
    lgtv = chunk(kit, 'LGTV')
    if lgtv:
        out.append(('LGTV', lgtv[1], lgtv[2][:0x1C * lgtv[1]]))
    for tag in ('ACTR', 'TRES'):
        entries = [a for a in recipe_room.get('actors', []) if a['tag'] == tag]
        if entries:
            out.append((tag, len(entries), b''.join(actor_entry(a, room) for a in entries)))
    return write_dz(out)


def actor_entry(a, room):
    """an ACTR or TRES entry (0x20 bytes): name, parameters, position, rotation words; a chest's x word is its room"""
    x, y, z = a['position']
    ax = (a.get('angle_x', 0) & ~0x3F | room) if a['tag'] == 'TRES' else a.get('angle_x', 0)
    return struct.pack('>8sI3fHhHH', a['name'].encode('ascii'), int(a['params']) & 0xFFFFFFFF, x, y, z, ax & 0xFFFF, angle16(a.get('angle', 0)),
                       a.get('angle_z', 0xFF) & 0xFFFF, 0xFFFF)


# ---- collision (room.dzb) ------------------------------------------------------------------------------------

def surface_codes():
    """the raw attribute code per surface: the code that is its own class when there is one, else the first
    code of that class (as the runtime's native_attributes)"""
    conv = gp.ATTRIBUTE_CLASS
    out = []
    for want in SURFACE_CLASS:
        code = want if want < 32 and conv[want] == want else next((c for c in range(32) if conv[c] == want), -1)
        require(code >= 0, 'no attribute code for a surface')
        out.append(code)
    return out


def surface_info(surface, exit_id):
    """the four info words of a triangle: as the port's world-object collision writes them, with the exit"""
    code = surface_codes()[surface]
    w0 = (0x07FFE0FF & ~0x7E000) | ((exit_id & 0x3F) << 13)
    w1 = 0xFF | (0x100 if surface == CLIMB else 0) | (code << 16) | ((4 << 21) if surface == VOID else 0)
    w3 = 0xED if surface == OBJECTS else 0x10 if surface == HOOKSHOT else 0
    return (w0, w1, 0xFFFFFFFF, w3)


LEAF = 32


def build_dzb(vertices, triangles, room):
    """vertices [(x,y,z)], triangles [(a, b, c, surface, exit)] -> room.dzb. Groups: 0 the room (its number),
    1 the ground (child of 0), 2 the ground triangles (child of 1; group info: the room table row), 3 the
    swimmable water (a sibling of 2 with the game's water bit). Each triangle group has its own tree."""
    require(0 < len(triangles) <= MAX_TRIANGLES and len(vertices) <= MAX_VERTICES, 'collision exceeds %d triangles' % MAX_TRIANGLES)
    infos, info_of = [], {}
    tris = []
    for a, b, c, surface, exit_id in triangles:
        key = (surface, exit_id)
        if key not in info_of:
            info_of[key] = len(infos)
            infos.append(surface_info(surface, exit_id))
        tris.append((a, b, c, info_of[key], 3 if surface == WATER else 2))
    ordered = [t for t in tris if t[4] == 2] + [t for t in tris if t[4] == 3]
    normal = sum(1 for t in tris if t[4] == 2)
    nodes, blocks = [], []

    def split(first, end, parent):
        nid = len(nodes)
        nodes.append({'parent': parent, 'block': None, 'children': [0xFFFF] * 8})
        if end - first <= LEAF:
            nodes[nid]['block'] = len(blocks)
            blocks.append(first)
            return nid
        pts = [vertices[v] for t in ordered[first:end] for v in t[:3]]
        lo = [min(p[k] for p in pts) for k in range(3)]
        hi = [max(p[k] for p in pts) for k in range(3)]
        axis = max(range(3), key=lambda k: hi[k] - lo[k])
        ordered[first:end] = sorted(ordered[first:end], key=lambda t: sum(vertices[v][axis] for v in t[:3]))
        mid = first + (end - first) // 2
        left = split(first, mid, nid)
        right = split(mid, end, nid)
        nodes[nid]['children'][0] = left
        nodes[nid]['children'][1] = right
        return nid
    ground_tree = split(0, normal, 0xFFFF) if normal else 0xFFFF
    water_tree = split(normal, len(ordered), 0xFFFF) if normal < len(ordered) else 0xFFFF
    ngroups = 4 if water_tree != 0xFFFF else 3
    vertex = 0x34
    tri = vertex + len(vertices) * 12
    block = tri + len(ordered) * 10
    node = (block + len(blocks) * 2 + 3) & ~3
    group = node + len(nodes) * 20
    info = group + 52 * ngroups
    name = info + 16 * len(infos)
    names = [b'R%02d_check\0' % room, b'ground\0', b'floor\0', b'water\0'][:ngroups]
    out = bytearray(name + sum(len(x) for x in names))
    out += bytes(-len(out) % 4)
    counts = [len(vertices), len(ordered), len(blocks), len(nodes), ngroups, len(infos)]
    offs = [vertex, tri, block, node, group, info]
    for i in range(6):
        struct.pack_into('>II', out, i * 8, counts[i], offs[i])
    for i, p in enumerate(vertices):
        struct.pack_into('>3f', out, vertex + 12 * i, *p)
    for i, t in enumerate(ordered):
        struct.pack_into('>5H', out, tri + 10 * i, *t)
    for i, b in enumerate(blocks):
        struct.pack_into('>H', out, block + 2 * i, b)
    for i, n in enumerate(nodes):
        leaf = n['block'] is not None
        struct.pack_into('>HH', out, node + 20 * i, 1 if leaf else 0, n['parent'])
        kids = [n['block']] + [0xFFFF] * 7 if leaf else n['children']
        struct.pack_into('>8H', out, node + 20 * i + 4, *kids)
    at = name
    for gi in range(ngroups):
        g = group + 52 * gi
        struct.pack_into('>I', out, g, at)
        out[at:at + len(names[gi])] = names[gi]
        at += len(names[gi])
        struct.pack_into('>3f', out, g + 4, 1.0, 1.0, 1.0)
        struct.pack_into('>H', out, g + 0x16, 0xFFFF)
        if gi == 0:    # the room: no parent, child 1, the room number
            struct.pack_into('>HHHHHHI', out, g + 0x24, 0xFFFF, 0xFFFF, 1, room, 0, 0xFFFF, 0)
        elif gi == 1:  # the ground: parent 0, child 2
            struct.pack_into('>HHHHHHI', out, g + 0x24, 0, 0xFFFF, 2, 0xFFFF, 0, 0xFFFF, 0)
        elif gi == 2:  # the ground triangles: parent 1, sibling water, the tree, the room table row
            struct.pack_into('>HHHHHHI', out, g + 0x24, 1, 3 if ngroups == 4 else 0xFFFF, 0xFFFF, 0xFFFF, 0, ground_tree, room)
        else:          # swimmable water: the game's water bit
            struct.pack_into('>HHHHHHI', out, g + 0x24, 1, 0xFFFF, 0xFFFF, 0xFFFF, 0, water_tree, room | 0x100)
    for i, inf in enumerate(infos):
        struct.pack_into('>4I', out, info + 16 * i, *inf)
    return bytes(out)


def read_dzb_summary(d):
    """(groups [(name, parent, child, room, info)], triangles [(group, exit)]) of a room.dzb (tests, checks)"""
    nv, ov, nt, ot, nb, ob, nn, on, ng, og, ni, oi = struct.unpack_from('>12I', d, 0)
    groups = []
    for i in range(ng):
        g = og + 52 * i
        no, = struct.unpack_from('>I', d, g)
        parent, sibling, child, room, _, tree, inf = struct.unpack_from('>HHHHHHI', d, g + 0x24)
        groups.append((d[no:d.index(b'\0', no)].decode(), parent, child, room, inf))
    infos = [struct.unpack_from('>4I', d, oi + 16 * i) for i in range(ni)]
    tris = []
    for i in range(nt):
        a, b, c, inf, grp = struct.unpack_from('>5H', d, ot + 10 * i)
        tris.append((grp, (infos[inf][0] >> 13) & 0x3F))
    return groups, tris


# ---- the package's ground model (GLB) -------------------------------------------------------------------------

def _matrix(node):
    if 'matrix' in node:
        m = node['matrix']
        require(len(m) == 16 and all(isinstance(v, (int, float)) and math.isfinite(v) for v in m), 'invalid node matrix')
        return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
    t = node.get('translation', [0, 0, 0])
    q = node.get('rotation', [0, 0, 0, 1])
    s = node.get('scale', [1, 1, 1])
    require(all(isinstance(v, (int, float)) and math.isfinite(v) for v in list(t) + list(q) + list(s)), 'invalid node transform')
    x, y, z, w = q
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0, 0, 0, 1]]


def _mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def read_glb(path):
    """the GLB's triangle primitives in game units (GLB metres x 100, node transforms applied):
    [{'collision': bool, 'material': name, 'positions', 'normals', 'uvs', 'colors', 'triangles'}]. A mesh or
    node whose name ends in _col is collision only; the others are drawn (and are the collision when the
    file has no _col mesh)."""
    require(os.path.getsize(path) <= MAX_GLB, 'the ground model is larger than 16 MiB')
    with open(path, 'rb') as f:
        b = f.read()
    require(len(b) >= 28 and b[:4] == b'glTF' and struct.unpack_from('<I', b, 4)[0] == 2, 'the ground model is not a GLB (glTF 2.0) file')
    jlen, kind = struct.unpack_from('<I4s', b, 12)
    require(kind == b'JSON' and 20 + jlen <= len(b), 'invalid GLB JSON chunk')
    doc = json.loads(b[20:20 + jlen])
    binary = b''
    if 20 + jlen + 8 <= len(b):
        blen, kind = struct.unpack_from('<I4s', b, 20 + jlen)
        require(kind == b'BIN\0' and 28 + jlen + blen <= len(b), 'invalid GLB BIN chunk')
        binary = b[28 + jlen:28 + jlen + blen]
    require(not doc.get('extensionsRequired') and not doc.get('skins'), 'the ground model may not use skinning or required extensions')
    materials = [m.get('name', '') for m in doc.get('materials', [])]

    def acc(i, kinds):
        a = doc['accessors'][i]
        require('sparse' not in a and a['type'] in kinds, 'unsupported GLB accessor')
        v = doc['bufferViews'][a['bufferView']]
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
        fmt = {5126: 'f', 5125: 'I', 5123: 'H', 5121: 'B'}[a['componentType']]
        size = struct.calcsize('<' + fmt)
        stride = v.get('byteStride', n * size)
        o = v.get('byteOffset', 0) + a.get('byteOffset', 0)
        count = a['count']
        require(count <= 4 * MAX_TRIANGLES * 3 and o + (count - 1) * stride + n * size <= len(binary), 'GLB accessor outside the file')
        scale = {'f': 1.0, 'I': 1.0, 'H': 65535.0 if a.get('normalized') else 1.0, 'B': 255.0 if a.get('normalized') else 1.0}[fmt]
        out = []
        for k in range(count):
            t = struct.unpack_from('<%d%s' % (n, fmt), binary, o + k * stride)
            if fmt == 'f':
                require(all(math.isfinite(x) for x in t), 'non-finite GLB value')
            out.append(tuple(x / scale for x in t) if scale != 1.0 else t if n > 1 else t[0])
        return out
    nodes = doc.get('nodes', [])
    out = []
    seen = set()

    def visit(index, parent, depth):
        require(depth < 32 and index not in seen and index < len(nodes), 'invalid GLB node tree')
        seen.add(index)
        node = nodes[index]
        m = _mul(parent, _matrix(node))
        if 'mesh' in node:
            mesh = doc['meshes'][node['mesh']]
            col = str(node.get('name', '')).endswith('_col') or str(mesh.get('name', '')).endswith('_col')
            normal_m = [[m[i][j] for j in range(3)] for i in range(3)]
            for p in mesh['primitives']:
                require(p.get('mode', 4) == 4, 'the ground model has primitives that are not triangles')
                at = p['attributes']
                pos = acc(at['POSITION'], ('VEC3',))
                nrm = acc(at['NORMAL'], ('VEC3',)) if 'NORMAL' in at else None
                uv = acc(at['TEXCOORD_0'], ('VEC2',)) if 'TEXCOORD_0' in at else [(0.0, 0.0)] * len(pos)
                col0 = acc(at['COLOR_0'], ('VEC3', 'VEC4')) if 'COLOR_0' in at else None
                idx = acc(p['indices'], ('SCALAR',)) if 'indices' in p else list(range(len(pos)))
                require(len(idx) % 3 == 0 and all(i < len(pos) for i in idx), 'invalid GLB triangle indices')
                placed = [tuple(100.0 * (sum(m[r][k] * v[k] for k in range(3)) + m[r][3]) for r in range(3)) for v in pos]
                if nrm:
                    nn = []
                    for v in nrm:
                        w = [sum(normal_m[r][k] * v[k] for k in range(3)) for r in range(3)]
                        ln = math.sqrt(sum(x * x for x in w)) or 1.0
                        nn.append(tuple(x / ln for x in w))
                else:
                    nn = None
                tris = [tuple(idx[k:k + 3]) for k in range(0, len(idx), 3)]
                det = (normal_m[0][0] * (normal_m[1][1] * normal_m[2][2] - normal_m[1][2] * normal_m[2][1]) -
                       normal_m[0][1] * (normal_m[1][0] * normal_m[2][2] - normal_m[1][2] * normal_m[2][0]) +
                       normal_m[0][2] * (normal_m[1][0] * normal_m[2][1] - normal_m[1][1] * normal_m[2][0]))
                if det < 0:
                    tris = [(a, c, b) for a, b, c in tris]
                if nn is None:
                    nn = face_normals(placed, tris)
                mi = p.get('material')
                out.append({'collision': col, 'material': materials[mi] if mi is not None and mi < len(materials) else '',
                            'positions': placed, 'normals': nn, 'uvs': [tuple(u) for u in uv],
                            'colors': [tuple(c) + (1.0,) * (4 - len(c)) for c in col0] if col0 else None, 'triangles': tris})
        for child in node.get('children', []):
            visit(child, m, depth + 1)
    scene = doc.get('scenes', [{}])[doc.get('scene', 0)] if doc.get('scenes') else {'nodes': list(range(len(nodes)))}
    identity = [[1 if i == j else 0 for j in range(4)] for i in range(4)]
    for root in scene.get('nodes', []):
        visit(root, identity, 0)
    require(any(not p['collision'] for p in out), 'the ground model has no visible mesh')
    for p in out:
        require(all(abs(x) <= MAX_EXTENT for v in p['positions'] for x in v), 'the ground model reaches farther than %d units from the room origin' % MAX_EXTENT)
    return out


def face_normals(pos, tris):
    acc = [[0.0, 0.0, 0.0] for _ in pos]
    for a, b, c in tris:
        u = [pos[b][k] - pos[a][k] for k in range(3)]
        v = [pos[c][k] - pos[a][k] for k in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        for i in (a, b, c):
            for k in range(3):
                acc[i][k] += n[k]
    out = []
    for n in acc:
        ln = math.sqrt(sum(x * x for x in n)) or 1.0
        out.append(tuple(x / ln for x in n) if any(n) else (0.0, 1.0, 0.0))
    return out


def collision_from(prims, exits):
    """welded collision vertices and triangles (a, b, c, surface, exit) of the GLB, and how many floor
    triangles each exit box took"""
    sources = [p for p in prims if p['collision']] or [p for p in prims if not p['collision']]
    vertices, welded, triangles = [], {}, []
    taken = [0] * len(exits)
    for p in sources:
        name = p['material']
        if p['collision'] or name.startswith('col_'):
            require(name in SURFACES, 'unknown collision material "%s" (col_stone, col_grass, ...)' % name)
        surface = SURFACES.index(name) if name in SURFACES else 0
        remap = []
        for v in p['positions']:
            v = struct.unpack('>3f', struct.pack('>3f', *v))
            if v not in welded:
                welded[v] = len(vertices)
                vertices.append(v)
            remap.append(welded[v])
        for a, b, c in p['triangles']:
            a, b, c = remap[a], remap[b], remap[c]
            if len({a, b, c}) < 3:
                continue
            pa, pb, pc = vertices[a], vertices[b], vertices[c]
            u = [pb[k] - pa[k] for k in range(3)]
            v = [pc[k] - pa[k] for k in range(3)]
            n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            ln = math.sqrt(sum(x * x for x in n))
            if ln < 1e-6:
                continue
            exit_id = NO_EXIT
            if n[1] / ln > 0.5 and surface != WATER:  # a floor: the exit boxes it lies in
                centre = [(pa[k] + pb[k] + pc[k]) / 3 for k in range(3)]
                for k, x in enumerate(exits):
                    if all(x['min'][j] <= centre[j] <= x['max'][j] for j in range(3)):
                        exit_id = k
                        taken[k] += 1
                        break
            triangles.append((a, b, c, surface, exit_id))
    require(len(vertices) <= MAX_VERTICES, 'the ground collision has more than %d vertices' % MAX_VERTICES)
    return vertices, triangles, taken


# ---- a kit room's own collision ------------------------------------------------------------------------------

def kit_collision(dzb, room, exits):
    """the kit room's room.dzb for room `room` of the new stage: every group's room number and room table row
    are the new room's, the game's exits are gone (they lead to the game's places), and the floor triangles in
    the exit boxes take the stage's exits. Returns (dzb, triangles each exit took)."""
    nv, ov, nt, ot, nb, ob, nn, on, ng, og, ni, oi = struct.unpack_from('>12I', dzb, 0)
    verts = [struct.unpack_from('>3f', dzb, ov + 12 * i) for i in range(nv)]
    tris = [list(struct.unpack_from('>5H', dzb, ot + 10 * i)) for i in range(nt)]
    infos = [list(struct.unpack_from('>4I', dzb, oi + 16 * i)) for i in range(ni)]
    for inf in infos:
        inf[0] = (inf[0] & ~0x7E000) | (NO_EXIT << 13)
    groups = []
    for i in range(ng):
        g = bytearray(dzb[og + 52 * i:og + 52 * i + 52])
        no, = struct.unpack_from('>I', g, 0)
        name = dzb[no:dzb.index(b'\0', no) + 1]
        rm, = struct.unpack_from('>H', g, 0x2A)
        if rm != 0xFFFF:
            struct.pack_into('>H', g, 0x2A, room)
        inf, = struct.unpack_from('>I', g, 0x30)
        struct.pack_into('>I', g, 0x30, (inf & ~0xFF) | room)
        groups.append((g, name))
    taken = [0] * len(exits)
    extra = {}
    for t in tris:
        pa, pb, pc = verts[t[0]], verts[t[1]], verts[t[2]]
        u = [pb[k] - pa[k] for k in range(3)]
        v = [pc[k] - pa[k] for k in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        ln = math.sqrt(sum(x * x for x in n))
        if ln < 1e-6 or n[1] / ln <= 0.5:
            continue
        centre = [(pa[k] + pb[k] + pc[k]) / 3 for k in range(3)]
        for k, x in enumerate(exits):
            if all(x['min'][j] <= centre[j] <= x['max'][j] for j in range(3)):
                key = (t[3], k)
                if key not in extra:
                    extra[key] = len(infos)
                    w = list(infos[t[3]])
                    w[0] = (w[0] & ~0x7E000) | (k << 13)
                    infos.append(w)
                t[3] = extra[key]
                taken[k] += 1
                break
    # the same layout, the info table grown and the names after it
    head = bytearray(dzb[:og])
    body = bytearray(head)
    for i, t in enumerate(tris):
        struct.pack_into('>5H', body, ot + 10 * i, *t)
    group_at = len(body)
    body += bytes(52 * ng)
    info_at = len(body)
    for inf in infos:
        body += struct.pack('>4I', *inf)
    for i, (g, name) in enumerate(groups):
        struct.pack_into('>I', g, 0, len(body))
        body[group_at + 52 * i:group_at + 52 * i + 52] = g
        body += name
    body += bytes(-len(body) % 4)
    struct.pack_into('>12I', body, 0, nv, ov, nt, ot, nb, ob, nn, on, ng, group_at, len(infos), info_at)
    return bytes(body), taken


# ---- the map (the port draws a stage's dungeon map from these; nothing of it goes to the game) -------------

MAP_MAX = 512  # pixels per side of a room's picture


def floor_triangles(dzb):
    """the floors of a room.dzb: [(a, b, c) positions] of the triangles facing up (not swimmable water)"""
    nv, ov, nt, ot, nb, ob, nn, on, ng, og, ni, oi = struct.unpack_from('>12I', dzb, 0)
    verts = [struct.unpack_from('>3f', dzb, ov + 12 * i) for i in range(nv)]
    water = set()
    for i in range(ng):
        inf, = struct.unpack_from('>I', dzb, og + 52 * i + 0x30)
        if inf & 0x100:
            water.add(i)
    out = []
    for i in range(nt):
        a, b, c, inf, grp = struct.unpack_from('>5H', dzb, ot + 10 * i)
        if grp in water:
            continue
        pa, pb, pc = verts[a], verts[b], verts[c]
        u = [pb[k] - pa[k] for k in range(3)]
        v = [pc[k] - pa[k] for k in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        ln = math.sqrt(sum(x * x for x in n))
        if ln > 1e-6 and n[1] / ln > 0.5:
            out.append((pa, pb, pc))
    return out


def floor_below(tris, x, y, z):
    """the height of the floor at (x, z) closest below y + 150 (a spawn a little above or in the floor stands on
    it), or None"""
    best = None
    for pa, pb, pc in tris:
        area = (pb[0] - pa[0]) * (pc[2] - pa[2]) - (pb[2] - pa[2]) * (pc[0] - pa[0])
        if abs(area) < 1e-9:
            continue
        w0 = (pb[0] - pa[0]) * (z - pa[2]) - (pb[2] - pa[2]) * (x - pa[0])
        w1 = (pc[0] - pb[0]) * (z - pb[2]) - (pc[2] - pb[2]) * (x - pb[0])
        w2 = (pa[0] - pc[0]) * (z - pc[2]) - (pa[2] - pc[2]) * (x - pc[0])
        if not ((w0 >= 0 and w1 >= 0 and w2 >= 0) or (w0 <= 0 and w1 <= 0 and w2 <= 0)):
            continue
        h = (w1 * pa[1] + w2 * pb[1] + w0 * pc[1]) / area
        if h <= y + 150 and (best is None or h > best):
            best = h
    return best


def snap_spawns(room, floors):
    """each spawn stands on the floor below it (the recipe's spawns are changed in place); notes for the ones moved
    or standing on nothing"""
    notes = []
    for s in room['spawns']:
        x, y, z = s['position']
        h = floor_below(floors, x, y, z)
        if h is None:
            # the nearest floor instead (the middle of the floor triangle nearest in the plan)
            best = None
            for t in floors:
                cx, cy, cz = [sum(p[k] for p in t) / 3 for k in range(3)]
                d = (cx - x) ** 2 + (cz - z) ** 2
                if best is None or d < best[0]:
                    best = (d, cx, cy, cz)
            if best is None:
                notes.append('spawn %d of room %d: the room has no floor' % (s['point'], room['room']))
                continue
            s['position'] = [best[1], best[2], best[3]]
            notes.append('spawn %d of room %d had no floor below it: moved to the nearest floor (%.0f, %.0f, %.0f)' % (s['point'], room['room'], best[1], best[2], best[3]))
        elif abs(h - y) > 1:
            notes.append('spawn %d of room %d moved onto the floor (y %.0f -> %.0f)' % (s['point'], room['room'], y, h))
            s['position'] = [x, h, z]
    return notes


def room_map(tris, units):
    """a room's floor seen from above: (x0, z0, width, height, RGBA rows) at `units` game units per pixel, north
    (-z) up. Grey, lighter the higher the floor (a pit or a lava basin shows darker than the walkways), with a
    dark edge where the floor ends or steps by more than 60 units; the port tints it."""
    if not tris:
        return None
    xs = [p[0] for t in tris for p in t]
    zs = [p[2] for t in tris for p in t]
    x0, z0 = min(xs) - units, min(zs) - units
    w = min(MAP_MAX, int(math.ceil((max(xs) + units - x0) / units)))
    h = min(MAP_MAX, int(math.ceil((max(zs) + units - z0) / units)))
    top = [None] * (w * h)
    for pa, pb, pc in tris:
        ax, az = (pa[0] - x0) / units, (pa[2] - z0) / units
        bx, bz = (pb[0] - x0) / units, (pb[2] - z0) / units
        cx, cz = (pc[0] - x0) / units, (pc[2] - z0) / units
        area = (bx - ax) * (cz - az) - (bz - az) * (cx - ax)
        if abs(area) < 1e-9:
            continue
        for py in range(max(0, int(min(az, bz, cz))), min(h, int(max(az, bz, cz)) + 1)):
            for px in range(max(0, int(min(ax, bx, cx))), min(w, int(max(ax, bx, cx)) + 1)):
                qx, qz = px + 0.5, py + 0.5
                w0 = (bx - ax) * (qz - az) - (bz - az) * (qx - ax)
                w1 = (cx - bx) * (qz - bz) - (cz - bz) * (qx - bx)
                w2 = (ax - cx) * (qz - cz) - (az - cz) * (qx - cx)
                if (w0 >= 0 and w1 >= 0 and w2 >= 0) or (w0 <= 0 and w1 <= 0 and w2 <= 0):
                    # the floor's height there (barycentric)
                    y = (w1 * pa[1] + w2 * pb[1] + w0 * pc[1]) / area  # w1, w2, w0: the weights of a, b, c
                    k = py * w + px
                    if top[k] is None or y > top[k]:
                        top[k] = y
    heights = [v for v in top if v is not None]
    lo, hi = min(heights), max(heights)
    rows = bytearray()
    for y in range(h):
        rows.append(0)  # PNG filter: none
        for x in range(w):
            v = top[y * w + x]
            if v is None:
                rows += b'\0\0\0\0'
                continue
            edge = False
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = top[(y + dy) * w + x + dx] if 0 <= x + dx < w and 0 <= y + dy < h else None
                if n is None or n < v - 60:
                    edge = True
            g = 0x60 if edge else int(0x90 + 0x6F * ((v - lo) / (hi - lo) if hi > lo else 1.0))
            rows += bytes((g, g, g, 0xFF))
    return x0, z0, w, h, bytes(rows)


def png(width, height, rows):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


# ---- the room archive (graft) --------------------------------------------------------------------------------

def mesh_shape(src, template, prim):
    """a Shape (game_piece_model) with the template shape's vertex layout and the primitive's geometry: each
    record starts as the template's first record, then position, normal, texture coordinate and colour"""
    s = gp.Shape()
    s.at, s.name, s.node, s.material = template.at, 'mesh', 'mesh', template.material
    s.vertex, s.nattr, s.stride, s.at_p = template.vertex, template.nattr, template.stride, template.at_p
    attrs = src.rel(s.vertex + 0x10)
    layout = {src.cstr(src.rel(attrs + i * 12)): (src.u16(attrs + i * 12 + 6), src.u32(attrs + i * 12 + 8)) for i in range(s.nattr)}
    first = template.records[0]
    s.records, s.pos = [], []
    for k, p in enumerate(prim['positions']):
        r = bytearray(first)
        struct.pack_into('>3f', r, s.at_p, *p)
        if '_n0' in layout:
            put_normal(r, layout['_n0'], prim['normals'][k])
        if '_u0' in layout:
            put_uv(r, layout['_u0'], prim['uvs'][k])
        if '_c0' in layout:
            put_color(r, layout['_c0'], prim['colors'][k] if prim['colors'] else (1.0, 1.0, 1.0, 1.0))
        s.records.append(bytes(r))
        s.pos.append(tuple(p))
    s.tris = list(prim['triangles'])
    return s


def put_normal(r, spec, n):
    at, fmt = spec
    if fmt == 0x811:
        struct.pack_into('>3f', r, at, *n)
    elif fmt == 0x20B:  # snorm 10_10_10_2
        q = [max(-511, min(511, int(round(v * 511)))) & 0x3FF for v in n]
        struct.pack_into('>I', r, at, q[0] | q[1] << 10 | q[2] << 20)
    elif fmt == 0x20A:  # snorm 8x4
        struct.pack_into('>4b', r, at, *[max(-127, min(127, int(round(v * 127)))) for v in n], 0)
    else:
        raise BuildError('the kit material\'s normal format is not supported (0x%X)' % fmt)


def put_uv(r, spec, uv):
    at, fmt = spec
    if fmt == 0x80D:
        struct.pack_into('>2f', r, at, uv[0], uv[1])
    elif fmt == 0x808:  # float 16x2
        struct.pack_into('>2e', r, at, uv[0], uv[1])
    else:
        raise BuildError('the kit material\'s texture coordinate format is not supported (0x%X)' % fmt)


def put_color(r, spec, c):
    at, fmt = spec
    if fmt == 0x813:
        struct.pack_into('>4f', r, at, *c)
    elif fmt == 0x00A:
        struct.pack_into('>4B', r, at, *[max(0, min(255, int(round(v * 255)))) for v in c])
    elif fmt == 0x80F:
        struct.pack_into('>4e', r, at, *c)


def replace_embedded(bfres, name, body):
    f = base.Bfres(bfres)
    group = f.rel(0x20 + 4 * 11)
    for n, at in f.dict(group):
        if n == name:
            f.set_rel(at, f.append(body, 0x20))
            f.put('I', at + 4, len(body))
            f.append(b'', 0x1000)
            f.put('I', 0xC, len(f.b))
            return bytes(f.b)
    raise BuildError('the kit room has no embedded ' + name)


def embedded_files(src):
    group = src.rel(0x20 + 4 * 11)
    out = {}
    for n, at in src.dict(group) if group else []:
        out[n] = bytes(src.b[src.rel(at):src.rel(at) + src.u32(at + 4)])
    return out


def kit_materials(src):
    """material name -> (kit model name, the first shape drawn with it)"""
    out = {}
    for mname, _ in src.dict(src.rel(0x20)):
        _, shapes, _ = gp.read_model(src, mname)
        for s in shapes:
            out.setdefault(s.material, (mname, s))
    return out


def build_room_archive(kit_files, kit_room, room, prims, recipe_room, dzb, dzr):
    bname = 'Room%d.bfres' % kit_room
    require(bname in kit_files, 'the kit room archive has no ' + bname)
    data = kit_files[bname]
    src = base.Bfres(data)
    if recipe_room.get('kit_ground'):
        # the kit room as it is: its models, their shader archives, its own collision (rewritten)
        out = base.Bfres(data)
        out.set_rel(0x14, append_string(out, 'Room%d' % room))
        out.append(b'', 0x1000)
        out.put('I', 0xC, len(out.b))
        body = bytes(out.b)
        for name, b in (('room.dzb', dzb), ('room.dzr', dzr)):
            body = replace_embedded(body, name, b)
        members = {n: b for n, b in kit_files.items() if n.endswith('.sharcfb')}
        members['Room%d.bfres' % room] = body
        tris = sum(len(sh.tris) for m, _ in src.dict(src.rel(0x20)) for sh in gp.read_model(src, m)[1])
        return base.yaz0_stored(base.sarc(members)), tris
    mats = kit_materials(src)
    require(mats, 'the kit room has no background model')
    default = recipe_room.get('material') or sorted(mats)[0]
    require(default in mats, 'the kit room has no material "%s" (it has: %s)' % (default, ', '.join(sorted(mats))))
    mapping = recipe_room.get('materials', {})
    for glb_name, kit_name in mapping.items():
        require(kit_name in mats, 'the kit room has no material "%s" (it has: %s)' % (kit_name, ', '.join(sorted(mats))))
    # visible primitives grouped by the kit model their material comes from: one output model each (the
    # game looks a model's shader archive up by the model's name)
    # the parts drawn with one kit material become one shape (a model of a hundred parts draws as a few)
    merged = {}
    tris = 0
    for p in prims:
        if p['collision']:
            continue
        kit_name = mapping.get(p['material']) or (p['material'] if p['material'] in mats else default)
        m = merged.setdefault(kit_name, {'positions': [], 'normals': [], 'uvs': [], 'colors': [], 'triangles': [], 'has_colors': False})
        base_index = len(m['positions'])
        m['positions'] += p['positions']
        m['normals'] += p['normals']
        m['uvs'] += p['uvs']
        m['colors'] += p['colors'] if p['colors'] else [(1.0, 1.0, 1.0, 1.0)] * len(p['positions'])
        m['has_colors'] |= bool(p['colors'])
        m['triangles'] += [(a + base_index, b + base_index, c + base_index) for a, b, c in p['triangles']]
        tris += len(p['triangles'])
    by_model = {}
    for kit_name, m in sorted(merged.items()):
        if not m['has_colors']:
            m['colors'] = None
        kit_model, template = mats[kit_name]
        by_model.setdefault(kit_model, []).append(mesh_shape(src, template, m))
    require(0 < tris <= MAX_TRIANGLES, 'the ground model has more than %d visible triangles' % MAX_TRIANGLES)
    require(len(by_model) <= 4, 'the ground model uses materials of more than four kit models')
    out = base.Bfres(data)
    w = gp.Writer(data, out)
    textures = dict(src.dict(src.rel(0x24)))
    models = []
    members = {}
    for i, (kit_model, shapes) in enumerate(sorted(by_model.items())):
        model_at, _, materials = gp.read_model(src, kit_model)
        name = gp.model_name(i)
        dst, _, _ = gp.write_model(w, model_at, name, shapes, {k: s.tris for k, s in enumerate(shapes)}, materials, textures, (0.0, 0.0, 0.0))
        models.append((name, dst))
        archive = kit_model + '.sharcfb'
        require(archive in kit_files, 'the kit room has no shader archive ' + archive)
        members[name + '.sharcfb'] = kit_files[archive]
    out.set_rel(0x20, append_dictionary(out, models))
    out.put('H', 0x50, len(models))
    out.set_rel(0x14, append_string(out, 'Room%d' % room))
    out.append(b'', 0x1000)
    out.put('I', 0xC, len(out.b))
    body = bytes(out.b)
    old = embedded_files(src)
    for name, b in (('room.dzb', dzb), ('room.dzr', dzr)):
        require(name in old, 'the kit room has no embedded ' + name)
        body = replace_embedded(body, name, b)
    members['Room%d.bfres' % room] = body
    return base.yaz0_stored(base.sarc(members)), tris


# ---- the recipe --------------------------------------------------------------------------------------------

def _point(v):
    return isinstance(v, list) and len(v) == 3 and all(isinstance(x, (int, float)) and math.isfinite(x) and abs(x) <= MAX_EXTENT for x in v)


def _place(v, stage_name):
    return (isinstance(v, dict) and set(v) == {'stage', 'room', 'point'} and isinstance(v['stage'], str) and STAGE.fullmatch(v['stage'])
            and isinstance(v['room'], int) and 0 <= v['room'] <= 63 and isinstance(v['point'], int) and 0 <= v['point'] <= 255)


def check_recipe(r):
    require(isinstance(r, dict) and r.get('format') == FORMAT, 'unsupported recipe format')
    require(isinstance(r.get('name'), str) and NAME.fullmatch(r['name']), 'invalid stage name')
    require(isinstance(r.get('template'), str) and STAGE.fullmatch(r['template']), 'invalid template stage')
    require(r.get('kind') in KINDS, 'kind is cave or dungeon')
    require(_place(r.get('entrance'), r['name']) and r['entrance']['stage'] != r['name'], 'the entrance is a stage, room and point of the game')
    rooms = r.get('rooms')
    require(isinstance(rooms, list) and 0 < len(rooms) <= MAX_ROOMS, 'a stage has 1 to %d rooms' % MAX_ROOMS)
    require([x.get('room') for x in rooms] == list(range(len(rooms))), 'rooms are numbered 0, 1, ... in order')
    points = set()
    for x in rooms:
        kit = x.get('kit')
        require(isinstance(kit, dict) and isinstance(kit.get('stage'), str) and STAGE.fullmatch(kit['stage']) and isinstance(kit.get('room'), int) and
                0 <= kit['room'] <= 63, 'room %d: invalid kit room' % x['room'])
        require(x.get('kit_ground') or (isinstance(x.get('ground'), str) and x['ground']), 'room %d: no ground model' % x['room'])
        actors = x.get('actors', [])
        require(isinstance(actors, list) and len(actors) <= 16, 'room %d: at most 16 actors' % x['room'])
        for a in actors:
            require(a.get('tag') in ('ACTR', 'TRES') and isinstance(a.get('name'), str) and 0 < len(a['name']) <= 8 and isinstance(a.get('params'), (int, float))
                    and _point(a.get('position')), 'room %d: invalid actor' % x['room'])
        spawns = x.get('spawns')
        require(isinstance(spawns, list) and 0 < len(spawns) <= MAX_SPAWNS, 'room %d: 1 to %d spawns' % (x['room'], MAX_SPAWNS))
        for s in spawns:
            require(isinstance(s.get('point'), int) and 0 <= s['point'] <= 99 and s['point'] not in points, 'room %d: spawn points are 0..99 and unique in the stage' % x['room'])
            points.add(s['point'])
            require(_point(s.get('position')) and isinstance(s.get('angle', 0), (int, float)) and s.get('start', 'stand') in STARTS, 'room %d: invalid spawn' % x['room'])
        exits = x.get('exits', [])
        require(isinstance(exits, list) and len(exits) <= MAX_EXITS, 'room %d: at most %d exits' % (x['room'], MAX_EXITS))
        for e in exits:
            require(_point(e.get('min')) and _point(e.get('max')) and all(e['min'][k] < e['max'][k] for k in range(3)), 'room %d: an exit box needs min < max' % x['room'])
            require(_place(e.get('to'), r['name']), 'room %d: an exit leads to a stage, room and point' % x['room'])
    for e in (e for x in rooms for e in x.get('exits', [])):
        if e['to']['stage'] == r['name']:
            require(e['to']['room'] < len(rooms) and any(s['point'] == e['to']['point'] for s in rooms[e['to']['room']]['spawns']),
                    'an exit leads to a spawn the stage does not have')
    doors = r.get('doors', [])
    require(isinstance(doors, list) and len(doors) <= MAX_DOORS, 'at most %d doors' % MAX_DOORS)
    for d in doors:
        require(_point(d.get('position')) and isinstance(d.get('angle', 0), (int, float)), 'invalid door')
        rr = d.get('rooms')
        require(isinstance(rr, list) and len(rr) == 2 and all(isinstance(v, int) and 0 <= v < len(rooms) for v in rr) and rr[0] != rr[1],
                'a door joins two different rooms of the stage')
        require(d.get('name', DOOR_MODEL) in DOOR_MODELS, 'a door is one of the game\'s knob doors')
    for j in r.get('joins', []):
        require(isinstance(j, list) and len(j) == 2 and all(isinstance(v, int) and 0 <= v < len(rooms) for v in j) and j[0] != j[1],
                'a join is two different rooms of the stage')


def check_room_table(dzs, doors, joins=()):
    """every door's and join's rooms list each other in the room table (else the door event never ends, and a
    room behind an open passage is not loaded)"""
    chunks = read_dz(dzs)
    t = chunk(chunks, 'RTBL')
    rows = read_rtbl(dzs, t[3], t[1])
    for d in list(doors) + [{'rooms': list(j)} for j in joins]:
        a, b = d['rooms']
        require(b in [e & 0x3F for e in rows[a][3]] and a in [e & 0x3F for e in rows[b][3]],
                'the room table does not list the rooms of a door (%d, %d)' % (a, b))


def build(game, recipe_path, out_dir):
    with open(recipe_path, 'rb') as f:
        recipe_bytes = f.read(256 * 1024 + 1)
    require(len(recipe_bytes) <= 256 * 1024, 'recipe too large')
    try:
        recipe = json.loads(recipe_bytes)
    except ValueError as exc:
        raise BuildError('the recipe is not JSON') from exc
    check_recipe(recipe)
    name = recipe['name']
    digest = hashlib.sha256()
    for path in (__file__, gp.__file__):
        with open(path, 'rb') as f:
            digest.update(f.read())
    digest.update(recipe_bytes)
    stage_files = gp.room_files(game, '%s_Stage.szs' % recipe['template'])
    kits = {}
    for r in recipe['rooms']:
        k = (r['kit']['stage'], r['kit']['room'])
        if k not in kits:
            kits[k] = gp.room_files(game, '%s_Room%d.szs' % k)
    for label, files in [('stage', stage_files)] + [('kit/%s/%d' % k, v) for k, v in sorted(kits.items())]:
        for n in sorted(files):
            digest.update(label.encode() + b'/' + n.encode() + b'\0' + hashlib.sha256(files[n]).digest())
    for r in recipe['rooms']:
        if not r.get('kit_ground'):
            with open(r['ground'], 'rb') as f:
                digest.update(hashlib.sha256(f.read()).digest())
    key = digest.hexdigest()
    meta_path = os.path.join(out_dir, 'stage.json')
    cached = cached_meta(out_dir, meta_path, key)
    if cached:
        cached['cached'] = True
        return cached
    stage_bfres = [n for n in stage_files if n.endswith('.bfres')]
    require(len(stage_bfres) == 1, 'the template stage archive has no stage model file')
    sb = base.Bfres(stage_files[stage_bfres[0]])
    stage_embedded = embedded_files(sb)
    require('stage.dzs' in stage_embedded, 'the template stage has no stage.dzs')
    stage_models = dict(sb.dict(sb.rel(0x20)))
    for d in recipe.get('doors', []):
        params = d.get('params', 0x0FFFF0FF)
        need = DOOR_MODELS[d.get('name', DOOR_MODEL)] + ([LOCK_MODELS[(params >> 8) & 0xF]] if d.get('name', DOOR_MODEL) == 'door10' and
                                                         (params >> 8) & 0xF in LOCK_MODELS and (params & 0xFF) != 0xFF else [])
        for m in need:
            require(m in stage_models, 'the template %s has no %s model (%s): pick a template with that kind of door' % (
                recipe['template'], m, {'door10': 'the knob door', 'door20': 'the boss door', 'key10': 'the small-key lock', 'stop10': 'the shutter bars'}[m]))
    dzs = build_dzs(stage_embedded['stage.dzs'], recipe)
    check_room_table(dzs, recipe.get('doors', []), recipe.get('joins', []))
    stage_members = dict(stage_files)
    stage_members[stage_bfres[0]] = replace_embedded(stage_files[stage_bfres[0]], 'stage.dzs', dzs)
    outputs = {'%s_Stage.szs' % name: base.yaz0_stored(base.sarc(stage_members))}
    rooms_meta = []
    floors = {}
    for r in recipe['rooms']:
        kit_files = kits[(r['kit']['stage'], r['kit']['room'])]
        kb = [n for n in kit_files if n.endswith('.bfres')]
        require(len(kb) == 1, 'the kit room archive has no room model file')
        kit_embedded = embedded_files(base.Bfres(kit_files[kb[0]]))
        require('room.dzr' in kit_embedded and 'room.dzb' in kit_embedded, 'the kit room has no room.dzr or room.dzb')
        if r.get('kit_ground'):
            prims = []
            dzb, taken = kit_collision(kit_embedded['room.dzb'], r['room'], r.get('exits', []))
            triangles = range(struct.unpack_from('>I', dzb, 8)[0])
        else:
            prims = read_glb(r['ground'])
            vertices, triangles, taken = collision_from(prims, r.get('exits', []))
            dzb = build_dzb(vertices, triangles, r['room'])
        for k, n in enumerate(taken):
            require(n > 0, 'room %d: exit %d covers no floor of the ground model' % (r['room'], k))
        snapped = snap_spawns(r, floor_triangles(dzb))
        dzr = build_dzr(kit_embedded['room.dzr'], r['room'], r)
        archive, tris = build_room_archive(kit_files, int(kb[0][4:-6]), r['room'], prims, r, dzb, dzr)
        outputs['%s_Room%d.szs' % (name, r['room'])] = archive
        rooms_meta.append({'room': r['room'], 'visible_triangles': tris, 'collision_triangles': len(triangles),
                           'exit_triangles': taken, 'spawns': [{'point': s['point'], 'position': s['position'], 'angle': s.get('angle', 0)} for s in r['spawns']],
                           'spawn_notes': snapped})
        floors[r['room']] = floor_triangles(dzb)
    # the map: one picture per room, every room at the same scale (the stage's floors fit 400 pixels)
    if recipe.get('map_mode') == 1:
        pts = [p for tris_ in floors.values() for t in tris_ for p in t]
        if pts:
            span = max(max(p[0] for p in pts) - min(p[0] for p in pts), max(p[2] for p in pts) - min(p[2] for p in pts))
            units = max(10.0, min(200.0, span / 400.0))
            for meta_room, r in zip(rooms_meta, recipe['rooms']):
                pic = room_map(floors[r['room']], units)
                if not pic:
                    continue
                x0, z0, w, h, rows = pic
                fname = '%s_map%d.png' % (name, r['room'])
                outputs[fname] = png(w, h, rows)
                meta_room['map'] = {'file': fname, 'x': x0, 'z': z0, 'units': units, 'width': w, 'height': h, 'floor': r.get('floor', 1)}
    os.makedirs(out_dir, exist_ok=True)
    files = {}
    for fname, body in sorted(outputs.items()):
        publish(out_dir, os.path.join(out_dir, fname), body)
        files[fname] = hashlib.sha256(body).hexdigest()
    meta = {'ok': True, 'builder_version': VERSION, 'cache_key': key, 'name': name, 'files': files, 'rooms': rooms_meta, 'cached': False}
    # the metadata commits the generation last
    publish(out_dir, meta_path, (json.dumps(meta, sort_keys=True) + '\n').encode())
    return meta


def publish(directory, path, body):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=directory, prefix='.stage-', delete=False) as target:
            temporary = target.name
            target.write(body)
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            os.unlink(temporary)


def cached_meta(out_dir, meta_path, key):
    """the previous build's metadata when every output is there and unchanged; otherwise None (rebuild)"""
    try:
        if os.path.getsize(meta_path) > 256 * 1024:
            return None
        with open(meta_path) as f:
            meta = json.load(f)
        if meta.get('cache_key') != key or meta.get('builder_version') != VERSION:
            return None
        for fname, sha in meta['files'].items():
            with open(os.path.join(out_dir, fname), 'rb') as f:
                if hashlib.sha256(f.read()).hexdigest() != sha:
                    return None
        return meta
    except (OSError, ValueError, TypeError, AttributeError, KeyError):
        return None


DOOR_KINDS = {'door10': 'knob', 'door20': 'boss', 'key10': 'small_key', 'stop10': 'shutter'}
SKY_MODELS = ('vr_sky', 'vr_back_cloud', 'vr_kasumi_mae', 'vr_uso_umi')


def templates(game):
    """the player's stages a mod stage can take as its template: their rooms, whether the stage archive has the
    sky dome, and which kinds of door it has the models of (names and facts only, nothing of the files)"""
    folder = os.path.join(game, 'content', 'Common', 'Stage')
    names = sorted(os.listdir(folder))
    out = []
    for fname in names:
        m = re.fullmatch(r'([A-Za-z0-9_]{1,8})_Stage\.szs', fname)
        if not m:
            continue
        stage = m.group(1)
        rooms = sorted(int(r.group(1)) for r in (re.fullmatch(re.escape(stage) + r'_Room(\d+)\.szs', n) for n in names) if r)
        entry = {'stage': stage, 'rooms': rooms, 'sky': False, 'doors': []}
        try:
            files = gp.room_files(game, fname)
            bf = [n for n in files if n.endswith('.bfres')]
            if len(bf) != 1:
                continue
            b = base.Bfres(files[bf[0]])
            try:
                models = set(dict(b.dict(b.rel(0x20))))
            except (UnicodeDecodeError, ValueError, IndexError, struct.error):
                models = set()  # a stage archive without models of its own
            if 'stage.dzs' not in embedded_files(b):
                continue
        except (BuildError, OSError, KeyError, IndexError, struct.error, ValueError):
            continue
        entry['sky'] = all(n in models for n in SKY_MODELS)
        entry['doors'] = sorted(kind for model, kind in DOOR_KINDS.items() if model in models)
        out.append(entry)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--game', required=True)
    parser.add_argument('--templates', action='store_true', help='list the stages a mod stage can take as its template')
    parser.add_argument('--recipe')
    parser.add_argument('--out')
    args = parser.parse_args()
    if args.templates:
        try:
            print(json.dumps({'ok': True, 'templates': templates(args.game)}, sort_keys=True))
        except OSError as exc:
            print(json.dumps({'ok': False, 'error': str(exc)}))
            sys.exit(1)
        return
    if not args.recipe or not args.out:
        parser.error('--recipe and --out (or --templates)')
    try:
        meta = build(args.game, args.recipe, args.out)
    except (BuildError, OSError, KeyError, IndexError, struct.error, ValueError, TypeError) as exc:
        print(json.dumps({'ok': False, 'error': str(exc)}))
        sys.exit(1)
    print(json.dumps(meta, sort_keys=True))


if __name__ == '__main__':
    main()
