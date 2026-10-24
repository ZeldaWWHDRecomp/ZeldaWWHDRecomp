# SPDX-License-Identifier: MPL-2.0
"""Kit materials: a game room's own materials for an original model, read from the player's game files.

A kit room is a room of the game ({"stage": "sea", "room": 13} is Dragon Roost Island). Its materials are those
of its background (BG) models ("model": the ground and buildings, "model1": water and shore, "model3": window
glows ...), each with its textures, its shader (the BG model's shader archive), its render state and, where the
room animates it, its texture SRT animation. An original model names a kit material per glTF material; its
triangles are then drawn as the room draws that material: the model's vertices are written in the kit material's
own vertex layout (position, normal, texture coordinates, vertex colour) and the material, its textures and its
shader archive are the room's.

Two users share this code:
  stage_builder.py   mod stages: a room archive is the kit room's archive with its models replaced;
  actor_model.py     world objects (a mod model on the sea, its distant model): a model archive of its own with the
                     used kit materials, their textures and texture animations, next to the model's own-texture
                     materials (docs/world-objects.md#game-materials).

Nothing of the game is in this file or in a mod: the names are references; the materials, textures and shaders
are read from the player's installation at build time and the output stays in the player's cache.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notebook_model as base  # noqa: E402
import game_piece_model as gp  # noqa: E402

BuildError = base.BuildError
require = gp.require
# a BG model's light set (settingTevStruct BG0..BG3), by the model's name, as the island draws it (daBg_Draw)
LIGHTS = {'model': 1, 'model1': 2, 'model2': 3, 'model3': 4}
STAGE = re.compile(r'[A-Za-z0-9_]{1,8}')
# Blender names a second copy of a material "<name>.001"; the copy is the same game material
BLENDER_COPY = re.compile(r'\.\d{3}$')
DOCS = 'docs/world-objects.md#game-materials'


# ---- the kit room --------------------------------------------------------------------------------------------

def room_model(game, stage, room):
    """(files, src) of a kit room: its archive's members (name -> bytes) and its room model (notebook_model.Bfres)"""
    require(isinstance(stage, str) and STAGE.fullmatch(stage) is not None and isinstance(room, int) and 0 <= room <= 63,
            'a kit room is a stage name and a room number (0..63), for example {"stage": "sea", "room": 13}')
    try:
        files = gp.room_files(game, '%s_Room%d.szs' % (stage, room))
    except BuildError:
        raise BuildError('the kit room %s room %d is not in your game files' % (stage, room))
    bf = [n for n in files if n.endswith('.bfres')]
    require(len(bf) == 1, 'the kit room %s room %d has no room model' % (stage, room))
    return files, base.Bfres(files[bf[0]])


def kit_materials(src):
    """material name -> (kit model name, the first shape drawn with it)"""
    out = {}
    for mname, _ in src.dict(src.rel(0x20)):
        _, shapes, _ = gp.read_model(src, mname)
        for s in shapes:
            out.setdefault(s.material, (mname, s))
    return out


def main_material(src):
    """the kit room's main ground material: the one most of its triangles are drawn with (the alphabetically first
    is often a distant backdrop's, a cut-out picture of mountains or clouds made for one strip of the sky)"""
    count = {}
    for mname, _ in src.dict(src.rel(0x20)):
        for s in gp.read_model(src, mname)[1]:
            count[s.material] = count.get(s.material, 0) + len(s.tris)
    return max(sorted(count), key=lambda m: count[m])


def list_materials(game, stage, room):
    """a kit room's materials as a model can use them: name, the BG model it belongs to, the textures' names, how
    many of the room's triangles use it and the texture coordinates the room's own models give it (a range inside
    0..1 on one axis means the texture is an atlas: several surfaces in bands, see docs/mod-stages.md). Names and
    numbers only."""
    _, src = room_model(game, stage, room)
    out = {}
    main = None
    for mname, _ in src.dict(src.rel(0x20)):
        _, shapes, materials = gp.read_model(src, mname)
        mats = dict(materials)
        for s in shapes:
            e = out.get(s.material)
            if e is None:
                mat = mats[s.material]
                refs = src.rel(mat + 0x28)
                textures = [src.cstr(src.rel(refs + 8 * i)) for i in range(src.b[mat + 0x10])]
                e = out[s.material] = {'material': s.material, 'model': mname, 'textures': textures, 'triangles': 0, 'uv_min': None, 'uv_max': None}
            e['triangles'] += len(s.tris)
            attrs = src.rel(s.vertex + 0x10)
            layout = {src.cstr(src.rel(attrs + i * 12)): (src.u16(attrs + i * 12 + 6), src.u32(attrs + i * 12 + 8)) for i in range(s.nattr)}
            if layout.get('_u0', (0, 0))[1] in (0x80D, 0x808):
                at, fmt = layout['_u0']
                for r in s.records:
                    uv = struct.unpack_from('>2f' if fmt == 0x80D else '>2e', r, at)
                    e['uv_min'] = [round(min(a, b), 2) for a, b in zip(e['uv_min'] or uv, uv)]
                    e['uv_max'] = [round(max(a, b), 2) for a, b in zip(e['uv_max'] or uv, uv)]
    if out:
        main = main_material(src)
    return {'materials': sorted(out.values(), key=lambda e: -e['triangles']), 'main': main}


