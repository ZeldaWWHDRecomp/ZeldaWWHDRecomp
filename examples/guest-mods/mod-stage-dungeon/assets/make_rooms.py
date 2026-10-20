#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Reproduce assets/hall.glb and assets/vault.glb, the two own rooms of the mod dungeon example; no external
assets. Game units in the comments (100 per metre); the floor is at -200, as in the lava cave's rooms.

hall (room 0): x -3600..-1000, z -900..900, 9 m high, with a doorway in its east wall (z -150..150) where the
knob door to the lava room stands, and one in its north wall (x -2250..-1950) for the small-key door.
vault (room 2): x -2600..-1600, z 900..2900 behind the small-key door; its far 3 m (z 2600..2900) is the exit
strip. Materials "floor" and "wall"; the visible triangles are the collision (stone)."""
import json
import math
import struct
from pathlib import Path

TILE = 4.0
Y0, H = -2.0, 7.0  # metres
DOOR_HALF, DOOR_TOP = 1.5, 1.0  # a doorway 3 m wide, up to y 1.0 (3 m high)


def add(parts, material, a, b, c, d):
    pos, nrm, uv, idx = parts.setdefault(material, ([], [], [], []))
    u = [b[k] - a[k] for k in range(3)]
    v = [d[k] - a[k] for k in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    ln = math.sqrt(sum(x * x for x in n))
    n = tuple(x / ln for x in n)
    k = len(pos)
    for p in (a, b, c, d):
        pos.append(p)
        nrm.append(n)
        uv.append((p[0] / TILE, p[2] / TILE) if abs(n[1]) > 0.5 else (p[2] / TILE, -p[1] / TILE) if abs(n[0]) > 0.5 else (p[0] / TILE, -p[1] / TILE))
    idx.extend((k, k + 1, k + 2, k, k + 2, k + 3))


def wall_x(parts, x, z0, z1, inward, gap=None):
    """a wall at x from z0 to z1, facing `inward` (+1: +x), with an optional doorway (gz0, gz1)"""
    spans = [(z0, z1)] if gap is None else [(z0, gap[0]), (gap[1], z1)]
    for a, b in spans:
        q = [(x, Y0, a), (x, Y0, b), (x, Y0 + H, b), (x, Y0 + H, a)]
        add(parts, 'wall', *(q if inward < 0 else q[::-1]))
    if gap:
        q = [(x, DOOR_TOP, gap[0]), (x, DOOR_TOP, gap[1]), (x, Y0 + H, gap[1]), (x, Y0 + H, gap[0])]
        add(parts, 'wall', *(q if inward < 0 else q[::-1]))


def wall_z(parts, z, x0, x1, inward, gap=None):
    spans = [(x0, x1)] if gap is None else [(x0, gap[0]), (gap[1], x1)]
    for a, b in spans:
        q = [(a, Y0, z), (b, Y0, z), (b, Y0 + H, z), (a, Y0 + H, z)]
        add(parts, 'wall', *(q if inward > 0 else q[::-1]))
    if gap:
        q = [(gap[0], DOOR_TOP, z), (gap[1], DOOR_TOP, z), (gap[1], Y0 + H, z), (gap[0], Y0 + H, z)]
        add(parts, 'wall', *(q if inward > 0 else q[::-1]))


def strips(lo, hi, step):
    out = [lo]
    while out[-1] + step < hi - 1e-6:
        out.append(out[-1] + step)
    return out + [hi]


def floor_and_ceiling(parts, x0, x1, z0, z1, step=3.0, along_z=False):
    """the floor in strips across x (or across z), seen from above, and the ceiling"""
    if along_z:
        for a, b in zip(strips(z0, z1, step), strips(z0, z1, step)[1:]):
            add(parts, 'floor', (x0, Y0, a), (x0, Y0, b), (x1, Y0, b), (x1, Y0, a))
    else:
        for a, b in zip(strips(x0, x1, step), strips(x0, x1, step)[1:]):
            add(parts, 'floor', (a, Y0, z0), (a, Y0, z1), (b, Y0, z1), (b, Y0, z0))
    add(parts, 'wall', (x0, Y0 + H, z0), (x1, Y0 + H, z0), (x1, Y0 + H, z1), (x0, Y0 + H, z1))


def hall():
    parts = {}
    floor_and_ceiling(parts, -36.0, -10.0, -9.0, 9.0)
    wall_x(parts, -36.0, -9.0, 9.0, +1)
    wall_x(parts, -10.0, -9.0, 9.0, -1, (-DOOR_HALF, DOOR_HALF))
    wall_z(parts, -9.0, -36.0, -10.0, +1)
    wall_z(parts, 9.0, -36.0, -10.0, -1, (-22.5, -19.5))
    return parts


def vault():
    parts = {}
    floor_and_ceiling(parts, -26.0, -16.0, 9.0, 29.0, along_z=True)
    wall_x(parts, -26.0, 9.0, 29.0, +1)
    wall_x(parts, -16.0, 9.0, 29.0, -1)
    wall_z(parts, 9.0, -26.0, -16.0, +1, (-22.5, -19.5))
    wall_z(parts, 29.0, -26.0, -16.0, -1)
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
    for name, (pos, nrm, uv, idx) in parts.items():
        p, n, t = floats(pos, 3), floats(nrm, 3), floats(uv, 2)
        accessors.append({'bufferView': view(struct.pack('<%dH' % len(idx), *idx)), 'componentType': 5123, 'count': len(idx), 'type': 'SCALAR'})
        materials.append({'name': name})
        primitives.append({'attributes': {'POSITION': p, 'NORMAL': n, 'TEXCOORD_0': t}, 'indices': len(accessors) - 1, 'material': len(materials) - 1})
    doc = {'asset': {'version': '2.0', 'generator': 'Original mod dungeon example, MPL-2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
           'nodes': [{'mesh': 0, 'name': 'room'}], 'meshes': [{'name': 'room', 'primitives': primitives}], 'materials': materials,
           'buffers': [{'byteLength': len(binary)}], 'bufferViews': views, 'accessors': accessors}
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    binary.extend(b'\0' * (-len(binary) % 4))
    body = struct.pack('<I4s', len(text), b'JSON') + text + struct.pack('<I4s', len(binary), b'BIN\0') + bytes(binary)
    return b'glTF' + struct.pack('<II', 2, 12 + len(body)) + body


if __name__ == '__main__':
    here = Path(__file__).parent
    (here / 'hall.glb').write_bytes(glb(hall()))
    (here / 'vault.glb').write_bytes(glb(vault()))
