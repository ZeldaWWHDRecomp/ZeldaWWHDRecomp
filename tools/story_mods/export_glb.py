#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Export a room of the player's own game, or one game piece, to a GLB file for Blender: a reference for mod makers.

What it exports
  --stage sea --room 44   a whole room: every background (BG) model of the room in place (model: terrain,
                          houses, rocks; model1: water and shore; model3: window glows ...), each model's nodes as
                          objects, one primitive per game material.
  --piece ID              one piece of the port's game pieces catalogue (runtime/assets/game_pieces: a building,
                          an island part or an island ground), selected exactly as the game pieces converter does,
                          its origin where a placed piece's origin is.
  --collision             adds the collision as one more object, "<name>_col", one primitive per ground type with
                          the world-object surface names (col_grass, col_sand, col_wood, col_water, col_lava ...).
  --actors                adds an empty object per placed actor of the room (its name and parameters; no model).

The GLB holds positions, normals, texture coordinates (TEXCOORD_0/1), vertex colours (COLOR_0) where the game has
them, and the textures as PNG, decoded from the game's formats (BC1-BC5, RGBA8, RGB565, RGB5A1, RGBA4, L8, LA8; the
texture's own channel selection applied). Each material is named after the game material, so the name can be used as
a kit material (docs/mod-stages.md); its glTF extras ("wwhd") list the material's samplers and texture names.

Units and axes: 1 metre = 100 game units (the factor every mod model uses), so a GLB coordinate is the game's
coordinate / 100. The game's axes are glTF's (Y up, right-handed); Blender's glTF importer turns them to Z up. A room
is exported room-local: for a Great Sea room the square's centre is the origin (its world centre is in the scene's
extras); other stages keep the stage's coordinates.

The output is the player's own copy of the game's models and textures, made from their own files: it is for looking
at and measuring, never for sharing or putting into a mod. Nothing of the game is in this file.

usage: export_glb.py --game GAME_DIR (--stage NAME --room N | --piece ID [--recipe FILE]) --out FILE.glb [--collision] [--actors]
                     [--max-texture N] [--json]
