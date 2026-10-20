#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Reproduce the original several-material island; no external assets or libraries.

island.glb has six materials, each with its own embedded 128 x 128 PNG: sand, grass, rock and wood
(opaque, repeating textures) and a fence and a bush (cut-out: alphaMode MASK, doubleSided).
A separate island_col mesh supplies sand, grass, stone and wood collision.

`python3 make_fixtures.py --atlas OUT.glb` also writes the same island with one material and one
texture atlas (no fence or bush cut-outs): the single-material comparison used for measurements.
"""
import json
import math
from pathlib import Path
import random
import struct
import sys
import zlib

SIZE = 128
SAND, GRASS, ROCK, WOOD, FENCE, BUSH = range(6)
NAMES = ('sand', 'grass', 'rock', 'wood', 'fence', 'bush')
SURFACE = {SAND: 'col_sand', GRASS: 'col_grass', ROCK: 'col_stone', WOOD: 'col_wood'}


# ---- textures ---------------------------------------------------------------------------------------

def texture(kind):
    rnd = random.Random(1000 + kind)
    px = [[(0, 0, 0, 0)] * SIZE for _ in range(SIZE)]
    noise = [[rnd.random() for _ in range(SIZE)] for _ in range(SIZE)]
    def smooth(x, y, r=2):
        s = 0
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                s += noise[(y + dy) % SIZE][(x + dx) % SIZE]
        return s / (2 * r + 1) ** 2
    for y in range(SIZE):
        for x in range(SIZE):
            n, m = noise[y][x], smooth(x, y)
            if kind == SAND:
                v = 0.85 + 0.25 * (m - .5) + (0.08 if n > .93 else 0)
                c = (232 * v, 208 * v, 150 * v, 255)
            elif kind == GRASS:
                blade = 1 if (x * 7 + y * 3) % 11 < 2 and n > .4 else 0
                v = 0.8 + 0.4 * (m - .5) + 0.12 * blade
                c = (76 * v, 158 * v, 64 * v, 255)
            elif kind == ROCK:
                crack = abs(math.sin(x * .09 + y * .05 + 3 * m)) < .05
                v = (0.75 + 0.5 * (m - .5)) * (0.55 if crack else 1)
                c = (138 * v, 134 * v, 128 * v, 255)
            elif kind == WOOD:
                plank = y % 32
                v = 0.85 + 0.15 * math.sin(x * .3 + 4 * m) - (0.35 if plank < 2 else 0) - (0.2 if (x + 17 * (y // 32)) % 64 < 2 else 0)
                c = (150 * v, 100 * v, 58 * v, 255)
            elif kind == FENCE:
                post = (x % 64) < 10
                rail = 28 <= y < 38 or 84 <= y < 94
                top = y >= 8 or (x % 64) in range(2, 8)
                if (post and top) or rail:
                    v = 0.8 + 0.2 * m
                    c = (196 * v, 172 * v, 128 * v, 255)
                else:
                    c = (196, 172, 128, 0)
            else:  # BUSH: leaf blobs over transparency, rounded top
                inside = False
                for cx, cy, r in ((64, 80, 44), (36, 92, 30), (92, 92, 30), (64, 44, 30), (40, 58, 24), (88, 58, 24)):
                    if (x - cx) ** 2 + (y - cy) ** 2 < r * r * (0.8 + 0.4 * m):
                        inside = True
                if inside and y < 124:
                    v = 0.7 + 0.5 * (m - .5) + (0.15 if n > .85 else 0)
                    c = (60 * v, 132 * v, 52 * v, 255)
                else:
                    c = (60, 132, 52, 0)
            px[y][x] = tuple(max(0, min(255, int(k))) for k in c)
    return px


def png(px):
    h, w = len(px), len(px[0])
    rows = b''.join(b'\0' + b''.join(bytes(p) for p in row) for row in px)
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    # Stored DEFLATE blocks give the same bytes with every zlib version (zlib.compress differs between platforms).
    packed = b'\x78\x01'
    for i in range(0, len(rows), 65535):
        block = rows[i:i + 65535]
        packed += bytes([i + 65535 >= len(rows)]) + struct.pack('<HH', len(block), len(block) ^ 0xffff) + block
    packed += struct.pack('>I', zlib.adler32(rows) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', packed) + chunk(b'IEND', b''))


# ---- geometry (centimetres while building, metres in the GLB) ----------------------------------------

def island():
    tris = []  # (p0, p1, p2, uv0, uv1, uv2, material)
    def planar(p, scale):
        return (p[0] / scale, p[2] / scale)
    def tri(a, b, c, material, uv=None, scale=200.0):
        uv = uv or tuple(planar(p, scale) for p in (a, b, c))
        tris.append((a, b, c) + tuple(uv) + (material,))
    def quad(a, b, c, d, material, uv=None, scale=200.0):
        if uv:
            tri(a, b, c, material, (uv[0], uv[1], uv[2]))
            tri(a, c, d, material, (uv[0], uv[2], uv[3]))
        else:
            tri(a, b, c, material, scale=scale)
            tri(a, c, d, material, scale=scale)
    n = 12
    def ring(r, y, wobble=0.0, phase=0.0):
        return [(r * (1 + wobble * math.sin(3 * i * 2 * math.pi / n + phase)) * math.cos(i * 2 * math.pi / n), y,
                 -r * (1 + wobble * math.sin(3 * i * 2 * math.pi / n + phase)) * math.sin(i * 2 * math.pi / n)) for i in range(n)]
    sea_floor = ring(1300, -900, .05)
    shore = ring(1150, 300, .05)
    beach = ring(800, 420, .06, 1)
    grass = ring(450, 700, .08, 2)
    top = ring(200, 820, .1, 3)
    def band(lower, upper, material, side=False):
        for i in range(n):
            j = (i + 1) % n
            if side:  # coast walls: U along the coast, V up the slope
                u0, u1 = i / n * 12, (i + 1) / n * 12
                quad(lower[i], lower[j], upper[j], upper[i], material,
                     ((u0, 0), (u1, 0), (u1, (upper[i][1] - lower[i][1]) / 200), (u0, (upper[i][1] - lower[i][1]) / 200)))
            else:
                quad(lower[i], lower[j], upper[j], upper[i], material)
    band(sea_floor, shore, SAND, side=True)
    band(shore, beach, SAND)
    band(beach, grass, GRASS)
    band(grass, top, GRASS)
    centre = (0, 860, 0)
    for i in range(n):
        tri(top[i], top[(i + 1) % n], centre, GRASS)
    # rocks: low boulders on the beach and the hill (an irregular octahedron each)
    def boulder(cx, cy, cz, r, seed):
        rnd = random.Random(seed)
        k = 6
        mid = [(cx + r * (0.8 + .4 * rnd.random()) * math.cos(a * 2 * math.pi / k), cy + r * .35, cz - r * (0.8 + .4 * rnd.random()) * math.sin(a * 2 * math.pi / k)) for a in range(k)]
        base_ring = [(cx + .9 * (p[0] - cx), cy - r * .3, cz + .9 * (p[2] - cz)) for p in mid]
        peak = (cx + r * .1, cy + r * (1.0 + .3 * rnd.random()), cz)
        for a in range(k):
            b = (a + 1) % k
            tri(mid[a], mid[b], peak, ROCK, scale=150.0)
            quad(base_ring[a], base_ring[b], mid[b], mid[a], ROCK, scale=150.0)
    boulder(-650, 400, 450, 170, 1)
    boulder(560, 420, 520, 130, 2)
    boulder(-250, 760, -150, 120, 3)
    boulder(700, 360, -380, 150, 4)
    # wooden pier from the beach (+x side is open sea to the south: -z)
    x0, x1, zs, ze = -120, 120, -780, -1900
    deck_top0, deck_top1 = 440, 460
    p = [(x0, -100, ze), (x1, -100, ze), (x1, -100, zs), (x0, -100, zs),
         (x0, deck_top1, ze), (x1, deck_top1, ze), (x1, deck_top0, zs), (x0, deck_top0, zs)]
    length = (zs - ze) / 100
    quad(p[4], p[7], p[6], p[5], WOOD, ((0, 0), (0, length), (2.4, length), (2.4, 0)))     # deck
    quad(p[0], p[4], p[5], p[1], WOOD, ((0, 0), (0, 3), (2.4, 3), (2.4, 0)))               # end
    quad(p[1], p[5], p[6], p[2], WOOD, ((0, 0), (0, 3), (length, 3), (length, 0)))         # sides
    quad(p[3], p[7], p[4], p[0], WOOD, ((0, 0), (0, 3), (length, 3), (length, 0)))
    # fences along both pier edges (cut-out, double-sided): one fence texture repeat per 1.28 m
    for x in (x0 + 5, x1 - 5):
        a, b = (x, deck_top1, ze + 40), (x, deck_top0, zs - 20)
        span = (zs - 20 - (ze + 40)) / 128
        quad(a, b, (b[0], b[1] + 110, b[2]), (a[0], a[1] + 110, a[2]), FENCE, ((0, 1), (span, 1), (span, 0), (0, 0)))
    # bushes: two crossed cards each, on the grass (cut-out, double-sided)
    for bx, by, bz, s in ((-300, 640, 300, 160), (330, 620, 240, 140), (120, 700, -330, 150), (-420, 560, -200, 130)):
        for angle in (0.3, 0.3 + math.pi / 2):
            dx, dz = math.cos(angle) * s / 2, math.sin(angle) * s / 2
            quad((bx - dx, by - 10, bz - dz), (bx + dx, by - 10, bz + dz), (bx + dx, by + s, bz + dz), (bx - dx, by + s, bz - dz), BUSH,
                 ((0, 1), (1, 1), (1, 0), (0, 0)))
    return tris


# ---- GLB ----------------------------------------------------------------------------------------------

def make(atlas=False):
    tris = island()
    if atlas:  # the single-material comparison: same triangles minus the cut-outs, one 256 x 256 atlas
        tris = [t for t in tris if t[6] not in (FENCE, BUSH)]
    binary, views, accessors = bytearray(), [], []
    def part(body):
        binary.extend(b'\0' * (-len(binary) % 4)); views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(body)})
        binary.extend(body); return len(views) - 1
    def floats(rows, width):
        # rounded to 6 decimals, then to float32: the last-digit differences of sin/cos between C libraries vanish
        rows = [tuple(struct.unpack('<f', struct.pack('<f', round(x, 6)))[0] for x in row) for row in rows]
        vi = part(struct.pack('<%df' % (len(rows) * width), *(x for row in rows for x in row)))
        lo = [min(r[k] for r in rows) for k in range(width)]; hi = [max(r[k] for r in rows) for k in range(width)]
        accessors.append({'bufferView': vi, 'componentType': 5126, 'count': len(rows), 'type': 'VEC%d' % width, 'min': lo, 'max': hi})
        return len(accessors) - 1
    def indices(values):
        vi = part(struct.pack('<%dH' % len(values), *values))
        accessors.append({'bufferView': vi, 'componentType': 5123, 'count': len(values), 'type': 'SCALAR'}); return len(accessors) - 1
    def geometry(group, uv_map):
        positions, normals, uvs = [], [], []
        for t in group:
            a, b, c = t[:3]
            u = [b[k] - a[k] for k in range(3)]; v = [c[k] - a[k] for k in range(3)]
            nrm = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            length = math.sqrt(sum(x * x for x in nrm))
            for p, uv in zip((a, b, c), t[3:6]):
                positions.append(tuple(x / 100 for x in p)); normals.append(tuple(x / length for x in nrm)); uvs.append(uv_map(uv, t[6]))
        return positions, normals, uvs
    materials, images, textures, prims = [], [], [], []
    if atlas:
        # 2 x 2 atlas cells of 128 texels: the repeat cannot wrap inside a cell, so UVs are folded into it
        cell = {SAND: (0, 0), GRASS: (1, 0), ROCK: (0, 1), WOOD: (1, 1)}
        def fold(uv, m):
            u, v = uv[0] % 1.0, uv[1] % 1.0
            cu, cv = cell[m]
            return ((cu + 0.02 + 0.96 * u) / 2, (cv + 0.02 + 0.96 * v) / 2)
        pos, nrm, uv = geometry(tris, fold)
        big = [[(0, 0, 0, 0)] * 256 for _ in range(256)]
        for m, (cu, cv) in cell.items():
            px = texture(m)
            for y in range(SIZE):
                big[cv * SIZE + y][cu * SIZE:(cu + 1) * SIZE] = px[y]
        images.append({'bufferView': part(png(big)), 'mimeType': 'image/png'})
        textures.append({'source': 0})
        materials.append({'name': 'island', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}, 'metallicFactor': 0, 'roughnessFactor': 1}})
        prims.append({'attributes': {'POSITION': floats(pos, 3), 'NORMAL': floats(nrm, 3), 'TEXCOORD_0': floats(uv, 2)},
                      'indices': indices(list(range(len(pos)))), 'material': 0})
    else:
        for m in range(6):
            group = [t for t in tris if t[6] == m]
            pos, nrm, uv = geometry(group, lambda uv, _: uv)
            images.append({'bufferView': part(png(texture(m))), 'mimeType': 'image/png'})
            textures.append({'source': m})
            material = {'name': NAMES[m], 'pbrMetallicRoughness': {'baseColorTexture': {'index': m}, 'metallicFactor': 0, 'roughnessFactor': 1}}
            if m in (FENCE, BUSH):
                material.update(alphaMode='MASK', alphaCutoff=0.5, doubleSided=True)
            materials.append(material)
            prims.append({'attributes': {'POSITION': floats(pos, 3), 'NORMAL': floats(nrm, 3), 'TEXCOORD_0': floats(uv, 2)},
                          'indices': indices(list(range(len(pos)))), 'material': m})
    # collision: the walkable surfaces, welded, one primitive per ground material
    welded, points, col_prims = {}, [], []
    col_material = {}
    for m in (SAND, GRASS, ROCK, WOOD):
        values = []
        for t in tris:
            if t[6] != m:
                continue
            for p in t[:3]:
                key = tuple(round(x, 3) for x in p)
                if key not in welded:
                    welded[key] = len(points); points.append(tuple(x / 100 for x in p))
                values.append(welded[key])
        col_material[m] = len(materials) + len(col_material)
        col_prims.append((values, col_material[m]))
    colpos = floats(points, 3)
    collision = [{'attributes': {'POSITION': colpos}, 'indices': indices(values), 'material': mi} for values, mi in col_prims]
    for m in (SAND, GRASS, ROCK, WOOD):
        materials.append({'name': SURFACE[m]})
    doc = {'asset': {'version': '2.0', 'generator': 'Original several-material island example, MPL-2.0'}, 'scene': 0, 'scenes': [{'nodes': [0, 1]}],
           'nodes': [{'mesh': 0, 'name': 'island'}, {'mesh': 1, 'name': 'island_col'}],
           'meshes': [{'name': 'island', 'primitives': prims}, {'name': 'island_col', 'primitives': collision}],
           'buffers': [{'byteLength': len(binary)}], 'bufferViews': views, 'accessors': accessors,
           'materials': materials, 'textures': textures, 'images': images}
    text = json.dumps(doc, separators=(',', ':')).encode(); text += b' ' * (-len(text) % 4); binary.extend(b'\0' * (-len(binary) % 4))
    body = struct.pack('<I4s', len(text), b'JSON') + text + struct.pack('<I4s', len(binary), b'BIN\0') + binary
    return b'glTF' + struct.pack('<II', 2, len(body) + 12) + body


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--atlas':
        Path(sys.argv[2]).write_bytes(make(atlas=True))
    else:
        Path(__file__).with_name('island.glb').write_bytes(make())