# ---- names -----------------------------------------------------------------------------------------------------

class Names:
    """How a model's material names become kit materials: `mapping` ({"<model material>": "<kit material>"}) first,
    then the model material's own name; a Blender copy suffix (".001") is ignored on both. `default` (optional)
    takes every texture-free material that names nothing else."""

    def __init__(self, src, stage, room, mapping=None, default=None):
        self.stage, self.room = stage, room
        self.materials = kit_materials(src)
        require(self.materials, 'the kit room %s room %d has no background model' % (stage, room))
        self.mapping = dict(mapping or {})
        self.default = default
        for kit_name in list(self.mapping.values()) + ([default] if default else []):
            require(kit_name in self.materials, self.unknown(kit_name))

    def resolve(self, name):
        """the kit material a model material names, or None"""
        name = name or ''
        for n in (name, BLENDER_COPY.sub('', name)):
            if n in self.mapping:
                return self.mapping[n]
        for n in (name, BLENDER_COPY.sub('', name)):
            if n in self.materials:
                return n
        return None

    def unknown(self, name, label=''):
        """the message for a name that is no material of the kit room: what the room has and where names come from"""
        return ('%s"%s" is not a material of the kit room (%s room %d). It has: %s. The exact names are those of the '
                'room\'s materials in the editor\'s export_glb and list_kit_materials (%s)' % (
                    label, name, self.stage, self.room, ', '.join(sorted(self.materials)), DOCS))


# ---- writing a model's triangles in a kit material's vertex layout ----------------------------------------------

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


def kit_shapes(src, mats, prims, choose):
    """the visible primitives (stage_builder.read_glb's form) grouped by the kit model their material comes from:
    ({kit model: [Shape, one per kit material]}, triangles). The parts drawn with one kit material become one shape
    (a model of a hundred parts draws as a few). `choose(prim)` is the primitive's kit material."""
    merged = {}
    tris = 0
    for p in prims:
        kit_name = choose(p)
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
    return by_model, tris


def write_kit_model(w, src, kit_model, name, shapes, textures):
    """one output FMDL `name` of `shapes` (kit_shapes) with the kit model's materials: (dst, used materials)"""
    model_at, _, materials = gp.read_model(src, kit_model)
    dst, _, used = gp.write_model(w, model_at, name, shapes, {k: s.tris for k, s in enumerate(shapes)}, materials, textures, (0.0, 0.0, 0.0))
    return dst, used


def texture_names(src, mats, kit_names):
    """the textures the kit materials sample, in first-use order"""
    out = []
    for kit_name in kit_names:
        kit_model, _ = mats[kit_name]
        material_at = dict(gp.read_model(src, kit_model)[2])[kit_name]
        refs = src.rel(material_at + 0x28)
        for i in range(src.b[material_at + 0x10]):
            t = src.cstr(src.rel(refs + 8 * i))
            if t not in out:
                out.append(t)
    return out


def animated(src, kit_model, kit_names):
    """True when the room's texture SRT animation of `kit_model` moves one of `kit_names` (an island's water, lava)"""
    entries = dict(src.dict(src.rel(0x34))) if src.rel(0x34) else {}
    if kit_model not in entries:
        return False
    animation = entries[kit_model]
    if src.b[animation:animation + 4] != b'FSHU':
        return False
    rows = src.rel(animation + 0x2C)
    return any(src.cstr(src.rel(rows + i * 32 + 0x10)) in kit_names for i in range(src.u16(animation + 0x14)))