"""
import argparse
import json
import math
import os
import re
import struct
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import notebook_model as base  # noqa: E402
import game_piece_model as gp  # noqa: E402

BuildError = base.BuildError
require = gp.require
UNITS_PER_METRE = 100.0
SCALE = 1.0 / UNITS_PER_METRE
RECIPES = os.path.join(os.path.dirname(os.path.dirname(HERE)), 'runtime', 'assets', 'game_pieces')
MAX_GLB = 512 * 1024 * 1024


# ---- textures -----------------------------------------------------------------------------------------

def element_index(x, y, bpp):
    """element index inside an 8 x 8 micro tile (thin, not depth), as tools/story_mods/lyt/gx2tile.py"""
    b = lambda v, i: (v >> i) & 1
    if bpp == 8:
        bits = (b(x, 0), b(x, 1), b(x, 2), b(y, 1), b(y, 0), b(y, 2))
    elif bpp == 16:
        bits = (b(x, 0), b(x, 1), b(x, 2), b(y, 0), b(y, 1), b(y, 2))
    elif bpp == 64:
        bits = (b(x, 0), b(y, 0), b(x, 1), b(x, 2), b(y, 1), b(y, 2))
    elif bpp == 128:
        bits = (b(y, 0), b(x, 0), b(x, 1), b(x, 2), b(y, 1), b(y, 2))
    else:
        bits = (b(x, 0), b(x, 1), b(y, 0), b(x, 2), b(y, 1), b(y, 2))
    return sum(v << i for i, v in enumerate(bits))


def surface_address(x, y, pitch, bpp, mode, swizzle):
    """byte address of element (x, y) of a GX2 2D surface, tile modes 1 (linear), 2 (1D) and 4 (2D thin):
    game_piece_model.tiled_address for every element size"""
    if mode in (0, 1):
        return (x + pitch * y) * bpp // 8
    pix = element_index(x, y, bpp) * bpp // 8
    if mode == 2:
        return pix + 64 * bpp // 8 * ((x >> 3) + (pitch >> 3) * (y >> 3))
    require(mode == 4, 'unsupported texture tile mode %d' % mode)
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


def rgb565(c):
    return (((c >> 11) & 31) * 255 + 15) // 31, (((c >> 5) & 63) * 255 + 31) // 63, ((c & 31) * 255 + 15) // 31


def colour_block(block, four=False):
    """16 RGBA texels (bytes of 4) of a BC1 colour block; `four`: always the four-colour mode (BC2, BC3)"""
    c0, c1, bits = struct.unpack('<HHI', block)
    a, b = rgb565(c0), rgb565(c1)
    if c0 > c1 or four:
        pal = [bytes(a + (255,)), bytes(b + (255,)), bytes(tuple((2 * a[k] + b[k] + 1) // 3 for k in range(3)) + (255,)),
               bytes(tuple((a[k] + 2 * b[k] + 1) // 3 for k in range(3)) + (255,))]
    else:
        pal = [bytes(a + (255,)), bytes(b + (255,)), bytes(tuple((a[k] + b[k]) // 2 for k in range(3)) + (255,)), b'\0\0\0\0']
    return [pal[(bits >> (2 * i)) & 3] for i in range(16)]


def value_block(block, signed=False):
    """16 values (0..255) of a BC4 block (BC3's alpha, a BC5 channel)"""
    if signed:
        a0, a1 = struct.unpack('bb', block[:2])
        a0, a1 = max(a0, -127), max(a1, -127)
    else:
        a0, a1 = block[0], block[1]
    if a0 > a1:
        pal = [a0, a1] + [((7 - i) * a0 + i * a1) / 7 for i in range(1, 7)]
    else:
        pal = [a0, a1] + [((5 - i) * a0 + i * a1) / 5 for i in range(1, 5)] + ([-127, 127] if signed else [0, 255])
    if signed:
        pal = [int(round((v + 127) * 255 / 254)) for v in pal]
    else:
        pal = [int(round(v)) for v in pal]
    bits = int.from_bytes(block[2:8], 'little')
    return [pal[(bits >> (3 * i)) & 7] for i in range(16)]


# GX2 surface format (low byte; 0x200 signed, 0x400 sRGB) -> (bits per element, block size in texels)
FORMATS = {0x01: (8, 1), 0x02: (8, 1), 0x07: (16, 1), 0x08: (16, 1), 0x0A: (16, 1), 0x0B: (16, 1), 0x1A: (32, 1),
           0x31: (64, 4), 0x32: (128, 4), 0x33: (128, 4), 0x34: (64, 4), 0x35: (128, 4)}


def texel_reader(fmt):
    """element bytes -> list of 4-byte RGBA texels (one, or 16 for a 4x4 block) in the surface's own channels"""
    kind, signed = fmt & 0xFF, bool(fmt & 0x200)
    if kind == 0x31:
        return lambda e: colour_block(e)
    if kind == 0x32:
        def bc2(e):
            alpha = int.from_bytes(e[:8], 'little')
            return [t[:3] + bytes((((alpha >> (4 * i)) & 15) * 17,)) for i, t in enumerate(colour_block(e[8:], True))]
        return bc2
    if kind == 0x33:
        return lambda e: [t[:3] + bytes((a,)) for t, a in zip(colour_block(e[8:], True), value_block(e[:8]))]
    if kind == 0x34:
        return lambda e: [bytes((v, 0, 0, 255)) for v in value_block(e, signed)]
    if kind == 0x35:
        return lambda e: [bytes((r, g, 0, 255)) for r, g in zip(value_block(e[:8], signed), value_block(e[8:], signed))]
    if kind == 0x1A:
        return lambda e: [bytes(e)]
    if kind == 0x08:
        return lambda e: [bytes(rgb565(struct.unpack('>H', e)[0]) + (255,))]
    if kind == 0x0A:  # RGB5A1: red in the top bits
        def rgb5a1(e):
            v = struct.unpack('>H', e)[0]
            return [bytes((((v >> 11) & 31) * 255 // 31, ((v >> 6) & 31) * 255 // 31, ((v >> 1) & 31) * 255 // 31, 255 if v & 1 else 0))]
        return rgb5a1
    if kind == 0x0B:  # RGBA4
        def rgba4(e):
            v = struct.unpack('>H', e)[0]
            return [bytes((((v >> 12) & 15) * 17, ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17))]
        return rgba4
    if kind in (0x01, 0x02):
        return lambda e: [bytes((e[0], 0, 0, 255))]
    if kind == 0x07:
        return lambda e: [bytes((e[0], e[1], 0, 255))]
    raise BuildError('unsupported texture format 0x%X' % fmt)


def select_channels(rgba, selectors):
    """the texture's component selection (GX2 comp_sel: 0..3 = R, G, B, A, 4 = 0, 5 = 1) on RGBA bytes"""
    if tuple(selectors) == (0, 1, 2, 3):
        return rgba
    channels = [rgba[k::4] for k in range(4)]
    n = len(rgba) // 4
    pick = [channels[s] if s < 4 else bytes([0 if s == 4 else 255]) * n for s in selectors]
    out = bytearray(len(rgba))
    for k in range(4):
        out[k::4] = pick[k]
    return bytes(out)


def decode_surface(data, width, height, fmt, mode, swizzle, pitch, selectors=(0, 1, 2, 3)):
    """the first level of a GX2 surface -> RGBA8 bytes, row 0 at the top"""
    require(fmt & 0xFF in FORMATS, 'unsupported texture format 0x%X' % fmt)
    bpp, block = FORMATS[fmt & 0xFF]
    size = bpp // 8
    read = texel_reader(fmt)
    ew, eh = -(-width // block), -(-height // block)
    rgba = bytearray(width * height * 4)
    for ey in range(eh):
        for ex in range(ew):
            at = surface_address(ex, ey, pitch, bpp, mode, swizzle)
            texels = read(data[at:at + size])
            if block == 1:
                o = (ey * width + ex) * 4
                rgba[o:o + 4] = texels[0]
                continue
            for row in range(4):
                y = ey * 4 + row
                if y >= height:
                    break
                x = ex * 4
                n = min(4, width - x)
                o = (y * width + x) * 4
                rgba[o:o + 4 * n] = b''.join(texels[row * 4:row * 4 + n])
    return select_channels(bytes(rgba), selectors)


def read_texture(src, texture):
    """(width, height, RGBA8) of a BFRES texture (FTEX) at its first level"""
    surf = texture + 4
    width, height, fmt, mode, swizzle, pitch = (src.u32(surf + 4), src.u32(surf + 8), src.u32(surf + 0x14), src.u32(surf + 0x30),
                                               src.u32(surf + 0x34), src.u32(surf + 0x3C))
    require(0 < width <= 8192 and 0 < height <= 8192, 'invalid texture size')
    data = src.rel(texture + 0xB0)
    length = src.u32(surf + 0x20)
    require(data and data + length <= len(src.b), 'texture data outside the file')
    return width, height, decode_surface(bytes(src.b[data:data + length]), width, height, fmt, mode, swizzle, pitch, src.b[texture + 0x88:texture + 0x8C])


def shrink(width, height, rgba, limit):
    """box-filtered to at most `limit` texels a side (halving)"""
    while limit and (width > limit or height > limit) and width % 2 == 0 and height % 2 == 0:
        nw, nh = width // 2, height // 2
        out = bytearray(nw * nh * 4)
        for y in range(nh):
            r0 = (2 * y) * width * 4
            r1 = r0 + width * 4
            for x in range(nw):
                a, b = r0 + 8 * x, r1 + 8 * x
                for k in range(4):
                    out[(y * nw + x) * 4 + k] = (rgba[a + k] + rgba[a + 4 + k] + rgba[b + k] + rgba[b + 4 + k] + 2) >> 2
        width, height, rgba = nw, nh, bytes(out)
    return width, height, rgba


def png(width, height, rgba):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)
    stride = width * 4
    raw = b''.join(b'\0' + rgba[y * stride:(y + 1) * stride] for y in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


# ---- the room's models ----------------------------------------------------------------------------------

def attribute(record, at, fmt):
    """one vertex attribute as floats; None for a format this tool does not read"""
    if fmt == 0x811:
        return struct.unpack_from('>3f', record, at)
    if fmt == 0x813:
        return struct.unpack_from('>4f', record, at)
    if fmt == 0x80D:
        return struct.unpack_from('>2f', record, at)
    if fmt == 0x808:
        return struct.unpack_from('>2e', record, at)
    if fmt == 0x80F:
        return struct.unpack_from('>4e', record, at)
    if fmt == 0x00A:
        return tuple(v / 255 for v in record[at:at + 4])
    if fmt == 0x20A:
        return tuple(max(-1.0, v / 127) for v in struct.unpack_from('>4b', record, at))
    if fmt == 0x007:
        return tuple(v / 65535 for v in struct.unpack_from('>2H', record, at))
    if fmt == 0x207:
        return tuple(max(-1.0, v / 32767) for v in struct.unpack_from('>2h', record, at))
    if fmt == 0x004:
        return tuple(v / 255 for v in record[at:at + 2])
    if fmt == 0x20B:  # snorm 10_10_10_2, x in the low bits
        v = struct.unpack_from('>I', record, at)[0]
        out = []
        for k in range(3):
            c = (v >> (10 * k)) & 0x3FF
            c = c - 0x400 if c & 0x200 else c
            out.append(max(-1.0, c / 511))
        return tuple(out)
    if fmt == 0x00B:  # unorm 10_10_10_2
        v = struct.unpack_from('>I', record, at)[0]
        return tuple(((v >> (10 * k)) & 0x3FF) / 1023 for k in range(3)) + ((v >> 30) / 3,)
    return None


ATTRIBUTE_SIZE = {0x811: 12, 0x813: 16, 0x80D: 8, 0x808: 4, 0x80F: 8, 0x00A: 4, 0x20A: 4, 0x007: 4, 0x207: 4, 0x004: 2, 0x20B: 4, 0x00B: 4}


class Part:
    """one exported primitive: node, material, positions (game units), normals, uvs, colours, triangles"""
    def __init__(self, node, material):
        self.node, self.material = node, material
        self.pos, self.normals, self.uv0, self.uv1, self.colours, self.tris = [], [], [], [], [], []


def inverse_transpose(m):
    a, b, c = m
    det = a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])
    if abs(det) < 1e-12:
        return None, det
    cof = [[b[1] * c[2] - b[2] * c[1], -(b[0] * c[2] - b[2] * c[0]), b[0] * c[1] - b[1] * c[0]],
           [-(a[1] * c[2] - a[2] * c[1]), a[0] * c[2] - a[2] * c[0], -(a[0] * c[1] - a[1] * c[0])],
           [a[1] * b[2] - a[2] * b[1], -(a[0] * b[2] - a[2] * b[0]), a[0] * b[1] - a[1] * b[0]]]
    return [[cof[j][k] / det for k in range(3)] for j in range(3)], det


def model_parts(src, name, warnings):
    """every shape of BG model `name` as Parts, in room coordinates: each shape's bone transform applied in full
    (rotation too), its first (most detailed) mesh"""
    models = dict(src.dict(src.rel(0x20)))
    require(name in models, 'the room has no model ' + name)
    model = models[name]
    matrices = gp.bone_matrices(src, src.rel(model + 0xC))
    materials = src.dict(src.rel(model + 0x18))
    vertex_array = src.rel(model + 0x10)
    parts = []
    for sname, at in src.dict(src.rel(model + 0x14)):
        node = sname.split('__')[0] if '__' in sname else sname
        material = materials[src.u16(at + 0xE)][0]
        if src.b[at + 0x16] != 0:
            warnings.append('%s/%s: a skinned shape, left out' % (name, sname))
            continue
        m, t = matrices.get(src.u16(at + 0x10), ([[1, 0, 0], [0, 1, 0], [0, 0, 1]], (0, 0, 0)))
        normal_matrix, det = inverse_transpose(m)
        if normal_matrix is None:
            warnings.append('%s/%s: a shape scaled to nothing, left out' % (name, sname))
            continue
        vertex = vertex_array + 0x20 * src.u16(at + 0x12)
        attrs = src.rel(vertex + 0x10)
        layout = {src.cstr(src.rel(attrs + i * 12)): (src.b[attrs + i * 12 + 4], src.u16(attrs + i * 12 + 6), src.u32(attrs + i * 12 + 8))
                  for i in range(src.b[vertex + 4])}
        buffers = src.rel(vertex + 0x18)
        if layout.get('_p0', (0, 0, 0))[2] != 0x811:
            warnings.append('%s/%s: positions not float, left out' % (name, sname))
            continue
        count = src.u32(vertex + 8)

        def column(attr):
            if attr not in layout:
                return None
            index, offset, fmt = layout[attr]
            if fmt not in ATTRIBUTE_SIZE or index >= src.b[vertex + 5]:
                warnings.append('%s/%s: %s in format 0x%X, left out' % (name, sname, attr, fmt))
                return None
            buf = buffers + 0x18 * index
            stride, data = src.u16(buf + 0xC), src.rel(buf + 0x14)
            require(offset + ATTRIBUTE_SIZE[fmt] <= stride and data + stride * count <= len(src.b), 'vertex data outside the file')
            return [attribute(src.b, data + stride * k + offset, fmt) for k in range(count)]
        positions = column('_p0')
        normals, uv0, uv1, colours = column('_n0'), column('_u0'), column('_u1'), column('_c0')
        mesh = src.rel(at + 0x24)
        primitive, index_format = src.u32(mesh), src.u32(mesh + 4)
        if primitive != 4 or index_format not in (4, 9):
            warnings.append('%s/%s: not an indexed triangle list, left out' % (name, sname))
            continue
        wide = index_format == 9
        n = src.u32(mesh + 8)
        first = src.u32(mesh + 0x18)
        ib = src.rel(src.rel(mesh + 0x14) + 0x14)
        idx = struct.unpack_from('>%d%s' % (n, 'I' if wide else 'H'), src.b, ib)
        tris = [(idx[i] + first, idx[i + 1] + first, idx[i + 2] + first) for i in range(0, n - 2, 3)]
        tris = [tr for tr in tris if max(tr) < count]
        if det < 0:  # a mirroring bone turns the faces inside out: keep them facing out
            tris = [(a, c, b) for a, b, c in tris]
        used = sorted({k for tr in tris for k in tr})
        remap = {k: j for j, k in enumerate(used)}
        p = Part(name + '/' + node, material)
        for k in used:
            x, y, z = positions[k]
            p.pos.append(tuple(m[j][0] * x + m[j][1] * y + m[j][2] * z + t[j] for j in range(3)))
            if normals:
                nx, ny, nz = normals[k][:3]
                w = [normal_matrix[j][0] * nx + normal_matrix[j][1] * ny + normal_matrix[j][2] * nz for j in range(3)]
                length = math.sqrt(sum(v * v for v in w)) or 1.0
                p.normals.append(tuple(v / length for v in w))
            if uv0:
                p.uv0.append(uv0[k][:2])
            if uv1:
                p.uv1.append(uv1[k][:2])
            if colours:
                c = colours[k]
                p.colours.append(tuple(min(1.0, max(0.0, v)) for v in (tuple(c) + (1.0,) * 4)[:4]))
        p.tris = [tuple(remap[k] for k in tr) for tr in tris]
        if p.tris:
            parts.append(p)
    return parts, dict(materials)


def material_info(src, material):
    """sampler -> texture name, render info and the draw buffer of a BG material"""
    refs = src.rel(material + 0x28)
    textures = [src.cstr(src.rel(refs + 8 * i)) for i in range(src.b[material + 0x10])]
    samplers = []
    first = src.rel(material + 0x2C)
    if src.rel(material + 0x30):
        for sname, at in src.dict(src.rel(material + 0x30)):
            index = (at - first) // 24
            if 0 <= index < len(textures):
                samplers.append((sname, textures[index]))
    if not samplers:
        samplers = [('_t%d' % i, t) for i, t in enumerate(textures)]
    info = {}
    if src.rel(material + 0x1C):
        for name, value in src.dict(src.rel(material + 0x1C)):
            n, kind = src.u16(value), src.b[value + 2]
            if kind == 0:
                info[name] = [struct.unpack_from('>i', src.b, value + 8 + 4 * i)[0] for i in range(n)]
            elif kind == 1:
                info[name] = [src.cstr(src.rel(value + 8 + 4 * i)) for i in range(n)]
    return samplers, info


def colour_sampler(samplers):
    """the sampler a material's base colour comes from: _a0, else the first"""
    for name, texture in samplers:
        if name == '_a0':
            return texture
    return samplers[0][1] if samplers else None


# ---- collision and actors ---------------------------------------------------------------------------------

def collision_parts(dzb, room_origin, center, keep=None, void=()):
    """the room collision as Parts per surface (col_*), positions relative to `center`. `keep(group name, points,
    exit)` selects triangles (default: all); groups in `void` are void floors (a game piece island's holes)"""
    verts, tris, names, infos, group_info = dzb
    by_surface = {}
    for a, b, c, info, group in tris:
        pts = [tuple(verts[k][j] - room_origin[j] for j in range(3)) for k in (a, b, c)]
        i0, i1 = infos[info][:2] if info < len(infos) else (0x3F << 13, 0)
        if keep and not keep(names[group], pts, (i0 >> 13) & 0x3F):
            continue
        surface = gp.triangle_surface(pts, (i1 >> 16) & 0x1F, group_info[group] & gp.WATER_GROUP, names[group] in void,
                                      group_info[group] & gp.LAVA_GROUP)
        if surface is None:
            continue
        p = by_surface.setdefault(surface, Part('collision', surface))
        base_index = len(p.pos)
        p.pos += [tuple(q[j] - center[j] for j in range(3)) for q in pts]
        p.tris.append((base_index, base_index + 1, base_index + 2))
    return [by_surface[k] for k in sorted(by_surface)]


def room_actors(dzr, room_origin, center):
    """placed actors of a room's dzr: [{name, kind, parameters, position (game units)}]"""
    out = []
    if len(dzr) < 4:
        return out
    n, = struct.unpack_from('>I', dzr, 0)
    if n > 64 or 4 + 12 * n > len(dzr):
        return out
    sizes = {'ACTR': 32, 'TRES': 32, 'TGOB': 32, 'SCOB': 36, 'TGSC': 36, 'DOOR': 36, 'TGDR': 36}
    for i in range(n):
        tag, count, offset = struct.unpack_from('>4sII', dzr, 4 + 12 * i)
        tag = tag.decode('ascii', 'replace')
        size = sizes.get(tag)
        if not size or offset > len(dzr):
            continue
        for k in range(min(count, (len(dzr) - offset) // size)):
            e = dzr[offset + k * size:offset + (k + 1) * size]
            name = e[:8].split(b'\0')[0].decode('ascii', 'replace')
            params, = struct.unpack_from('>I', e, 8)
            x, y, z = struct.unpack_from('>3f', e, 12)
            rx, ry, rz = struct.unpack_from('>3h', e, 24)
            if not name:
                continue
            out.append({'name': name, 'kind': tag, 'parameters': '%08X' % params,
                        'position': [x - room_origin[0] - center[0], y - room_origin[1] - center[1], z - room_origin[2] - center[2]],
                        'angle': ry * 360.0 / 65536.0})
    return out


# ---- the GLB -------------------------------------------------------------------------------------------------

class Glb:
    def __init__(self):
        self.doc = {'asset': {'version': '2.0', 'generator': 'wwhd export_glb'}, 'scenes': [{'nodes': []}], 'scene': 0,
                    'nodes': [], 'meshes': [], 'materials': [], 'accessors': [], 'bufferViews': [], 'buffers': [],
                    'images': [], 'textures': [], 'samplers': [{'wrapS': 10497, 'wrapT': 10497, 'magFilter': 9729, 'minFilter': 9987}]}
        self.bin = bytearray()

    def view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        v = {'buffer': 0, 'byteOffset': len(self.bin), 'byteLength': len(data)}
        if target:
            v['target'] = target
        self.bin += data
        self.doc['bufferViews'].append(v)
        return len(self.doc['bufferViews']) - 1

    def accessor(self, values, kind, component=5126, target=34962, bounds=False):
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[kind]
        flat = [x for v in values for x in (v if n > 1 else (v,))]
        code = {5126: 'f', 5125: 'I'}[component]
        a = {'bufferView': self.view(struct.pack('<%d%s' % (len(flat), code), *flat), target), 'componentType': component,
             'count': len(values), 'type': kind}
        if bounds:
            a['min'] = [min(v[k] for v in values) for k in range(n)]
            a['max'] = [max(v[k] for v in values) for k in range(n)]
        self.doc['accessors'].append(a)
        return len(self.doc['accessors']) - 1

    def image(self, name, data):
        self.doc['images'].append({'name': name, 'mimeType': 'image/png', 'bufferView': self.view(data)})
        self.doc['textures'].append({'source': len(self.doc['images']) - 1, 'sampler': 0, 'name': name})
        return len(self.doc['textures']) - 1

    def node(self, node, parent=None):
        self.doc['nodes'].append(node)
        i = len(self.doc['nodes']) - 1
        if parent is None:
            self.doc['scenes'][0]['nodes'].append(i)
        else:
            self.doc['nodes'][parent].setdefault('children', []).append(i)
        return i

    def mesh(self, name, parts, material_index, scale):
        prims = []
        for p in parts:
            attrs = {'POSITION': self.accessor([tuple(v * scale for v in q) for q in p.pos], 'VEC3', bounds=True)}
            if p.normals and len(p.normals) == len(p.pos):
                attrs['NORMAL'] = self.accessor(p.normals, 'VEC3')
            if p.uv0 and len(p.uv0) == len(p.pos):
                attrs['TEXCOORD_0'] = self.accessor(p.uv0, 'VEC2')
            if p.uv1 and len(p.uv1) == len(p.pos):
                attrs['TEXCOORD_1'] = self.accessor(p.uv1, 'VEC2')
            if p.colours and len(p.colours) == len(p.pos):
                attrs['COLOR_0'] = self.accessor(p.colours, 'VEC4')
            indices = self.accessor([k for t in p.tris for k in t], 'SCALAR', 5125, 34963)
            prims.append({'attributes': attrs, 'indices': indices, 'material': material_index[p.material], 'mode': 4})
        self.doc['meshes'].append({'name': name, 'primitives': prims})
        return len(self.doc['meshes']) - 1

    def bytes(self):
        self.doc['buffers'] = [{'byteLength': len(self.bin)}]
        for k in [k for k, v in self.doc.items() if v == []]:
            del self.doc[k]
        js = json.dumps(self.doc, separators=(',', ':'), sort_keys=True).encode()
        js += b' ' * (-len(js) % 4)
        body = bytes(self.bin) + b'\0' * (-len(self.bin) % 4)
        total = 12 + 8 + len(js) + 8 + len(body)
        require(total <= MAX_GLB, 'the export is larger than 512 MiB')
        return (struct.pack('<4sII', b'glTF', 2, total) + struct.pack('<I4s', len(js), b'JSON') + js +
                struct.pack('<I4s', len(body), b'BIN\0') + body)


def write_glb(name, parts, materials, textures, collision=None, actors=None, extras=None, max_texture=0):
    """the GLB bytes and a summary. parts: [Part] (game units); materials: name -> (samplers, info); textures:
    texture name -> (width, height, RGBA8)"""
    g = Glb()
    summary = {'triangles': 0, 'vertices': 0, 'materials': 0, 'textures': 0, 'texture_names': [], 'collision_triangles': 0,
               'surfaces': {}, 'actors': 0}
    texture_index = {}

    def texture(tname):
        if tname not in texture_index:
            if tname not in textures:
                return None
            w, h, rgba = shrink(*textures[tname], max_texture)
            texture_index[tname] = (g.image(tname, png(w, h, rgba)), any(rgba[k] < 255 for k in range(3, len(rgba), 4)))
            summary['texture_names'].append(tname)
        return texture_index[tname]
    material_index = {}
    for p in parts:
        if p.material in material_index:
            continue
        samplers, info = materials[p.material]
        m = {'name': p.material, 'doubleSided': False,
             'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0.0, 'roughnessFactor': 1.0},
             'extras': {'wwhd': {'kit_material': p.material, 'samplers': {s: t for s, t in samplers}, 'render_info': info}}}
        colour = colour_sampler(samplers)
        if colour:
            got = texture(colour)
            if got:
                m['pbrMetallicRoughness']['baseColorTexture'] = {'index': got[0], 'texCoord': 0}
                if got[1]:
                    # a texture with alpha: cut-out, as the game draws its foliage and fences (xlu buffers blend)
                    translucent = info.get('draw_buffer_type', [1])[0] not in (0, 1)
                    m['alphaMode'] = 'BLEND' if translucent else 'MASK'
                    if not translucent:
                        m['alphaCutoff'] = 0.5
                    m['doubleSided'] = True
        for s, t in samplers:  # the material's other textures: in the file, named in the extras
            texture(t)
        material_index[p.material] = len(g.doc['materials'])
        g.doc['materials'].append(m)
    root = g.node({'name': name, 'extras': extras or {}})
    groups = {}
    for p in parts:
        groups.setdefault(p.node, []).append(p)
    models = {}
    for node_name, ps in groups.items():
        model, _, node = node_name.partition('/')
        if model not in models:
            models[model] = g.node({'name': model}, root)
        g.node({'name': node or model, 'mesh': g.mesh(node_name, ps, material_index, SCALE)}, models[model])
        summary['triangles'] += sum(len(p.tris) for p in ps)
        summary['vertices'] += sum(len(p.pos) for p in ps)
    if collision:
        for p in collision:
            material_index[p.material] = len(g.doc['materials'])
            g.doc['materials'].append({'name': p.material, 'pbrMetallicRoughness': {'baseColorFactor': list(SURFACE_COLOURS.get(p.material, (0.6, 0.6, 0.6))) + [1.0],
                                       'metallicFactor': 0.0, 'roughnessFactor': 1.0}, 'extras': {'wwhd': {'surface': p.material}}})
            summary['surfaces'][p.material] = len(p.tris)
            summary['collision_triangles'] += len(p.tris)
        g.node({'name': name + '_col', 'mesh': g.mesh(name + '_col', collision, material_index, SCALE),
                'extras': {'wwhd': {'collision': True}}}, root)
    if actors:
        holder = g.node({'name': 'actors'}, root)
        for a in actors:
            yaw = math.radians(a['angle']) / 2
            g.node({'name': '%s %s' % (a['name'], a['parameters']), 'translation': [v * SCALE for v in a['position']],
                    'rotation': [0, math.sin(yaw), 0, math.cos(yaw)], 'extras': {'wwhd': a}}, holder)
        summary['actors'] = len(actors)
    summary['materials'] = len(material_index) - (len(collision) if collision else 0)
    summary['textures'] = len(texture_index)
    return g.bytes(), summary


SURFACE_COLOURS = {'col_grass': (0.3, 0.7, 0.25), 'col_sand': (0.9, 0.8, 0.5), 'col_wood': (0.6, 0.4, 0.2), 'col_stone': (0.55, 0.55, 0.55),
                   'col_wall': (0.4, 0.4, 0.45), 'col_water': (0.2, 0.4, 0.9), 'col_shallow': (0.4, 0.7, 0.9), 'col_metal': (0.7, 0.7, 0.8),
                   'col_ice': (0.8, 0.9, 1.0), 'col_void': (0.1, 0.1, 0.1), 'col_waterfall': (0.5, 0.7, 1.0), 'col_lava': (0.95, 0.35, 0.05)}


# ---- the two kinds of export ----------------------------------------------------------------------------------

def room_archive(game, stage, room):
    require(re.fullmatch(r'[A-Za-z0-9_]{1,8}', stage or '') is not None and 0 <= room <= 63, 'a room is a stage name and a room number (0..63)')
    files = gp.room_files(game, '%s_Room%d.szs' % (stage, room))
    bfres = [n for n in files if n.endswith('.bfres')]
    require(len(bfres) == 1, 'the room has no room model')
    return files, base.Bfres(files[bfres[0]])


def sea_origin(stage, room):
    if stage == 'sea' and 1 <= room <= 49:
        return ((((room - 1) % 7) - 3) * 100000.0, 0.0, (((room - 1) // 7) - 3) * 100000.0)
    return (0.0, 0.0, 0.0)


def embedded(src, name):
    group = src.rel(0x20 + 4 * 11)
    for n, at in src.dict(group) if group else []:
        if n == name:
            return bytes(src.b[src.rel(at):src.rel(at) + src.u32(at + 4)])
    return None


def textures_of(src, parts, materials):
    all_textures = dict(src.dict(src.rel(0x24))) if src.rel(0x24) else {}
    out = {}
    for p in parts:
        for _, t in materials[p.material][0]:
            if t not in out and t in all_textures:
                out[t] = read_texture(src, all_textures[t])
    return out


def export_room(game, stage, room, collision=False, actors=False, max_texture=0):
    files, src = room_archive(game, stage, room)
    warnings, parts, materials = [], [], {}
    for mname, _ in src.dict(src.rel(0x20)):
        ps, mats = model_parts(src, mname, warnings)
        for p in ps:
            if p.material not in materials:  # a name in two models: the first, as kit materials take it
                materials[p.material] = material_info(src, mats[p.material])
        parts += ps
    require(parts, 'the room has no models')
    origin = sea_origin(stage, room)
    col = act = None
    if collision:
        d = embedded(src, 'room.dzb')
        require(d is not None, 'the room has no collision')
        col = collision_parts(gp.read_dzb(d), origin, (0, 0, 0))
    if actors:
        d = embedded(src, 'room.dzr')
        act = room_actors(d, origin, (0, 0, 0)) if d else []
    name = '%s_room%d' % (stage, room)
    extras = {'wwhd': {'stage': stage, 'room': room, 'room_origin_game_units': list(origin), 'units_per_metre': UNITS_PER_METRE,
                       'note': 'your own copy of the game\'s models and textures: a reference, never to be shared or put into a mod'}}
    body, summary = write_glb(name, parts, materials, textures_of(src, parts, materials), col, act, extras, max_texture)
    summary.update(kind='room', stage=stage, room=room, warnings=warnings)
    return body, summary


def find_piece(piece_id, recipes=RECIPES):
    """(recipe, piece) of a piece id: `recipes` is the recipes folder or one recipe file (the game writes the set's)"""
    paths = [recipes] if os.path.isfile(recipes) else [os.path.join(recipes, n) for n in sorted(os.listdir(recipes)) if n.endswith('.recipe.json')]
    for path in paths:
        require(os.path.getsize(path) <= 256 * 1024, 'recipe too large')
        with open(path) as f:
            recipe = json.load(f)
        for p in recipe.get('pieces', []):
            if p.get('id') == piece_id:
                gp.check_recipe(recipe)
                return recipe, p
    raise BuildError('no game piece "%s" (runtime/assets/game_pieces)' % piece_id)


def export_piece(game, piece_id, collision=False, max_texture=0, recipes=RECIPES):
    recipe, piece = find_piece(piece_id, recipes)
    room = recipe['source']['room']
    files = gp.room_files(game, recipe['source']['archive'])
    require('Room%d.bfres' % room in files, 'the room archive has no room model')
    src = base.Bfres(files['Room%d.bfres' % room])
    warnings, parts, materials, first = [], [], {}, None
    for k, part in enumerate(piece['parts']):
        mname = gp.model_name(part['model'])
        _, shapes, mats = gp.read_model(src, mname)
        taken, _ = gp.select(shapes, part['nodes'], piece['volume'], part.get('without_materials', []))
        require(taken, piece_id + ': nothing of the piece is in your game files (another game version?)')
        mat_at = dict(mats)
        for si, tris in sorted(taken.items()):
            s = shapes[si]
            p = shape_part(src, s, mname, tris)
            parts.append(p)
            if s.material not in materials:
                materials[s.material] = material_info(src, mat_at[s.material])
            if k == 0:
                first = (first or []) + [s.pos[i] for t in tris for i in t]
    lo = [min(q[j] for q in first) for j in range(3)]
    hi = [max(q[j] for q in first) for j in range(3)]
    center = [(lo[0] + hi[0]) / 2, piece.get('origin_y', lo[1]), (lo[2] + hi[2]) / 2]
    for p in parts:
        p.pos = [tuple(q[j] - center[j] for j in range(3)) for q in p.pos]
    col = None
    if collision:
        d = embedded(src, 'room.dzb')
        if d is not None and piece.get('collision', {}).get('groups'):
            # the game piece's own collision rules (game_piece_model.piece_collision): the listed groups inside the
            # volume, exits left out where the recipe says so, void groups as void floors
            rules = piece['collision']
            groups, void, strip = set(rules['groups']), set(rules.get('void', [])), rules.get('strip_exits', False)
            x0, y0, z0, x1, y1, z1 = piece['volume']
            col = collision_parts(gp.read_dzb(d), sea_origin('sea', room), center,
                                  lambda g, pts, exit_id: g in groups and all(x0 <= q[0] <= x1 and y0 <= q[1] <= y1 and z0 <= q[2] <= z1 for q in pts)
                                  and not (strip and exit_id != 0x3F and g not in void), void)
    extras = {'wwhd': {'piece': piece_id, 'set': recipe['set'], 'room': room, 'origin_room_local_game_units': center,
                       'units_per_metre': UNITS_PER_METRE,
                       'note': 'your own copy of the game\'s models and textures: a reference, never to be shared or put into a mod'}}
    body, summary = write_glb(piece_id, parts, materials, textures_of(src, parts, materials), col, None, extras, max_texture)
    summary.update(kind='piece', piece=piece_id, set=recipe['set'], warnings=warnings)
    return body, summary


def shape_part(src, s, mname, tris):
    """a Part of the taken triangles of a game_piece_model Shape (its positions already moved into the room)"""
    attrs = src.rel(s.vertex + 0x10)
    layout = {src.cstr(src.rel(attrs + i * 12)): (src.u16(attrs + i * 12 + 6), src.u32(attrs + i * 12 + 8)) for i in range(s.nattr)}
    used = sorted({k for t in tris for k in t})
    remap = {k: j for j, k in enumerate(used)}
    p = Part(mname + '/' + s.node, s.material)
    for k in used:
        r = s.records[k]
        p.pos.append(s.pos[k])
        for attr, target in (('_n0', p.normals), ('_u0', p.uv0), ('_u1', p.uv1), ('_c0', p.colours)):
            if attr in layout and layout[attr][1] in ATTRIBUTE_SIZE:
                v = attribute(r, *layout[attr])
                if attr == '_n0':
                    length = math.sqrt(sum(x * x for x in v[:3])) or 1.0
                    v = tuple(x / length for x in v[:3])
                elif attr == '_c0':
                    v = tuple(min(1.0, max(0.0, x)) for x in (tuple(v) + (1.0,) * 4)[:4])
                else:
                    v = v[:2]
                target.append(v)
    p.tris = [tuple(remap[k] for k in t) for t in tris]
    return p


def current_umask():
    mask = os.umask(0o022)
    os.umask(mask)
    return mask


def publish(path, body):
    directory = os.path.dirname(os.path.abspath(path))
    os.makedirs(directory, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=directory, prefix='.export-', delete=False) as target:
            temporary = target.name
            target.write(body)
        os.chmod(temporary, 0o644 & ~current_umask())
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', required=True)
    parser.add_argument('--stage', help='a stage of the game (sea for the Great Sea)')
    parser.add_argument('--room', type=int)
    parser.add_argument('--piece', help='a game piece id (runtime/assets/game_pieces)')
    parser.add_argument('--recipe', help='with --piece: the recipe file of its set (default: the recipes folder)')
    parser.add_argument('--out', required=True)
    parser.add_argument('--collision', action='store_true', help='add the collision as "<name>_col"')
    parser.add_argument('--actors', action='store_true', help='a room: add an empty per placed actor')
    parser.add_argument('--max-texture', type=int, default=0, help='halve textures larger than this many texels a side')
    parser.add_argument('--json', action='store_true', help='print the result as one JSON line')
    args = parser.parse_args()
    try:
        require((args.piece is None) != (args.stage is None), 'give --stage and --room, or --piece')
        require(args.out.lower().endswith('.glb'), '--out is a .glb file')
        if args.piece:
            body, summary = export_piece(args.game, args.piece, args.collision, args.max_texture, args.recipe or RECIPES)
        else:
            require(args.room is not None, '--stage needs --room')
            body, summary = export_room(args.game, args.stage, args.room, args.collision, args.actors, args.max_texture)
        publish(args.out, body)
        summary.update(ok=True, file=os.path.abspath(args.out), bytes=len(body))
    except (BuildError, OSError, KeyError, IndexError, struct.error, ValueError, TypeError, zlib.error) as exc:
        if args.json:
            print(json.dumps({'ok': False, 'error': str(exc)}))
            sys.exit(1)
        parser.exit(1, 'export_glb: %s\n' % exc)
    print(json.dumps(summary, sort_keys=True))


if __name__ == '__main__':
    main()
