#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Reproduce assets/island.glb and assets/arena.glb, the two own rooms of the sky island example; no external
assets. Metres, as in any .glb (the game counts 100 units per metre).

island (room 0): a floating island about 44 m across, its grass top at y 0 rising a little to the middle, a rocky
rim and an underside that narrows to a point 26 m below. A raised stone path leads north to the arena's gate.
arena (room 1): an open-air ring 24 m across north of the island (centre z 36), its floor at y 2, walled 5 m
high, with a gateway (3 m wide) in its south wall facing the island. Materials "grass", "rock", "stone" and
"wall"; the visible triangles are the collision."""
import json
import math
import struct
from pathlib import Path

TILE = 4.0
R_TOP = 22.0       # the island's radius
GATE_Z = 22.0      # where the path meets the arena's gateway
ARENA_Z, ARENA_R, ARENA_Y, WALL_H = 36.0, 12.0, 2.0, 5.0
GATE_HALF, GATE_TOP = 1.5, 3.0


def wobble(a, seed):
    """a fixed, hand-made irregularity of the rim (no randomness, the file is reproducible)"""
    return 1.0 + 0.08 * math.sin(3 * a + seed) + 0.05 * math.sin(7 * a + 2 * seed) + 0.03 * math.sin(13 * a)


def tri(parts, material, a, b, c, uv_mode='top'):
    pos, nrm, uv, idx = parts.setdefault(material, ([], [], [], []))
    u = [b[k] - a[k] for k in range(3)]
    v = [c[k] - a[k] for k in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    ln = math.sqrt(sum(x * x for x in n)) or 1.0
    n = tuple(x / ln for x in n)
    k = len(pos)
    for p in (a, b, c):
        pos.append(p)
        nrm.append(n)
        if abs(n[1]) > 0.6:
            uv.append((p[0] / TILE, p[2] / TILE))
        else:  # walls and cliffs: around the side, and down
            uv.append(((math.atan2(p[2], p[0]) * R_TOP) / TILE if uv_mode == 'round' else (p[0] + p[2]) / TILE, -p[1] / TILE))
    idx.extend((k, k + 1, k + 2))


def quad(parts, material, a, b, c, d, uv_mode='top'):
    tri(parts, material, a, b, c, uv_mode)
    tri(parts, material, a, c, d, uv_mode)


def island():
    parts = {}
    seg, rings = 48, 6
    def top(r_frac, a):
        r = R_TOP * r_frac * wobble(a, 1.3)
        return (r * math.cos(a), 0.6 * (1 - r_frac * r_frac), r * math.sin(a))
    # the grass top: rings around the middle, seen from above (counter-clockwise from above: +y normals)
    for i in range(seg):
        a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
        tri(parts, 'grass', top(0, 0), top(1 / rings, a1), top(1 / rings, a0))
        for k in range(1, rings):
            f0, f1 = k / rings, (k + 1) / rings
            quad(parts, 'grass', top(f0, a0), top(f0, a1), top(f1, a1), top(f1, a0))
    # the rim and the underside: rock rings down to the point
    under = [(1.0, 0.0), (1.02, -2.5), (0.9, -6.0), (0.7, -11.0), (0.45, -17.0), (0.2, -22.0)]
    for i in range(seg):
        a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
        def ring(j, a):
            f, y = under[j]
            r = R_TOP * f * wobble(a, 1.3 + 0.7 * j)
            return (r * math.cos(a), y if j else top(1, a)[1], r * math.sin(a))
        for j in range(len(under) - 1):
            quad(parts, 'rock', ring(j, a1), ring(j + 1, a1), ring(j + 1, a0), ring(j, a0), 'round')
        tri(parts, 'rock', ring(len(under) - 1, a1), (0.0, -26.0, 0.0), ring(len(under) - 1, a0), 'round')
    # the stone path north to the arena's gate: 3 m wide, a little above the grass
    for z0 in range(0, int(GATE_Z), 2):
        z1 = min(z0 + 2, GATE_Z)
        y0, y1 = 0.7 + (ARENA_Y - 0.7) * max(0.0, (z0 - 14) / 8), 0.7 + (ARENA_Y - 0.7) * max(0.0, (z1 - 14) / 8)
        quad(parts, 'stone', (-1.5, y0, z0), (-1.5, y1, z1), (1.5, y1, z1), (1.5, y0, z0))
    return parts


def arena():
    parts = {}
    seg = 40
    gate_a = -math.pi / 2  # the gateway faces the island (south, -z)
    def at(r, a, y):
        return (r * math.cos(a), y, ARENA_Z + r * math.sin(a))
    for i in range(seg):
        a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
        # the floor: a stone disc
        tri(parts, 'stone', (0.0, ARENA_Y, ARENA_Z), at(ARENA_R, a1, ARENA_Y), at(ARENA_R, a0, ARENA_Y))
        # the wall, facing inwards, open at the gateway
        mid = (a0 + a1) / 2
        gap = abs(math.atan2(math.sin(mid - gate_a), math.cos(mid - gate_a))) * ARENA_R < GATE_HALF
        lo = ARENA_Y + (GATE_TOP if gap else 0.0)
        quad(parts, 'wall', at(ARENA_R, a1, lo), at(ARENA_R, a1, ARENA_Y + WALL_H), at(ARENA_R, a0, ARENA_Y + WALL_H), at(ARENA_R, a0, lo), 'round')
        # the wall's outside and top (seen from the island)
        quad(parts, 'wall', at(ARENA_R + 0.8, a0, lo), at(ARENA_R + 0.8, a0, ARENA_Y + WALL_H), at(ARENA_R + 0.8, a1, ARENA_Y + WALL_H), at(ARENA_R + 0.8, a1, lo), 'round')
        quad(parts, 'stone', at(ARENA_R, a1, ARENA_Y + WALL_H), at(ARENA_R + 0.8, a1, ARENA_Y + WALL_H), at(ARENA_R + 0.8, a0, ARENA_Y + WALL_H), at(ARENA_R, a0, ARENA_Y + WALL_H))
        # the ring's rock base below the floor
        quad(parts, 'rock', at(ARENA_R + 0.8, a0, ARENA_Y - 6), at(ARENA_R + 0.8, a0, ARENA_Y), at(ARENA_R + 0.8, a1, ARENA_Y), at(ARENA_R + 0.8, a1, ARENA_Y - 6), 'round')
    # the gateway's floor between the island's path and the ring
    quad(parts, 'stone', (-GATE_HALF, ARENA_Y, GATE_Z), (-GATE_HALF, ARENA_Y, ARENA_Z - ARENA_R + 0.5), (GATE_HALF, ARENA_Y, ARENA_Z - ARENA_R + 0.5), (GATE_HALF, ARENA_Y, GATE_Z))
    return parts


def glb(parts):
    binary = bytearray()
    views, accessors, primitives, materials = [], [], [], []

    def view(body):
        binary.extend(b'\0' * (-len(binary) % 4))
        views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(body)})
        binary.extend(body)
        return len(views) - 1

    def floats(rows, width):
        accessors.append({'bufferView': view(struct.pack('<%df' % (len(rows) * width), *(x for r in rows for x in r))), 'componentType': 5126,
                          'count': len(rows), 'type': 'VEC%d' % width, 'min': [min(r[k] for r in rows) for k in range(width)],
                          'max': [max(r[k] for r in rows) for k in range(width)]})
        return len(accessors) - 1
    for name, (pos, nrm, uv, idx) in sorted(parts.items()):
        p, n, t = floats(pos, 3), floats(nrm, 3), floats(uv, 2)
        accessors.append({'bufferView': view(struct.pack('<%dH' % len(idx), *idx)), 'componentType': 5123, 'count': len(idx), 'type': 'SCALAR'})
        materials.append({'name': name})
        primitives.append({'attributes': {'POSITION': p, 'NORMAL': n, 'TEXCOORD_0': t}, 'indices': len(accessors) - 1, 'material': len(materials) - 1})
    doc = {'asset': {'version': '2.0', 'generator': 'Original sky island example, MPL-2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
           'nodes': [{'mesh': 0, 'name': 'room'}], 'meshes': [{'name': 'room', 'primitives': primitives}], 'materials': materials,
           'buffers': [{'byteLength': len(binary)}], 'bufferViews': views, 'accessors': accessors}
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    binary.extend(b'\0' * (-len(binary) % 4))
    return b'glTF' + struct.pack('<II', 2, 12 + 8 + len(text) + 8 + len(binary)) + struct.pack('<I4s', len(text), b'JSON') + text + \
        struct.pack('<I4s', len(binary), b'BIN\0') + bytes(binary)


if __name__ == '__main__':
    here = Path(__file__).resolve().parent
    for name, make in (('island', island), ('arena', arena)):
        parts = make()  # in metres, as a .glb is (the game's units are centimetres)
        (here / (name + '.glb')).write_bytes(glb(parts))
        print(name, sum(len(p[3]) // 3 for p in parts.values()), 'triangles')
