#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Game pieces: island buildings built from the player's own room archive.

A piece set recipe (runtime/assets/game_pieces/<set>.recipe.json) names a room archive, and for each
piece a room-local volume, the room's background (BG) models and nodes it takes, the light set each
part is drawn with, and the collision groups it keeps. Nothing from the game is in the recipe or in
this file: the shapes, materials, textures, shader archives and collision are read from the player's
installation, and the output goes only to the player's model cache.

Output, for one set: <out> = Yaz0(SARC) with one <identity>.bfres holding one model per piece part
(the set's textures once, shared by every piece), one <model>.sharcfb per model (the part's BG shader
archive, which the game looks up by model name), and next to it <out>.json (metadata) and
<out>.<piece>.wcol (each piece's collision, WCOL v1 as written by collision_mesh.encode).

Selection never cuts a mesh: per listed node, the welded connected components lying wholly inside
the volume are taken and every component crossing it is left out; the recipe's expected triangle
counts guard against another game version. Collision: the listed room collision groups, triangles
wholly inside the volume; exit, camera, path, special and shadow fields are dropped (door polygons
stay as plain walls), and the ground type maps to the world-object surfaces.

Island grounds (category Islands) use three more recipe fields: collision.void (groups whose triangles become
a void: a hole's floor that led into a cave sends Link back to the restart point instead), collision.strip_exits
(any other triangle with an exit is left out) and distant ({"archive": "sea_LODNN.szs", "triangles": n}: the
island's own distant model from the game's distant-view archive of the same room, drawn in the neighbouring
squares; written as an archive of its own, <out>.<piece>.distant.szs, so a neighbouring square loads only it). origin_y puts the piece's origin at that room-local height (0: the island's sea level) instead of its
lowest point. Water groups (the game's water bit) become swimmable water. A piece's second part may be
"animated": true (an island's water model, model1): the room's texture animation rows for that part's materials
are copied into the set as an animation named like the part's model, which the game plays on it.
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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notebook_model as base  # noqa: E402
import collision_mesh  # noqa: E402
from skinned_actor_model import append_dictionary, append_string  # noqa: E402

VERSION = 3
FORMAT = 1
BuildError = base.BuildError
LIGHTS = {'bg0': 1, 'bg1': 2, 'bg2': 3, 'bg3': 4}
# vertex attribute formats a BG vertex may use (GX2: float 32x3/32x4/32x2, unorm 8x4, 16-bit float); records are
# copied as they are and only the float position is moved
FORMATS = {0x811: 12, 0x813: 16, 0x80D: 8, 0x00A: 4, 0x80F: 8, 0x808: 4, 0x007: 4, 0x20A: 4, 0x00B: 4, 0x20B: 4}
# The game's attribute conversion (dBgS::GetAttributeCode): a triangle's raw attribute code -> the class the
# game treats it as (footsteps, effects, damage). Several raw codes share a class (12, 13, 14, 16 and 26 are wood;
# 17, 20 and 24 metal); 9 is damage, not metal. A fact about the format, like the runtime's NativeResultIds.
ATTRIBUTE_CLASS = (0, 1, 2, 3, 4, 5, 6, 1, 8, 9, 10, 11, 2, 2, 2, 15, 2, 20, 1, 19, 20, 21, 22, 23, 20, 10, 2, 0, 0, 0, 0, 0)
CLASS_SURFACES = {2: 'col_wood', 3: 'col_stone', 4: 'col_grass', 6: 'col_lava', 11: 'col_sand', 20: 'col_metal', 15: 'col_ice',
                  19: 'col_shallow', 23: 'col_waterfall'}
WATER_GROUP = 0x100  # a collision group's info bit: the game's swimmable water
LAVA_GROUP = 0x200   # and the game's lava (its surface; the lava's bottom below it is a floor of attribute class 6)
# raw attribute code -> WCOL surface (codes of other classes: stone on floors, a plain wall otherwise)
SURFACES = {code: CLASS_SURFACES[c] for code, c in enumerate(ATTRIBUTE_CLASS) if c in CLASS_SURFACES}
MAX_ARCHIVE = 12 * 1024 * 1024
PACKS = ('first_szs_permanent.pack', 'szs_permanent0.pack', 'szs_permanent1.pack', 'szs_permanent2.pack')


def require(ok, message):
    if not ok:
        raise BuildError(message)


# ---- the recipe -----------------------------------------------------------------------------------

def neutral(s, longest=32):
    return isinstance(s, str) and 0 < len(s) <= longest and re.fullmatch(r'[a-z0-9]+(-[a-z0-9]+)*', s) is not None


def check_recipe(r):
    """The same rules as story_game_pieces_catalogue.h (the runtime parses the recipe first)."""
    require(isinstance(r, dict) and r.get('game_pieces_format') == FORMAT, 'unsupported game_pieces_format')
    require(neutral(r.get('set')), 'invalid set')
    src = r.get('source', {})
    require(isinstance(src.get('archive'), str) and re.fullmatch(r'sea_Room[0-9]{1,2}\.szs', src['archive']), 'invalid source archive')
    require(type(src.get('room')) is int and 1 <= src['room'] <= 49, 'invalid source room')
    for name, rule in r.get('textures', {}).items():
        require(isinstance(name, str) and re.fullmatch(r'[A-Za-z0-9_]{1,63}', name) and isinstance(rule, dict) and
                type(rule.get('max_size')) is int and rule['max_size'] in (128, 256, 512, 1024), 'invalid texture rule')
    pieces = r.get('pieces')
    require(isinstance(pieces, list) and 0 < len(pieces) <= 16, 'a set has 1 to 16 pieces')
    seen = set()
    for p in pieces:
        require(neutral(p.get('id')) and p['id'] not in seen, 'invalid or duplicate piece id')
        seen.add(p['id'])
        v = p.get('volume')
        require(isinstance(v, list) and len(v) == 6 and all(type(x) in (int, float) for x in v) and all(v[k] < v[k + 3] for k in range(3)), p['id'] + ': invalid volume')
        parts = p.get('parts')
        require(isinstance(parts, list) and 1 <= len(parts) <= 2, p['id'] + ': 1 or 2 parts')
        for part in parts:
            require(type(part.get('model')) is int and 0 <= part['model'] <= 3, p['id'] + ': invalid BG model')
            require(part.get('light') in LIGHTS, p['id'] + ': invalid light')
            require(isinstance(part.get('nodes'), list) and part['nodes'] and all(isinstance(n, str) and n for n in part['nodes']), p['id'] + ': invalid nodes')
            require(part.get('animated', False) in (True, False) and (not part.get('animated') or parts.index(part) == 1),
                    p['id'] + ': only a piece\'s second part may be animated')
            without = part.get('without_materials', [])
            require(isinstance(without, list) and all(isinstance(n, str) and n for n in without), p['id'] + ': invalid without_materials')
        groups = p.get('collision', {}).get('groups', [])
        require(isinstance(groups, list) and all(isinstance(g, str) and g for g in groups), p['id'] + ': invalid collision groups')
        void = p.get('collision', {}).get('void', [])
        require(isinstance(void, list) and all(isinstance(g, str) and g in groups for g in void), p['id'] + ': collision.void names listed groups')
        require(p.get('collision', {}).get('strip_exits', False) in (True, False), p['id'] + ': invalid collision.strip_exits')
        require('origin_y' not in p or (type(p['origin_y']) in (int, float) and v[1] <= p['origin_y'] <= v[4]), p['id'] + ': origin_y lies in the volume')
        d = p.get('distant')
        require(d is None or (isinstance(d, dict) and d.get('archive') == 'sea_LOD%02d.szs' % src['room'] and type(d.get('triangles')) is int and 0 < d['triangles'] <= 2048),
                p['id'] + ': distant names the room\'s own sea_LODNN.szs and its triangle count (1..2048)')
        e = p.get('expect', {})
        require(isinstance(e.get('triangles'), list) and len(e['triangles']) == len(parts) and all(type(t) is int and t > 0 for t in e['triangles']),
                p['id'] + ': expect.triangles must give each part\'s count')
        require(type(e.get('collision_triangles')) is int and e['collision_triangles'] >= 0, p['id'] + ': invalid expect.collision_triangles')
        # each part's source textures (the room's own texture names its materials sample, a few end in a dot): pins the look
        t = e.get('textures')
        require(t is None or (isinstance(t, list) and len(t) == len(parts) and all(isinstance(x, list) and x and all(isinstance(n, str) and re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.]{0,62}', n) for n in x) for x in t)),
                p['id'] + ': expect.textures must give each part\'s texture names')


# ---- the player's files ---------------------------------------------------------------------------

def room_files(game, archive):
    """name -> bytes of the room archive: a loose Stage archive or a member of a permanent pack."""
    common = os.path.join(game, 'content', 'Common')
    loose = os.path.join(common, 'Stage', archive)
    if os.path.isfile(loose):
        with open(loose, 'rb') as f:
            raw = f.read()
    else:
        raw = None
        for pack in PACKS:
            path = os.path.join(common, 'Pack', pack)
            if not os.path.isfile(path):
                continue
            with open(path, 'rb') as f:
                members = dict(base.shaderprep.sarc(f.read()))
            if archive in members:
                raw = members[archive]
                break
        require(raw is not None, 'the room archive is not in your game files')
    return dict(base.shaderprep.sarc(base.shaderprep.yaz0(raw)))


def model_name(index):
    return 'model' if index == 0 else 'model%d' % index


# ---- the room's BG models -------------------------------------------------------------------------

class Shape:
    pass


def bone_matrices(src, skeleton):
    """bone index -> (3x3 world matrix incl. scale, world translation) of a BG model's skeleton"""
    bones = src.rel(skeleton + 0x14)
    nbones = src.u16(skeleton + 8)
    euler = (src.u32(skeleton + 4) >> 12) & 7 == 1
    matrices = {}
    for i in range(nbones):
        b = bones + 0x40 * i
        parent = src.u16(b + 6)
        scale = struct.unpack_from('>3f', src.b, b + 0x14)
        rot = struct.unpack_from('>4f', src.b, b + 0x20)
        tr = struct.unpack_from('>3f', src.b, b + 0x30)
        if euler:
            cx, sx, cy, sy, cz, sz = math.cos(rot[0]), math.sin(rot[0]), math.cos(rot[1]), math.sin(rot[1]), math.cos(rot[2]), math.sin(rot[2])
            r = [[cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz],
                 [cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz],
                 [-sy, sx * cy, cx * cy]]
        else:
            x, y, z, w = rot
            r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                 [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                 [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
        local = [[r[j][k] * scale[k] for k in range(3)] for j in range(3)]
        if parent == 0xFFFF:
            matrices[i] = (local, tr)
        else:
            pm, pt = matrices[parent]
            m = [[sum(pm[j][q] * local[q][k] for q in range(3)) for k in range(3)] for j in range(3)]
            t = tuple(pt[j] + sum(pm[j][q] * tr[q] for q in range(3)) for j in range(3))
            matrices[i] = (m, t)
    return matrices


def read_model(src, name):
    """Shapes of one BG model: vertex records (room-local), indices, node, material."""
    models = dict(src.dict(src.rel(0x20)))
    require(name in models, 'the room has no BG model ' + name)
    model = models[name]
    # each bone's world transform (3x3 and translation); a shape is taken when its bone's world rotation comes out as
    # none (a parent's turn undone by the child's, as Five-Star's pillars): its positions are scaled and moved. A scale
    # that differs between the axes is taken only for shapes without normals (an island's water surfaces)
    matrices = bone_matrices(src, src.rel(model + 0xC))
    world = {}
    for i, (m, t) in matrices.items():
        unturned = all(abs(m[j][k]) < 1e-5 for j in range(3) for k in range(3) if j != k) and all(m[j][j] > 1e-3 for j in range(3))
        world[i] = (tuple(t), tuple(m[j][j] for j in range(3))) if unturned else None
    materials = src.dict(src.rel(model + 0x18))
    vertex_array = src.rel(model + 0x10)
    out = []
    for sname, at in src.dict(src.rel(model + 0x14)):
        s = Shape()
        s.at, s.name = at, sname
        s.node = sname.split('__')[0] if '__' in sname else sname
        s.material = materials[src.u16(at + 0xE)][0]
        require(src.b[at + 0x16] == 0, 'a BG shape is skinned')
        bone = world.get(src.u16(at + 0x10), ((0, 0, 0), (1, 1, 1)))
        if bone is None:  # a shape on a turned bone (effects): never selectable
            continue
        offset, scale = bone
        vertex = vertex_array + 0x20 * src.u16(at + 0x12)
        s.vertex = vertex
        attrs = src.rel(vertex + 0x10)
        s.nattr = src.b[vertex + 4]
        layout = {src.cstr(src.rel(attrs + i * 12)): (src.b[attrs + i * 12 + 4], src.u16(attrs + i * 12 + 6), src.u32(attrs + i * 12 + 8)) for i in range(s.nattr)}
        buffer = src.rel(vertex + 0x18)
        s.stride = src.u16(buffer + 0xC)
        require(src.b[vertex + 5] == 1 and layout.get('_p0', (None, None, None))[2] == 0x811, 'unsupported BG vertex layout in ' + sname)
        for index, at_attr, fmt in layout.values():
            require(index == 0 and fmt in FORMATS and at_attr + FORMATS[fmt] <= s.stride, 'unsupported BG vertex attribute in ' + sname)
        if '_n0' in layout and max(scale) - min(scale) > 1e-4 * max(scale):
            continue  # its normals would need turning as well
        s.at_p = layout['_p0'][1]
        data = src.rel(buffer + 0x14)
        count = src.u32(vertex + 8)
        s.records = []
        s.pos = []
        for k in range(count):
            r = bytearray(src.b[data + s.stride * k:data + s.stride * (k + 1)])
            x, y, z = struct.unpack_from('>3f', r, s.at_p)
            p = (x * scale[0] + offset[0], y * scale[1] + offset[1], z * scale[2] + offset[2])
            struct.pack_into('>3f', r, s.at_p, *p)
            s.records.append(bytes(r))
            s.pos.append(p)
        mesh = src.rel(at + 0x24)
        require(src.u32(mesh) == 4 and src.u32(mesh + 4) in (4, 9), 'a BG mesh is not a triangle list')
        wide = src.u32(mesh + 4) == 9
        n = src.u32(mesh + 8)
        first = src.u32(mesh + 0x18)
        ib = src.rel(src.rel(mesh + 0x14) + 0x14)
        idx = [struct.unpack_from('>I' if wide else '>H', src.b, ib + (4 if wide else 2) * i)[0] + first for i in range(n)]
        s.tris = [tuple(idx[i:i + 3]) for i in range(0, n - 2, 3)]
        out.append(s)
    return model, out, materials


def components(shapes):
    """Welded connected components over the triangles of one node: [[(shape, tri), ...]]."""
    parent = {}

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a, b):
        a, b = find(a), find(b)
        if a != b:
            parent[b] = a

    key = lambda p: (round(p[0], 2), round(p[1], 2), round(p[2], 2))
    for s in shapes:
        for t in s.tris:
            for i in t:
                parent.setdefault(key(s.pos[i]), key(s.pos[i]))
            a = key(s.pos[t[0]])
            for i in t[1:]:
                union(a, key(s.pos[i]))
    groups = {}
    for si, s in enumerate(shapes):
        for ti, t in enumerate(s.tris):
            groups.setdefault(find(key(s.pos[t[0]])), []).append((si, ti))
    return list(groups.values())


def select(shapes, nodes, volume, without=()):
    """Whole components of the nodes inside the volume -> {shape index: [tri, ...]}, crossing count. Shapes of
    the materials in `without` (another object standing in the volume, like a tree) are not looked at."""
    x0, y0, z0, x1, y1, z1 = volume
    inside = lambda p: x0 <= p[0] <= x1 and y0 <= p[1] <= y1 and z0 <= p[2] <= z1
    taken, crossing = {}, 0
    for node in nodes:
        local = [s for s in shapes if s.node == node and s.material not in without]
        require(local, 'the room has no node ' + node)
        for comp in components(local):
            flags = [inside(local[si].pos[i]) for si, ti in comp for i in local[si].tris[ti]]
            if all(flags):
                for si, ti in comp:
                    taken.setdefault(shapes.index(local[si]), []).append(local[si].tris[ti])
            elif any(flags):
                crossing += 1
    return taken, crossing


# ---- the set's BFRES --------------------------------------------------------------------------------

class Writer:
    def __init__(self, data, out):
        self.data, self.src, self.out = data, base.Bfres(data), out

    def block(self, at, size, alignment=4):
        require(0 < at <= len(self.data) - size, 'BG resource span invalid')
        return self.out.append(self.data[at:at + size], alignment)

    def text(self, at):
        return append_string(self.out, self.src.cstr(at))

    def strings(self, dictionary):
        return append_dictionary(self.out, [(n, self.text(v)) for n, v in self.src.dict(dictionary)])


# ---- textures a recipe makes smaller (an oversized texture on a few triangles) --------------------------

def tiled_address(x, y, pitch, bpp, mode, swizzle):
    """Byte address of element (x, y) in a GX2 2D surface: tile modes 1, 2 and 4 (the port's LatteAddrLib cases;
    the same arithmetic as tools/story_mods/lyt/gx2tile.py, one element at a time, without numpy)."""
    if mode == 1:
        return (x + pitch * y) * bpp // 8
    b = lambda v, i: (v >> i) & 1
    if bpp == 64:
        bits = (b(x, 0), b(y, 0), b(x, 1), b(x, 2), b(y, 1), b(y, 2))
    elif bpp == 128:
        bits = (b(y, 0), b(x, 0), b(x, 1), b(x, 2), b(y, 1), b(y, 2))
    else:
        raise BuildError('only block-compressed textures can be made smaller')
    pix = sum(v << i for i, v in enumerate(bits)) * bpp // 8
    if mode == 2:
        return pix + 64 * bpp // 8 * ((x >> 3) + (pitch >> 3) * (y >> 3))
    require(mode == 4, 'unsupported texture tile mode')
    pipe = ((y >> 3) ^ (x >> 3)) & 1
    bank = (y >> 4) & 3
    bank = ((bank >> 1) | (bank << 1)) & 3
    bank = (bank ^ (x >> 3)) & 3
    bank_pipe = ((pipe + 2 * bank) ^ (((swizzle >> 8) & 1) + 2 * ((swizzle >> 9) & 3))) % 8
    pipe, bank = bank_pipe % 2, bank_pipe // 2
    macro_bytes = 32 * 16 * bpp // 8
    off = ((((x // 32) + (pitch // 32) * (y // 16)) * macro_bytes) >> 3) + pix
    off = ((off & ~0xFF) << 3) | (off & 0xFF)
    return off | (pipe << 8) | (bank << 9)


def bc1_texels(block):
    """16 RGBA texels of one BC1 block (little-endian endpoints, as the GPU reads them)."""
    c0, c1, bits = struct.unpack('<HHI', block)
    rgb = lambda c: (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)
    a, b = rgb(c0), rgb(c1)
    if c0 > c1:
        pal = [a + (255,), b + (255,), tuple((2 * a[k] + b[k]) // 3 for k in range(3)) + (255,), tuple((a[k] + 2 * b[k]) // 3 for k in range(3)) + (255,)]
    else:
        pal = [a + (255,), b + (255,), tuple((a[k] + b[k]) // 2 for k in range(3)) + (255,), (0, 0, 0, 0)]
    return [pal[(bits >> (2 * i)) & 3] for i in range(16)]


def smaller_texture(w, texture, limit):
    """A copy of a BC1 texture at most `limit` texels wide and high: the base level decoded, box-filtered and
    stored as one linear RGBA8 sRGB level (the format the port's own model converters write)."""
    src, out = w.src, w.out
    surf = texture + 4
    width, height, fmt, mode, swizzle, pitch = (src.u32(surf + 4), src.u32(surf + 8), src.u32(surf + 0x14), src.u32(surf + 0x30),
                                               src.u32(surf + 0x34), src.u32(surf + 0x3C))
    require(fmt in (0x31, 0x431) and width % 4 == 0 and height % 4 == 0, 'only BC1 textures can be made smaller')
    factor = 1
    while width // factor > limit or height // factor > limit:
        factor *= 2
    require(factor >= 4 and (width // factor) % 64 == 0 and (height // factor) % 8 == 0, 'the texture cannot be made smaller this way')
    data = src.rel(texture + 0xB0)
    wb, hb = width // 4, height // 4
    nw, nh = width // factor, height // factor
    acc = [[0, 0, 0, 0] for _ in range(nw * nh)]
    for by in range(hb):
        for bx in range(wb):
            at = data + tiled_address(bx, by, pitch // 4 if mode == 1 else -(-wb // 32) * 32, 64, mode, swizzle)
            for i, t in enumerate(bc1_texels(bytes(src.b[at:at + 8]))):
                x, y = bx * 4 + (i & 3), by * 4 + (i >> 2)
                a = acc[(y // factor) * nw + x // factor]
                for k in range(4):
                    a[k] += t[k]
    n = factor * factor
    rgba = bytes(min(255, (v + n // 2) // n) for a in acc for v in a)
    dst = w.block(texture, 0xC0)
    for offset in (0xA8, 0xAC):
        target = src.rel(texture + offset)
        out.set_rel(dst + offset, w.text(target)) if target else out.put('i', dst + offset, 0)
    at = out.append(rgba, 0x2000)
    s = dst + 4
    out.put('I', s + 0x04, nw)
    out.put('I', s + 0x08, nh)
    out.put('I', s + 0x10, 1)
    out.put('I', s + 0x14, base.FMT_RGBA8_SRGB)
    out.put('I', s + 0x20, len(rgba))
    out.put('I', s + 0x28, 0)
    out.put('I', s + 0x30, base.TILE_LINEAR_ALIGNED)
    out.put('I', s + 0x34, 0)
    out.put('I', s + 0x38, 0x100)
    out.put('I', s + 0x3C, nw)
    for k in range(13):
        out.put('I', s + 0x40 + 4 * k, 0)
    out.put('I', dst + 0x7C, 1)
    for k in range(5):
        out.put('I', dst + 0x8C + 4 * k, 0)
    out.set_rel(dst + 0xB0, at)
    out.put('i', dst + 0xB4, 0)
    out.put('II', dst + 0xB8, 0, 0)
    return dst, len(rgba)


def copy_texture(w, texture):
    src, out = w.src, w.out
    dst = w.block(texture, 0xC0)
    for offset in (0xA8, 0xAC):
        target = src.rel(texture + offset)
        out.set_rel(dst + offset, w.text(target)) if target else out.put('i', dst + offset, 0)
    size = 0
    for offset, n in ((0xB0, src.u32(texture + 0x24)), (0xB4, src.u32(texture + 0x2C))):
        if n:
            out.set_rel(dst + offset, w.block(src.rel(texture + offset), n, 0x2000))
            size += n
        else:
            out.put('i', dst + offset, 0)
    out.put('II', dst + 0xB8, 0, 0)
    return dst, size


def copy_material(w, material, index, name, shader_archive, textures):
    src, out, data = w.src, w.out, w.data
    dst = w.block(material, 0x4C)
    out.set_rel(dst + 4, append_string(out, name))
    out.put('H', dst + 0xC, index)
    infos = []
    for info_name, value in src.dict(src.rel(material + 0x1C)):
        n, kind = src.u16(value), data[value + 2]
        require(kind in (0, 1), 'unsupported BG render info')
        at = w.block(value, 8 + 4 * n)
        out.set_rel(at + 4, append_string(out, info_name))
        infos.append((info_name, at))
    out.set_rel(dst + 0x1C, append_dictionary(out, infos))
    out.set_rel(dst + 0x20, w.block(src.rel(material + 0x20), 48))
    shader = src.rel(material + 0x24)
    dst_shader = w.block(shader, 28)
    for offset in (0, 4):
        out.set_rel(dst_shader + offset, w.text(src.rel(shader + offset)))
    for offset in (0x10, 0x14, 0x18):
        target = src.rel(shader + offset)
        out.set_rel(dst_shader + offset, w.strings(target)) if target else out.put('i', dst_shader + offset, 0)
    out.set_rel(dst + 0x24, dst_shader)
    ntex, nsamp = data[material + 0x10], data[material + 0x11]
    refs = src.rel(material + 0x28)
    dst_refs = w.block(refs, 8 * ntex)
    for i in range(ntex):
        t = src.cstr(src.rel(refs + 8 * i))
        out.set_rel(dst_refs + 8 * i, append_string(out, t))
        out.set_rel(dst_refs + 8 * i + 4, textures[t])
    out.set_rel(dst + 0x28, dst_refs)
    samplers = src.rel(material + 0x2C)
    dst_samplers = w.block(samplers, 24 * nsamp)
    entries = []
    for sname, value in src.dict(src.rel(material + 0x30)):
        at = dst_samplers + value - samplers
        out.set_rel(at + 0xC, append_string(out, sname))
        entries.append((sname, at))
    out.set_rel(dst + 0x2C, dst_samplers)
    out.set_rel(dst + 0x30, append_dictionary(out, entries))
    params = src.rel(material + 0x34)
    count = src.u16(material + 0x12)
    dst_params = w.block(params, count * 20) if count else 0
    entries = []
    for pname, value in src.dict(src.rel(material + 0x38)):
        at = dst_params + value - params
        out.set_rel(at + 16, append_string(out, pname))
        entries.append((pname, at))
    if count:
        out.set_rel(dst + 0x34, dst_params)
    else:
        out.put('i', dst + 0x34, 0)
    out.set_rel(dst + 0x38, append_dictionary(out, entries))
    source = src.rel(material + 0x3C)
    out.set_rel(dst + 0x3C, w.block(source, src.u16(material + 0x16))) if source and src.u16(material + 0x16) else out.put('i', dst + 0x3C, 0)
    volatile = src.rel(material + 0x44)
    out.set_rel(dst + 0x44, w.block(volatile, (count + 7) // 8)) if volatile and count else out.put('i', dst + 0x44, 0)
    out.put('i', dst + 0x40, 0)
    out.put('H', dst + 0x1A, 0)
    out.put('I', dst + 0x48, 0)
    return dst


def write_model(w, model_at, name, shapes, taken, materials, textures, center):
    """One output FMDL `name` with the taken triangles of `shapes`, positions relative to `center`."""
    src, out = w.src, w.out
    dst_model = w.block(model_at, 0x30)
    out.set_rel(dst_model + 4, append_string(out, name))
    out.put('i', dst_model + 8, 0)
    out.put('i', dst_model + 0x1C, 0)
    skeleton = src.rel(model_at + 0xC)
    dst_skeleton = w.block(skeleton, 0x24)
    out.put('HHH', dst_skeleton + 8, 1, 0, 0)
    dst_bone = w.block(src.rel(skeleton + 0x14), 0x40)
    out.set_rel(dst_bone, append_string(out, 'root'))
    out.put('HHHH', dst_bone + 4, 0, 0xFFFF, 0xFFFF, 0xFFFF)
    out.put('3f', dst_bone + 0x14, 1, 1, 1)
    out.put('4f', dst_bone + 0x20, 0, 0, 0, 1)
    out.put('3f', dst_bone + 0x30, 0, 0, 0)
    out.set_rel(dst_skeleton + 0x10, append_dictionary(out, [('root', dst_bone)]))
    out.set_rel(dst_skeleton + 0x14, dst_bone)
    for offset in (0x18, 0x1C, 0x20):
        out.put('i', dst_skeleton + offset, 0)
    out.set_rel(dst_model + 0xC, dst_skeleton)
    order = sorted(taken)
    used_materials = []
    for si in order:
        if shapes[si].material not in used_materials:
            used_materials.append(shapes[si].material)
    material_at = dict(materials)
    dst_materials = [(m, copy_material(w, material_at[m], i, m, name, textures)) for i, m in enumerate(used_materials)]
    out.set_rel(dst_model + 0x18, append_dictionary(out, dst_materials))
    dst_vertex_array = out.append(bytes(0x20 * len(order)), 4)
    out_shapes, vertices, triangles = [], 0, 0
    for index, si in enumerate(order):
        s = shapes[si]
        tris = taken[si]
        used = sorted({k for t in tris for k in t})
        remap = {k: j for j, k in enumerate(used)}
        indices = [remap[k] for t in tris for k in t]
        dst_vertex = dst_vertex_array + 0x20 * index
        out.b[dst_vertex:dst_vertex + 0x20] = w.data[s.vertex:s.vertex + 0x20]
        out.put('H', dst_vertex + 6, index)
        out.put('I', dst_vertex + 8, len(used))
        out.b[dst_vertex + 0xC] = 0
        attributes = src.rel(s.vertex + 0x10)
        dst_attributes = w.block(attributes, 12 * s.nattr)
        attr_entries = []
        for i in range(s.nattr):
            aname = src.cstr(src.rel(attributes + i * 12))
            out.set_rel(dst_attributes + i * 12, append_string(out, aname))
            attr_entries.append((aname, dst_attributes + i * 12))
        out.set_rel(dst_vertex + 0x10, dst_attributes)
        out.set_rel(dst_vertex + 0x14, append_dictionary(out, attr_entries))
        out.put('I', dst_vertex + 0x1C, 0)
        moved = bytearray()
        pos = []
        for k in used:
            r = bytearray(s.records[k])
            p = tuple(s.pos[k][j] - center[j] for j in range(3))
            struct.pack_into('>3f', r, s.at_p, *p)
            moved += r
            pos.append(p)
        dst_buffer = w.block(src.rel(s.vertex + 0x18), 24)
        out.put('I', dst_buffer + 4, len(moved))
        out.set_rel(dst_buffer + 0x14, out.append(bytes(moved), 0x1000))
        out.set_rel(dst_vertex + 0x18, dst_buffer)
        dst_shape = w.block(s.at, 0x40)
        sname = 'shape%d' % index
        out.set_rel(dst_shape + 4, append_string(out, sname))
        out.put('HHHH', dst_shape + 0xC, index, used_materials.index(s.material), 0, index)
        out.put('H', dst_shape + 0x14, 0)
        out.b[dst_shape + 0x16] = 0
        out.b[dst_shape + 0x17] = 1
        out.b[dst_shape + 0x18] = 0
        out.b[dst_shape + 0x19] = 0
        out.put('i', dst_shape + 0x28, 0)
        out.put('i', dst_shape + 0x2C, 0)
        out.put('I', dst_shape + 0x3C, 0)
        out.set_rel(dst_shape + 0x20, dst_vertex)
        dst_mesh = w.block(src.rel(s.at + 0x24), 28)
        index_bytes = struct.pack('>%dI' % len(indices), *indices)
        out.put('II', dst_mesh, 4, 9)
        out.put('I', dst_mesh + 8, len(indices))
        out.put('H', dst_mesh + 0xC, 1)
        out.put('I', dst_mesh + 0x18, 0)
        out.set_rel(dst_mesh + 0x10, out.append(struct.pack('>II', 0, len(indices)), 4))
        dst_index = w.block(src.rel(src.rel(s.at + 0x24) + 0x14), 24)
        out.put('I', dst_index + 4, len(index_bytes))
        out.set_rel(dst_index + 0x14, out.append(index_bytes, 0x1000))
        out.set_rel(dst_mesh + 0x14, dst_index)
        out.set_rel(dst_shape + 0x24, dst_mesh)
        lo = [min(p[k] for p in pos) for k in range(3)]
        hi = [max(p[k] for p in pos) for k in range(3)]
        mid = [(a + b) * .5 for a, b in zip(lo, hi)]
        ext = [(b - a) * .5 for a, b in zip(lo, hi)]
        out.put('f', dst_shape + 0x1C, sum(v * v for v in ext) ** .5)
        out.set_rel(dst_shape + 0x30, out.append(struct.pack('>6H', 0, 0, 0, 0, 0, 1), 4))
        out.set_rel(dst_shape + 0x34, out.append(struct.pack('>6f', *mid, *ext), 4))
        out.set_rel(dst_shape + 0x38, out.append(struct.pack('>H', 0), 4))
        out_shapes.append((sname, dst_shape))
        vertices += len(used)
        triangles += len(tris)
    out.set_rel(dst_model + 0x10, dst_vertex_array)
    out.set_rel(dst_model + 0x14, append_dictionary(out, out_shapes))
    out.put('HHHH', dst_model + 0x20, len(order), len(order), len(used_materials), 0)
    out.put('I', dst_model + 0x28, vertices)
    return dst_model, triangles, used_materials


def copy_animation(w, mname, used_materials, name, dst_model):
    """The room's texture animation of BG model `mname`, its rows for `used_materials` only, as the animation `name`
    of the output model (an island's water: its ripples and flow). None when none of them moves."""
    src, out = w.src, w.out
    entries = dict(src.dict(src.rel(0x34))) if src.rel(0x34) else {}
    require(mname in entries, 'the room has no texture animation for ' + mname)
    animation = entries[mname]
    require(src.b[animation:animation + 4] == b'FSHU' and 1 < src.u32(animation + 0x10) <= 1000, 'unsupported room texture animation')
    rows = src.rel(animation + 0x2C)
    row_of = {src.cstr(src.rel(rows + i * 32 + 0x10)): rows + i * 32 for i in range(src.u16(animation + 0x14))}
    kept = [(m, row_of[m]) for m in used_materials if m in row_of]
    if not kept:
        return None

    def pointer(dst, original, offset, size):
        target = src.rel(original + offset)
        out.set_rel(dst + offset, w.block(target, size)) if target else out.put('i', dst + offset, 0)
    dst_animation = w.block(animation, 0x34)
    out.set_rel(dst_animation + 4, append_string(out, name))
    out.put('i', dst_animation + 8, 0)
    out.put('H', dst_animation + 0x14, len(kept))
    out.put('H', dst_animation + 0x16, 0)
    out.set_rel(dst_animation + 0x24, dst_model)
    out.set_rel(dst_animation + 0x28, out.append(struct.pack('>%dH' % len(kept), *([0xFFFF] * len(kept))), 4))
    out.put('i', dst_animation + 0x30, 0)
    dst_rows = [w.block(row, 32) for _, row in kept]
    require(all(dst_rows[i] == dst_rows[0] + 32 * i for i in range(len(kept))), 'room animation rows not contiguous')
    params_total = curves_total = 0
    for (material, row), dst_row in zip(kept, dst_rows):
        # where this row's curves and results begin, after the rows before it (as the game's own animations count them)
        out.put('II', dst_row + 8, curves_total, params_total * 4)
        out.set_rel(dst_row + 0x10, append_string(out, material))
        param_count, curve_count, constant_count = src.u16(row), src.u16(row + 2), src.u16(row + 4)
        params_total += param_count
        curves_total += curve_count
        params = src.rel(row + 0x14)
        dst_params = w.block(params, param_count * 16)
        for i in range(param_count):
            out.set_rel(dst_params + i * 16 + 12, w.text(src.rel(params + i * 16 + 12)))
        out.set_rel(dst_row + 0x14, dst_params)
        pointer(dst_row, row, 0x1C, constant_count * 8)
        curves = src.rel(row + 0x18)
        if curve_count:
            dst_curves = w.block(curves, curve_count * 36)
            for i in range(curve_count):
                original, dst = curves + i * 36, dst_curves + i * 36
                require(src.u16(original) == 0, 'unsupported room animation curve encoding')
                keys = src.u16(original + 2)
                pointer(dst, original, 28, keys * 4)
                pointer(dst, original, 32, keys * 16)
            out.set_rel(dst_row + 0x18, dst_curves)
        else:
            out.put('i', dst_row + 0x18, 0)
    out.put('I', dst_animation + 0x18, params_total * 4)
    out.put('I', dst_animation + 0x1C, curves_total)
    # the baked size, as the game's own animations have it: every curve, a value per frame and its header
    out.put('I', dst_animation + 0x20, curves_total * (src.u32(animation + 0x10) * 4 + 20))
    out.set_rel(dst_animation + 0x2C, dst_rows[0])
    return dst_animation


# ---- collision --------------------------------------------------------------------------------------

def read_dzb(d):
    nv, ov, nt, ot, nb, ob, nn, on, ng, og, ni, oi = struct.unpack_from('>12I', d, 0)
    require(nv <= 65535 and nt <= 200000 and ng <= 4096 and ni <= 65535, 'room collision too large')
    verts = [struct.unpack_from('>3f', d, ov + 12 * i) for i in range(nv)]
    tris = [struct.unpack_from('>5H', d, ot + 10 * i) for i in range(nt)]
    names, group_info = [], []
    for i in range(ng):
        no = struct.unpack_from('>I', d, og + 0x34 * i)[0]
        names.append(d[no:d.index(b'\0', no)].decode('ascii', 'replace'))
        group_info.append(struct.unpack_from('>I', d, og + 0x34 * i + 0x30)[0])
    infos = [struct.unpack_from('>4I', d, oi + 16 * i) for i in range(ni)]
    return verts, tris, names, infos, group_info


def triangle_surface(pts, code, water, void=False, lava=False):
    """The world-object surface (col_*) of one room collision triangle: its raw attribute code's class on a floor
    (stone for the other classes), a plain wall (wood stays wood) on a slope steeper than 60 degrees; the group's
    water bit makes it swimmable water, its lava bit lava's surface and a void group a void floor (a floor of the lava
    class, the bottom under the game's lava, is lava too). None for a collapsed triangle."""
    u = [pts[1][k] - pts[0][k] for k in range(3)]
    v = [pts[2][k] - pts[0][k] for k in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    length = sum(x * x for x in n) ** .5
    if length < 1e-6:
        return None
    floor = n[1] / length > 0.5
    surface = SURFACES.get(code, 'col_stone' if floor else 'col_wall')
    if not floor and surface != 'col_wall':
        surface = 'col_wood' if surface == 'col_wood' else 'col_wall'
    if water:
        surface = 'col_water'
    elif lava:
        surface = 'col_lava' if floor else 'col_wall'
    elif void:
        surface = 'col_void' if floor else 'col_wall'
    return surface


def piece_collision(dzb, piece, room_origin, center):
    """WCOL bytes, metadata and the kept triangle count of one piece."""
    verts, tris, names, infos, group_info = dzb
    groups = set(piece.get('collision', {}).get('groups', []))
    void = set(piece.get('collision', {}).get('void', []))
    strip_exits = piece.get('collision', {}).get('strip_exits', False)
    if not groups:
        return None, {}, 0
    x0, y0, z0, x1, y1, z1 = piece['volume']
    local = lambda p: (p[0] - room_origin[0], p[1] - room_origin[1], p[2] - room_origin[2])
    inside = lambda p: x0 <= p[0] <= x1 and y0 <= p[1] <= y1 and z0 <= p[2] <= z1
    out_vertices, out_tris, remap = [], [], {}
    for a, b, c, info, group in tris:
        if names[group] not in groups:
            continue
        pts = [local(verts[k]) for k in (a, b, c)]
        if not all(inside(p) for p in pts):
            continue
        i0, i1 = infos[info][0], infos[info][1]
        code = (i1 >> 16) & 0x1F
        exit_id = (i0 >> 13) & 0x3F
        if strip_exits and exit_id != 0x3F and names[group] not in void:
            continue
        surface = triangle_surface(pts, code, group_info[group] & WATER_GROUP, names[group] in void, group_info[group] & LAVA_GROUP)
        if surface is None:
            continue
        row = []
        for k, p in zip((a, b, c), pts):
            if k not in remap:
                remap[k] = len(out_vertices)
                out_vertices.append(tuple(p[j] - center[j] for j in range(3)))
            row.append(remap[k])
        out_tris.append((row[0], row[1], row[2], collision_mesh.SEMANTICS[surface]))
    if not out_tris:
        return None, {}, 0

    class Api:
        @staticmethod
        def require(ok, message):
            require(ok, message)
    body, info = collision_mesh.encode(out_vertices, out_tris, Api)
    return body, info, len(out_tris)


# ---- the set ----------------------------------------------------------------------------------------

def build_set(game, recipe, identity, check_expect=True):
    check_recipe(recipe)
    require(re.fullmatch(r'[a-z][a-z0-9_]{0,30}', identity) is not None, 'invalid set identity')
    src_info = recipe['source']
    files = room_files(game, src_info['archive'])
    room = src_info['room']
    bfres_name = 'Room%d.bfres' % room
    require(bfres_name in files, 'the room archive has no room model')
    data = files[bfres_name]
    src = base.Bfres(data)
    room_origin = ((((room - 1) % 7) - 3) * 100000.0, 0.0, (((room - 1) // 7) - 3) * 100000.0)
    externals = dict(src.dict(src.rel(0x20 + 4 * 11))) if src.rel(0x20 + 4 * 11) else {}
    dzb = None
    if 'room.dzb' in externals:
        at = externals['room.dzb']
        dzb = read_dzb(bytes(data[src.rel(at):src.rel(at) + src.u32(at + 4)]))
    out = base.Bfres(data[:0x70])
    out.b[0x18:0x68] = bytes(0x50)
    w = Writer(data, out)
    loaded = {}
    # textures: every texture the selected materials use, once for the whole set
    plan = []
    for pi, piece in enumerate(recipe['pieces']):
        parts = []
        for part in piece['parts']:
            mname = model_name(part['model'])
            if mname not in loaded:
                loaded[mname] = read_model(src, mname)
            model_at, shapes, materials = loaded[mname]
            taken, crossing = select(shapes, part['nodes'], piece['volume'], part.get('without_materials', []))
            require(taken, piece['id'] + ': nothing of ' + '/'.join(part['nodes']) + ' lies wholly inside the volume')
            parts.append((part, mname, model_at, shapes, materials, taken, crossing))
        plan.append((piece, parts))
    texture_names = []
    for piece, parts in plan:
        for part, mname, model_at, shapes, materials, taken, crossing in parts:
            mat_at = dict(materials)
            for si in taken:
                m = mat_at[shapes[si].material]
                refs = src.rel(m + 0x28)
                for i in range(src.b[m + 0x10]):
                    t = src.cstr(src.rel(refs + 8 * i))
                    if t not in texture_names:
                        texture_names.append(t)
    all_textures = dict(src.dict(src.rel(0x24)))
    textures, texture_bytes, per_texture = {}, 0, {}
    smaller = recipe.get('textures', {})
    for t in texture_names:
        require(t in all_textures, 'a material names a missing texture')
        if t in smaller:
            textures[t], size = smaller_texture(w, all_textures[t], smaller[t]['max_size'])
        else:
            textures[t], size = copy_texture(w, all_textures[t])
        texture_bytes += size
        per_texture[t] = size
    out.set_rel(0x24, append_dictionary(out, [(t, textures[t]) for t in texture_names]))
    out.put('H', 0x52, len(texture_names))
    models, shaders, pieces_meta, collisions, distant_archives, animations = [], {}, [], {}, {}, []
    for pi, (piece, parts) in enumerate(plan):
        body = parts[0]
        # the piece's origin: the middle of its first part, at its lowest point
        pts = [body[3][si].pos[k] for si, tris in body[5].items() for t in tris for k in t]
        lo = [min(p[k] for p in pts) for k in range(3)]
        hi = [max(p[k] for p in pts) for k in range(3)]
        center = [(lo[0] + hi[0]) / 2, piece.get('origin_y', lo[1]), (lo[2] + hi[2]) / 2]
        meta_parts, counts = [], []
        piece_lo, piece_hi = [1e30] * 3, [-1e30] * 3
        for k, (part, mname, model_at, shapes, materials, taken, crossing) in enumerate(parts):
            name = 'gp%02d%s' % (pi, 'abcd'[k])
            dst, tris, used = write_model(w, model_at, name, shapes, taken, materials, textures, center)
            models.append((name, dst))
            shader_file = mname + '.sharcfb'
            require(shader_file in files, 'the room archive has no ' + shader_file)
            # the game finds a model's shader archive by the model's name: every model has its own copy
            shaders[name + '.sharcfb'] = files[shader_file]
            counts.append(tris)
            for si, ts in taken.items():
                for t in ts:
                    for i in t:
                        p = shapes[si].pos[i]
                        for j in range(3):
                            piece_lo[j] = min(piece_lo[j], p[j] - center[j])
                            piece_hi[j] = max(piece_hi[j], p[j] - center[j])
            mat_at = dict(materials)
            sampled = sorted({src.cstr(src.rel(src.rel(mat_at[shapes[si].material] + 0x28) + 8 * i)) for si in taken for i in range(src.b[mat_at[shapes[si].material] + 0x10])})
            meta_parts.append({'model': name, 'light': LIGHTS[part['light']], 'triangles': tris, 'materials': used, 'textures': sampled, 'crossing_left_out': crossing})
            if part.get('animated'):
                # the part's texture animation, named as the part's model (the game loads it as <model>.btk)
                animation = copy_animation(w, mname, used, name, dst)
                require(animation is not None, piece['id'] + ': none of the animated part\'s materials moves')
                animations.append((name, animation))
                meta_parts[-1]['animation'] = name
        expect = piece['expect']
        require(not check_expect or counts == expect['triangles'], '%s: the parts have %s triangles, the recipe expects %s (another game version?)' % (piece['id'], counts, expect['triangles']))
        if check_expect and 'textures' in expect:
            got = [m['textures'] for m in meta_parts]
            require(got == [sorted(t) for t in expect['textures']], '%s: the parts sample %s, the recipe expects %s (another game version?)' % (piece['id'], got, expect['textures']))
        col, col_info, col_tris = (None, {}, 0)
        if dzb:
            col, col_info, col_tris = piece_collision(dzb, piece, room_origin, center)
        require(not check_expect or col_tris == expect['collision_triangles'], '%s: %d collision triangles, the recipe expects %d' % (piece['id'], col_tris, expect['collision_triangles']))
        if col:
            collisions[piece['id']] = col
        entry = {'id': piece['id'], 'parts': meta_parts, 'origin': center, 'minimum': piece_lo, 'maximum': piece_hi, 'collision': col_info}
        if piece.get('distant'):
            # the distant model in an archive of its own (as the game keeps it), so a neighbouring square
            # loads only the small distant view, not the whole island
            d_identity = distant_identity(identity, pi)
            distant_archives[piece['id']], entry['distant'] = build_distant(game, piece, d_identity, center, check_expect)
        pieces_meta.append(entry)
    out.set_rel(0x20, append_dictionary(out, models))
    out.put('H', 0x50, len(models))
    if animations:
        out.set_rel(0x34, append_dictionary(out, animations))
        out.put('H', 0x5A, len(animations))
    out.set_rel(0x14, append_string(out, identity))
    out.append(b'', 0x1000)
    out.put('I', 0xC, len(out.b))
    bfres = bytes(out.b)
    members = {identity + '.bfres': bfres}
    members.update(shaders)
    packed = base.sarc(members)
    require(len(packed) <= MAX_ARCHIVE, 'the set archive exceeds 12 MiB')
    meta = {'set': recipe['set'], 'identity': identity, 'pieces': pieces_meta, 'models': [m for m, _ in models],
            'texture_bytes': texture_bytes, 'textures': per_texture, 'shader_bytes': sum(len(v) for v in shaders.values()),
            'bfres_bytes': len(bfres), 'archive_bytes': len(packed)}
    return packed, collisions, meta, distant_archives


def distant_identity(identity, index):
    """the resource identity of a set's piece's distant archive: "gd", the set's digits, the piece's number"""
    return 'gd' + identity[2:29] + '%02x' % index


def build_distant(game, piece, identity, center, check_expect=True):
    """(packed archive, metadata) of a piece's distant model: the island's whole model in the room's distant-view
    archive (sea_LODNN.szs), positions relative to the piece's origin, with its own textures and shader archive"""
    d = piece['distant']
    lod_files = room_files(game, d['archive'])
    bfres = [n for n in lod_files if n.endswith('.bfres')]
    require(len(bfres) == 1 and 'model.sharcfb' in lod_files, piece['id'] + ': the distant-view archive has no model')
    data = lod_files[bfres[0]]
    out = base.Bfres(data[:0x70])
    out.b[0x18:0x68] = bytes(0x50)
    w = Writer(data, out)
    model_at, shapes, materials = read_model(w.src, 'model')
    taken = {si: list(sh.tris) for si, sh in enumerate(shapes) if sh.tris}
    require(taken, piece['id'] + ': the distant model is empty')
    all_textures = dict(w.src.dict(w.src.rel(0x24)))
    mat_at = dict(materials)
    names, textures, texture_bytes = [], {}, 0
    for si in taken:
        m = mat_at[shapes[si].material]
        refs = w.src.rel(m + 0x28)
        for i in range(w.src.b[m + 0x10]):
            t = w.src.cstr(w.src.rel(refs + 8 * i))
            if t not in textures:
                require(t in all_textures, 'a distant material names a missing texture')
                textures[t], size = copy_texture(w, all_textures[t])
                names.append(t)
                texture_bytes += size
    out.set_rel(0x24, append_dictionary(out, [(t, textures[t]) for t in names]))
    out.put('H', 0x52, len(names))
    name = identity[:2] + identity[-2:] + 'd'
    dst, tris, used = write_model(w, model_at, name, shapes, taken, materials, textures, center)
    require(not check_expect or tris == d['triangles'], '%s: the distant model has %d triangles, the recipe expects %d' % (piece['id'], tris, d['triangles']))
    out.set_rel(0x20, append_dictionary(out, [(name, dst)]))
    out.put('H', 0x50, 1)
    out.set_rel(0x14, append_string(out, identity))
    out.append(b'', 0x1000)
    out.put('I', 0xC, len(out.b))
    packed = base.sarc({identity + '.bfres': bytes(out.b), name + '.sharcfb': lod_files['model.sharcfb']})
    pts = [shapes[si].pos[k] for si, ts in taken.items() for t in ts for k in t]
    meta = {'archive': identity, 'model': name, 'light': LIGHTS['bg0'], 'triangles': tris, 'materials': used, 'texture_bytes': texture_bytes,
            'archive_bytes': len(packed),
            'minimum': [min(q[j] for q in pts) - center[j] for j in range(3)],
            'maximum': [max(q[j] for q in pts) - center[j] for j in range(3)]}
    return packed, meta


def cached_meta(out, key):
    """The previous build's metadata when every output is there and unchanged; otherwise None (rebuild)."""
    try:
        if os.path.getsize(out + '.json') > 256 * 1024 or os.path.getsize(out) > 2 * MAX_ARCHIVE:
            return None
        with open(out + '.json') as f:
            meta = json.load(f)
        if meta.get('cache_key') != key or meta.get('converter_version') != VERSION:
            return None
        with open(out, 'rb') as f:
            if hashlib.sha256(f.read()).hexdigest() != meta.get('archive_sha256'):
                return None
        for piece in meta.get('pieces', []):
            name = piece.get('collision_file')
            if name:
                with open(os.path.join(os.path.dirname(os.path.abspath(out)), name), 'rb') as f:
                    if hashlib.sha256(f.read()).hexdigest() != piece['collision'].get('collision_sha256'):
                        return None
            d = piece.get('distant')
            if d:
                with open(os.path.join(os.path.dirname(os.path.abspath(out)), d['file']), 'rb') as f:
                    if hashlib.sha256(f.read()).hexdigest() != d.get('archive_sha256'):
                        return None
        return meta
    except (OSError, ValueError, TypeError, AttributeError, KeyError):
        return None


def build(game, recipe_path, identity, out, check_expect=True):
    with open(recipe_path, 'rb') as f:
        recipe_bytes = f.read(256 * 1024 + 1)
    require(len(recipe_bytes) <= 256 * 1024, 'recipe too large')
    try:
        recipe = json.loads(recipe_bytes)
    except ValueError as exc:
        raise BuildError('the recipe is not JSON') from exc
    digest = hashlib.sha256()
    for path in (__file__, collision_mesh.__file__):
        with open(path, 'rb') as f:
            digest.update(f.read())
    digest.update(identity.encode() + b'\0' + recipe_bytes)
    room = room_files(game, recipe.get('source', {}).get('archive', ''))
    for name in sorted(room):
        digest.update(name.encode() + b'\0' + hashlib.sha256(room[name]).digest())
    for archive in sorted({p['distant']['archive'] for p in recipe.get('pieces', []) if isinstance(p, dict) and isinstance(p.get('distant'), dict) and
                           isinstance(p['distant'].get('archive'), str) and re.fullmatch(r'sea_LOD[0-9]{2}\.szs', p['distant']['archive'])}):
        lod = room_files(game, archive)
        for name in sorted(lod):
            digest.update(archive.encode() + b'/' + name.encode() + b'\0' + hashlib.sha256(lod[name]).digest())
    if check_expect:
        cached = cached_meta(out, digest.hexdigest())
        if cached:
            return cached
    packed, collisions, meta, distant_archives = build_set(game, recipe, identity, check_expect)
    archive = base.yaz0_stored(packed)
    meta.update(converter_version=VERSION, cache_key=digest.hexdigest(), archive_sha256=hashlib.sha256(archive).hexdigest())
    directory = os.path.dirname(os.path.abspath(out))
    os.makedirs(directory, exist_ok=True)

    def publish(path, body):
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=directory, prefix='.game-piece-', delete=False) as target:
                temporary = target.name
                target.write(body)
            os.replace(temporary, path)
            temporary = None
        finally:
            if temporary is not None:
                os.unlink(temporary)
    publish(out, archive)
    for piece in meta['pieces']:
        col = collisions.get(piece['id'])
        if col:
            path = '%s.%s.wcol' % (out, piece['id'])
            publish(path, col)
            piece['collision_file'] = os.path.basename(path)  # next to the archive (the cache may move)
        if piece['id'] in distant_archives:
            body = base.yaz0_stored(distant_archives[piece['id']])
            path = '%s.%s.distant.szs' % (out, piece['id'])
            publish(path, body)
            piece['distant'].update(file=os.path.basename(path), archive_sha256=hashlib.sha256(body).hexdigest())
    # the metadata commits the generation last
    publish(out + '.json', (json.dumps(meta, sort_keys=True) + '\n').encode())
    return meta


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', required=True)
    parser.add_argument('--recipe', required=True)
    parser.add_argument('--identity', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--measure', action='store_true', help='recipe authoring: report the counts instead of checking expect')
    args = parser.parse_args()
    try:
        meta = build(args.game, args.recipe, args.identity, args.out, not args.measure)
    except (BuildError, OSError, KeyError, IndexError, struct.error, ValueError) as exc:
        parser.exit(1, 'game_piece_model: %s\n' % exc)
    print(json.dumps(meta, sort_keys=True))


if __name__ == '__main__':
    main()
