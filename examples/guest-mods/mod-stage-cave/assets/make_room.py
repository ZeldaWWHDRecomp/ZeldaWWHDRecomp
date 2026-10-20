#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Reproduce assets/room.glb: an original cave chamber for the mod stage example; no external assets.

The chamber is 24 m long (x -12..12) and 16 m wide (z -8..8) with its floor at y 0, walls and a ceiling 6 m
high, four pillars and a low ledge. The last 3 m of the floor (x 9..12) is the exit strip the manifest's exit
box covers. Materials: "floor", "wall" and "pillar" (the manifest draws each with a material of the kit room);
there is no _col mesh, so the visible triangles are the collision (stone)."""
import json
import math
import struct
from pathlib import Path

TILE = 4.0  # metres per texture repeat


def make():
    parts = {'floor': ([], [], [], []), 'wall': ([], [], [], []), 'pillar': ([], [], [], [])}

    def quad(material, a, b, c, d):
        """a b c d counter-clockwise seen from the side the face shows"""
        pos, nrm, uv, idx = parts[material]
        u = [b[k] - a[k] for k in range(3)]
        v = [d[k] - a[k] for k in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        ln = math.sqrt(sum(x * x for x in n))
        n = tuple(x / ln for x in n)
        k = len(pos)
        for p in (a, b, c, d):
            pos.append(p)
            nrm.append(n)
            if abs(n[1]) > 0.5:
                uv.append((p[0] / TILE, p[2] / TILE))
            elif abs(n[0]) > 0.5:
                uv.append((p[2] / TILE, -p[1] / TILE))
            else:
                uv.append((p[0] / TILE, -p[1] / TILE))
        idx.extend((k, k + 1, k + 2, k, k + 2, k + 3))

    def box(material, x0, x1, y0, y1, z0, z1):
        """a solid box seen from outside (no bottom)"""
        quad(material, (x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0))   # top
        quad(material, (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))   # +z
        quad(material, (x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0))   # -z
        quad(material, (x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1))   # +x
        quad(material, (x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0))   # -x

    X0, X1, Z0, Z1, H = -12.0, 12.0, -8.0, 8.0, 6.0
    # the floor in strips (smaller triangles light better), seen from above
    xs = [X0 + 3 * i for i in range(9)]
    for a, b in zip(xs, xs[1:]):
        quad('floor', (a, 0, Z0), (a, 0, Z1), (b, 0, Z1), (b, 0, Z0))
    # walls and ceiling, seen from inside
    quad('wall', (X0, 0, Z0), (X1, 0, Z0), (X1, H, Z0), (X0, H, Z0))
    quad('wall', (X1, 0, Z1), (X0, 0, Z1), (X0, H, Z1), (X1, H, Z1))
    quad('wall', (X0, 0, Z1), (X0, 0, Z0), (X0, H, Z0), (X0, H, Z1))
    quad('wall', (X1, 0, Z0), (X1, 0, Z1), (X1, H, Z1), (X1, H, Z0))
    quad('wall', (X0, H, Z0), (X1, H, Z0), (X1, H, Z1), (X0, H, Z1))
    for x in (-5.0, 3.0):
        for z in (-5.0, 5.0):
            box('pillar', x - 0.6, x + 0.6, 0.0, H, z - 0.6, z + 0.6)
    box('pillar', -12.0, -9.0, 0.0, 0.5, 4.0, 8.0)  # a low ledge in a corner
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
        lo = [min(r[k] for r in rows) for k in range(width)]
        hi = [max(r[k] for r in rows) for k in range(width)]
        accessors.append({'bufferView': view(struct.pack('<%df' % (len(rows) * width), *(x for r in rows for x in r))),
                          'componentType': 5126, 'count': len(rows), 'type': 'VEC%d' % width, 'min': lo, 'max': hi})
        return len(accessors) - 1
    for name, (pos, nrm, uv, idx) in parts.items():
        p, n, t = floats(pos, 3), floats(nrm, 3), floats(uv, 2)
        accessors.append({'bufferView': view(struct.pack('<%dH' % len(idx), *idx)), 'componentType': 5123, 'count': len(idx), 'type': 'SCALAR'})
        materials.append({'name': name, 'pbrMetallicRoughness': {'baseColorFactor': [0.5, 0.4, 0.35, 1], 'metallicFactor': 0, 'roughnessFactor': 1}})
        primitives.append({'attributes': {'POSITION': p, 'NORMAL': n, 'TEXCOORD_0': t}, 'indices': len(accessors) - 1, 'material': len(materials) - 1})
    doc = {'asset': {'version': '2.0', 'generator': 'Original cave chamber example, MPL-2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
           'nodes': [{'mesh': 0, 'name': 'chamber'}], 'meshes': [{'name': 'chamber', 'primitives': primitives}], 'materials': materials,
           'buffers': [{'byteLength': len(binary)}], 'bufferViews': views, 'accessors': accessors}
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    binary.extend(b'\0' * (-len(binary) % 4))
    body = struct.pack('<I4s', len(text), b'JSON') + text + struct.pack('<I4s', len(binary), b'BIN\0') + bytes(binary)
    return b'glTF' + struct.pack('<II', 2, 12 + len(body)) + body


if __name__ == '__main__':
    Path(__file__).with_name('room.glb').write_bytes(glb(make()))
