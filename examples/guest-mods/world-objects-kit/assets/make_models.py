#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Reproduce the original models of the game-materials example; no external assets or libraries.

Every model is original geometry. The materials named after Dragon Roost Island's (Txe_gake_v: rock,
Txe_hai_v: ash, Txe_ita_v: wooden planks) carry no texture: the port draws them with the game's own
materials, from the player's game files (docs/world-objects.md#game-materials).

  rock.glb        a small volcanic rock island: rock cliffs, an ash top, a plank deck and a ramp up from the
                  water; vertex colours shade it as the game's rooms are shaded (darker at the foot of a cliff)
                  and island_col gives stone, sand-like ash and wood collision
  rock-far.glb    its distant model: the same island with fewer triangles (no deck), for the squares around
  lookout.glb     a mixed model: a rock base and a plank floor with game materials, and a roof with a PNG
                  texture of its own (red cloth, 128 x 128)
"""
import json
import math
from pathlib import Path
import struct
import sys
import zlib

ROCK, ASH, WOOD, CLOTH = 'Txe_gake_v', 'Txe_hai_v', 'Txe_ita_v', 'cloth'
# texture repeats per metre, as Dragon Roost Island's own models repeat these textures (measured from the room)
REPEAT = {ROCK: 1 / 16.6, ASH: 1 / 6.5, WOOD: 1 / 3.85, CLOTH: 1 / 2.0}
COLLISION = {ROCK: 'col_stone', ASH: 'col_sand', WOOD: 'col_wood', CLOTH: 'col_wood'}


def png(px):
    h, w = len(px), len(px[0])
    rows = b''.join(b'\0' + b''.join(bytes(p) for p in row) for row in px)
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    packed = b'\x78\x01'  # stored DEFLATE blocks: the same bytes with every zlib version
    for i in range(0, len(rows), 65535):
        block = rows[i:i + 65535]
        packed += bytes([i + 65535 >= len(rows)]) + struct.pack('<HH', len(block), len(block) ^ 0xffff) + block
    packed += struct.pack('>I', zlib.adler32(rows) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', packed) + chunk(b'IEND', b''))


def cloth():
    """red cloth with lighter stripes and a dark hem"""
    px = []
    for y in range(128):
        row = []
        for x in range(128):
            stripe = (x // 16) % 2
            weave = ((x + y) % 4 == 0) * 10
            hem = y < 6 or y > 121
            r, g, b = (150, 40, 34) if stripe else (178, 58, 44)
            if hem:
                r, g, b = 90, 30, 24
            row.append((min(255, r + weave), min(255, g + weave), min(255, b + weave), 255))
        px.append(row)
    return px


class Mesh:
    """triangles in metres: (a, b, c, material, colours) with planar or given texture coordinates"""
    def __init__(self):
        self.tris = []

    def tri(self, a, b, c, material, shade=(1, 1, 1), uv=None):
        if uv is None:
            uv = uv_for(material, (a, b, c))
        self.tris.append(((a, b, c), uv, material, shade))

    def quad(self, a, b, c, d, material, shade=(1, 1, 1, 1), uv=None):
        self.tri(a, b, c, material, (shade[0], shade[1], shade[2]), None if uv is None else (uv[0], uv[1], uv[2]))
        self.tri(a, c, d, material, (shade[0], shade[2], shade[3]), None if uv is None else (uv[0], uv[2], uv[3]))


def uv_for(material, pts):
    """box mapping: the axis the face looks along chooses the plane"""
    a, b, c = pts
    u = [b[k] - a[k] for k in range(3)]
    v = [c[k] - a[k] for k in range(3)]
    n = [abs(u[1] * v[2] - u[2] * v[1]), abs(u[2] * v[0] - u[0] * v[2]), abs(u[0] * v[1] - u[1] * v[0])]
    s = REPEAT[material]
    axis = n.index(max(n))
    plane = {0: (2, 1), 1: (0, 2), 2: (0, 1)}[axis]
    return tuple((p[plane[0]] * s, -p[plane[1]] * s) for p in pts)


def ring(n, r, y, wobble, phase):
    return [(r * (1 + wobble * math.sin(3 * i * 2 * math.pi / n + phase) + 0.5 * wobble * math.sin(7 * i * 2 * math.pi / n)) * math.cos(i * 2 * math.pi / n), y,
             -r * (1 + wobble * math.sin(3 * i * 2 * math.pi / n + phase) + 0.5 * wobble * math.sin(7 * i * 2 * math.pi / n)) * math.sin(i * 2 * math.pi / n))
            for i in range(n)]


def band(m, n, lower, upper, material, shade_low, shade_high):
    for i in range(n):
        j = (i + 1) % n
        m.quad(lower[i], lower[j], upper[j], upper[i], material, (shade_low, shade_low, shade_high, shade_high))


def rock_island(far=False):
    """about 26 m across; its ash top 6 m above the sea; a ramp up its south side (-z) from the water"""
    m = Mesh()
    n = 12 if far else 24
    under = ring(n, 15.0, -8.0, .06, 0)
    sea = ring(n, 13.0, -0.5, .07, 0.4)
    ledge = ring(n, 11.5, 2.5, .09, 0.9)
    lip = ring(n, 9.5, 5.6, .1, 1.3)
    top = ring(n, 7.5, 6.0, .08, 1.7)
    band(m, n, under, sea, ROCK, .18, .30)
    band(m, n, sea, ledge, ROCK, .30, .48)
    band(m, n, ledge, lip, ROCK, .48, .62)
    band(m, n, lip, top, ASH, .55, .62)
    centre = (0.3, 6.25, -0.2)
    for i in range(n):
        m.tri(top[i], top[(i + 1) % n], centre, ASH, (.62, .62, .66))
    if far:
        return m
    # the ramp: rock from the water up to the lip, 2.6 m wide, along -z
    x0, x1 = -1.3, 1.3
    z_sea, z_top = -16.5, -8.6
    steps = 6
    for k in range(steps):
        t0, t1 = k / steps, (k + 1) / steps
        za, zb = z_sea + (z_top - z_sea) * t0, z_sea + (z_top - z_sea) * t1
        ya, yb = -0.3 + 5.9 * t0, -0.3 + 5.9 * t1
        sa, sb = .40 + .2 * t0, .40 + .2 * t1
        m.quad((x0, ya, za), (x1, ya, za), (x1, yb, zb), (x0, yb, zb), ROCK, (sa, sa, sb, sb))
        for x, side in ((x0, -1), (x1, 1)):  # the ramp's sides, down into the sea
            a, b = (x, ya, za), (x, yb, zb)
            if side < 0:
                m.quad((x, -3.0, zb), b, a, (x, -3.0, za), ROCK, (.2, .45, .45, .2))
            else:
                m.quad((x, -3.0, za), a, b, (x, -3.0, zb), ROCK, (.2, .45, .45, .2))
    # a plank deck on the ash top, 5 x 4 m, 25 cm thick
    dx0, dx1, dz0, dz1, dy0, dy1 = -2.5, 2.5, -2.0, 2.0, 5.9, 6.4
    m.quad((dx0, dy1, dz0), (dx0, dy1, dz1), (dx1, dy1, dz1), (dx1, dy1, dz0), WOOD, (.62, .62, .62, .62))
    for a, b in (((dx0, dz0), (dx1, dz0)), ((dx1, dz0), (dx1, dz1)), ((dx1, dz1), (dx0, dz1)), ((dx0, dz1), (dx0, dz0))):
        m.quad((a[0], dy0, a[1]), (b[0], dy0, b[1]), (b[0], dy1, b[1]), (a[0], dy1, a[1]), WOOD, (.35, .35, .55, .55))
    return m


def lookout():
    """a 4 m rock base with a plank floor, four posts and a cloth roof (own texture), about 7 m high"""
    m = Mesh()
    n = 10
    base0, base1 = ring(n, 2.6, 0.0, .05, 0), ring(n, 2.2, 1.0, .05, 0.5)
    band(m, n, base0, base1, ROCK, .3, .5)
    for i in range(n):
        m.tri(base1[i], base1[(i + 1) % n], (0, 1.05, 0), ROCK, (.5, .5, .55))
    h = 1.2
    m.quad((-1.8, h, -1.8), (-1.8, h, 1.8), (1.8, h, 1.8), (1.8, h, -1.8), WOOD, (.62, .62, .62, .62))
    for a, b in (((-1.8, -1.8), (1.8, -1.8)), ((1.8, -1.8), (1.8, 1.8)), ((1.8, 1.8), (-1.8, 1.8)), ((-1.8, 1.8), (-1.8, -1.8))):
        m.quad((a[0], 1.0, a[1]), (b[0], 1.0, b[1]), (b[0], h, b[1]), (a[0], h, a[1]), WOOD, (.4, .4, .55, .55))
    for px, pz in ((-1.6, -1.6), (1.6, -1.6), (1.6, 1.6), (-1.6, 1.6)):  # posts
        r = .12
        c = [(px - r, pz - r), (px + r, pz - r), (px + r, pz + r), (px - r, pz + r)]
        for k in range(4):
            a, b = c[k], c[(k + 1) % 4]
            m.quad((a[0], h, a[1]), (b[0], h, b[1]), (b[0], 4.4, b[1]), (a[0], 4.4, a[1]), WOOD, (.45, .45, .6, .6))
    # the roof: a four-sided cloth pyramid, its texture's V down each side
    apex, eave = (0, 6.2, 0), [(-2.3, 4.3, -2.3), (2.3, 4.3, -2.3), (2.3, 4.3, 2.3), (-2.3, 4.3, 2.3)]
    for k in range(4):
        a, b = eave[k], eave[(k + 1) % 4]
        m.tri(b, a, apex, CLOTH, uv=((2, 1), (0, 1), (1, 0)))
        m.tri(a, b, apex, CLOTH, uv=((0, 1), (2, 1), (1, 0)))  # its underside
    return m


def glb(mesh, collision=True, images=None):
    """GLB bytes: one visible mesh, one primitive per material (game materials without a texture, with
    COLOR_0), and an island_col mesh with col_* materials"""
    images = images or {}
    binary, views, accessors = bytearray(), [], []

    def part(body):
        binary.extend(b'\0' * (-len(binary) % 4))
        views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(body)})
        binary.extend(body)
        return len(views) - 1

    def floats(rows, width):
        rows = [tuple(struct.unpack('<f', struct.pack('<f', round(x, 6)))[0] for x in row) for row in rows]
        vi = part(struct.pack('<%df' % (len(rows) * width), *(x for row in rows for x in row)))
        accessors.append({'bufferView': vi, 'componentType': 5126, 'count': len(rows), 'type': 'VEC%d' % width,
                          'min': [min(r[k] for r in rows) for k in range(width)], 'max': [max(r[k] for r in rows) for k in range(width)]})
        return len(accessors) - 1

    def indices(count):
        vi = part(struct.pack('<%dH' % count, *range(count)))
        accessors.append({'bufferView': vi, 'componentType': 5123, 'count': count, 'type': 'SCALAR'})
        return len(accessors) - 1

    def geometry(tris, with_uv_colour):
        pos, nrm, uv, col = [], [], [], []
        for pts, uvs, _, shade in tris:
            a, b, c = pts
            u = [b[k] - a[k] for k in range(3)]
            v = [c[k] - a[k] for k in range(3)]
            n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            ln = math.sqrt(sum(x * x for x in n)) or 1
            for k in range(3):
                pos.append(pts[k])
                nrm.append(tuple(x / ln for x in n))
                uv.append(uvs[k])
                col.append((shade[k], shade[k], shade[k], 1.0))
        out = {'POSITION': floats(pos, 3), 'NORMAL': floats(nrm, 3)}
        if with_uv_colour:
            out['TEXCOORD_0'] = floats(uv, 2)
            out['COLOR_0'] = floats(col, 4)
        return out, len(pos)

    materials, textures, imgs = [], [], []
    names = []
    for _, _, mat, _ in mesh.tris:
        if mat not in names:
            names.append(mat)
    for name in names:
        m = {'name': name, 'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0.0, 'roughnessFactor': 1.0}}
        if name in images:  # a material with a texture of its own
            imgs.append({'bufferView': part(png(images[name])), 'mimeType': 'image/png'})
            textures.append({'source': len(imgs) - 1})
            m['pbrMetallicRoughness']['baseColorTexture'] = {'index': len(textures) - 1}
        materials.append(m)
    primitives = []
    for i, name in enumerate(names):
        group = [t for t in mesh.tris if t[2] == name]
        attrs, count = geometry(group, True)
        primitives.append({'attributes': attrs, 'indices': indices(count), 'material': i})
    meshes = [{'name': 'model', 'primitives': primitives}]
    nodes = [{'name': 'model', 'mesh': 0}]
    if collision:
        col_names = []
        for name in names:
            if COLLISION[name] not in col_names:
                col_names.append(COLLISION[name])
        col_primitives = []
        for cname in col_names:
            materials.append({'name': cname, 'pbrMetallicRoughness': {'baseColorFactor': [.5, .5, .5, 1]}})
            group = [t for t in mesh.tris if COLLISION[t[2]] == cname]
            attrs, count = geometry(group, False)
            col_primitives.append({'attributes': attrs, 'indices': indices(count), 'material': len(materials) - 1})
        meshes.append({'name': 'model_col', 'primitives': col_primitives})
        nodes.append({'name': 'model_col', 'mesh': 1})
    doc = {'asset': {'version': '2.0', 'generator': 'make_models.py (original)'}, 'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}],
           'nodes': nodes, 'meshes': meshes, 'materials': materials, 'accessors': accessors, 'bufferViews': views,
           'buffers': [{'byteLength': len(binary)}]}
    if imgs:
        doc.update(images=imgs, textures=textures)
    js = json.dumps(doc, separators=(',', ':'), sort_keys=True).encode()
    js += b' ' * (-len(js) % 4)
    binary.extend(b'\0' * (-len(binary) % 4))
    total = 12 + 8 + len(js) + 8 + len(binary)
    return struct.pack('<4sII', b'glTF', 2, total) + struct.pack('<I4s', len(js), b'JSON') + js + struct.pack('<I4s', len(binary), b'BIN\0') + bytes(binary)


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    (out / 'rock.glb').write_bytes(glb(rock_island()))
    (out / 'rock-far.glb').write_bytes(glb(rock_island(True), collision=False))
    (out / 'lookout.glb').write_bytes(glb(lookout(), images={CLOTH: cloth()}))


if __name__ == '__main__':
    main()
