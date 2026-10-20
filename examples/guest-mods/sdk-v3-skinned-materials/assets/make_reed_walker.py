#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Original reed walker: a skinned creature with three materials and idle/walk clips.

Materials: `cloth` (opaque; the lower body and the legs), `bark` (opaque; the upper body and the head
with its face) and `leaf` (cut-out MASK, double-sided; a leaf crest on the head and two leaf arms).
The body is one tube cut into two materials at the waist: the ring of vertices where they meet is in
both primitives with the same skin weights (half hips, half body), so a bend at the waist shows
whether the two parts deform together. Units are metres, positive Y is up, the face looks along +Z.

The output is the same bytes on every platform: PNG data uses stored (uncompressed) deflate blocks and
every float is rounded before it is packed. Run with Python 3 to write reed_walker.glb next to it.
"""
import json
import math
import pathlib
import struct
import zlib

SIZE = 128


def stored_zlib(data):
    """zlib stream with uncompressed (stored) deflate blocks: the same bytes on every platform, unlike
    zlib.compress, whose output depends on the zlib build."""
    out = bytearray(b'\x78\x01')
    for start in range(0, len(data), 65535):
        block = data[start:start + 65535]
        out += bytes([1 if start + 65535 >= len(data) else 0]) + struct.pack('<HH', len(block), len(block) ^ 0xffff) + block
    return bytes(out + struct.pack('>I', zlib.adler32(data) & 0xffffffff))


def png(pixel):
    """A SIZE x SIZE RGBA PNG from pixel(x, y) -> (r, g, b, a)."""
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    rows = b''.join(b'\0' + bytes(v for x in range(SIZE) for v in pixel(x, y)) for y in range(SIZE))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', SIZE, SIZE, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', stored_zlib(rows)) + chunk(b'IEND', b''))


def cloth(x, y):
    # deep blue cloth with light diagonal stripes
    stripe = ((x + y) // 12) % 2 == 0
    return (40, 86, 168, 255) if stripe else (226, 236, 246, 255)


def bark(x, y):
    # warm orange bark with darker grain; the face (two eyes and a mouth) sits in the upper middle
    eye = (34 <= x < 52 or 76 <= x < 94) and 26 <= y < 44
    mouth = 52 <= x < 76 and 60 <= y < 66
    if eye or mouth:
        return (24, 20, 30, 255)
    grain = (y // 6) % 3 == 0
    return (176, 92, 36, 255) if grain else (226, 132, 52, 255)


def leaf(x, y):
    # a green leaf (an ellipse with a light middle vein) over transparency
    u, v = (x + 0.5) / SIZE * 2 - 1, (y + 0.5) / SIZE * 2 - 1
    if (u / 0.62) ** 2 + v * v > 1:
        return (0, 0, 0, 0)
    if abs(u) < 0.05:
        return (210, 240, 150, 255)
    return (52, 150, 60, 255) if (y // 10) % 2 else (70, 178, 74, 255)


# joints: name, parent, world position of the joint (identity bind rotations)
JOINTS = [('hips', -1, (0, .45, 0)), ('body', 0, (0, .85, 0)), ('head', 1, (0, 1.25, 0)),
          ('leg_l', 0, (-.11, .45, 0)), ('leg_r', 0, (.11, .45, 0)),
          ('arm_l', 1, (-.24, 1.08, 0)), ('arm_r', 1, (.24, 1.08, 0))]
HIPS, BODY, HEAD, LEG_L, LEG_R, ARM_L, ARM_R = range(7)


class Mesh:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.joints, self.weights, self.indices = [], [], [], [], [], []

    def vertex(self, p, n, t, influence):
        (a, b), (wa, wb) = influence
        self.pos.append(p); self.nrm.append(n); self.uv.append(t)
        self.joints.append((a, b, 0, 0)); self.weights.append((wa, wb, 0., 0.))
        return len(self.pos) - 1

    def quad(self, corners, normal, uvs, influence):
        at = [self.vertex(p, normal, t, influence(p)) for p, t in zip(corners, uvs)]
        self.indices += [at[0], at[1], at[2], at[0], at[2], at[3]]

    def box(self, lo, hi, influence, side_uv=((0, 1), (1, 1), (1, 0), (0, 0))):
        """the +Z face takes the whole texture, the other faces side_uv"""
        x, y, z = lo; X, Y, Z = hi
        for corners, normal in (([(x, y, Z), (X, y, Z), (X, Y, Z), (x, Y, Z)], (0, 0, 1)),
                                ([(X, y, z), (x, y, z), (x, Y, z), (X, Y, z)], (0, 0, -1)),
                                ([(X, y, Z), (X, y, z), (X, Y, z), (X, Y, Z)], (1, 0, 0)),
                                ([(x, y, z), (x, y, Z), (x, Y, Z), (x, Y, z)], (-1, 0, 0)),
                                ([(x, Y, Z), (X, Y, Z), (X, Y, z), (x, Y, z)], (0, 1, 0)),
                                ([(x, y, z), (X, y, z), (X, y, Z), (x, y, Z)], (0, -1, 0))):
            uv = ((0, 1), (1, 1), (1, 0), (0, 0)) if normal == (0, 0, 1) else side_uv
            self.quad(corners, normal, uv, influence)


SEGMENTS = 16
RADIUS = .21


def body_weights(y):
    """hips below 0.65 m, body above 1.05 m, a linear blend between (half and half at the 0.85 m waist)"""
    t = min(1., max(0., (y - .65) / .4))
    return ((HIPS, BODY), (1 - t, t)) if 0 < t < 1 else (((HIPS, HIPS), (1., 0.)) if t == 0 else ((BODY, BODY), (1., 0.)))


def tube(mesh, rings):
    """a closed tube around Y through rings (y values), skinned by body_weights; u around, v along
    (0.6 to 1.0 from the neck down: below the face on the bark texture)"""
    first = len(mesh.pos)
    for y in rings:
        for s in range(SEGMENTS + 1):
            a = 2 * math.pi * s / SEGMENTS
            n = (math.sin(a), 0., math.cos(a))
            mesh.vertex((RADIUS * n[0], y, RADIUS * n[2]), n, (s / SEGMENTS, .6 + (1.25 - y) / 2), body_weights(y))
    row = SEGMENTS + 1
    for r in range(len(rings) - 1):
        for s in range(SEGMENTS):
            a, b = first + r * row + s, first + r * row + s + 1
            c, d = a + row, b + row
            mesh.indices += [a, b, d, a, d, c]


def card(mesh, centre, half_width, half_height, axis, joint):
    """a leaf card standing upright: in the XY plane (axis 'z') or the ZY plane (axis 'x')"""
    cx, cy, cz = centre
    if axis == 'z':
        corners = [(cx - half_width, cy - half_height, cz), (cx + half_width, cy - half_height, cz),
                   (cx + half_width, cy + half_height, cz), (cx - half_width, cy + half_height, cz)]
        normal = (0., 0., 1.)
    else:
        corners = [(cx, cy - half_height, cz + half_width), (cx, cy - half_height, cz - half_width),
                   (cx, cy + half_height, cz - half_width), (cx, cy + half_height, cz + half_width)]
        normal = (1., 0., 0.)
    mesh.quad(corners, normal, [(0, 1), (1, 1), (1, 0), (0, 0)], lambda p: ((joint, joint), (1., 0.)))


def arm_card(mesh, side, joint):
    """a leaf arm hanging out and down from the shoulder, in the XY plane"""
    x0 = side * .2
    x1 = side * .52
    corners = [(x0, 1.12, 0.), (x1, .80, 0.), (x1 + side * .06, .96, 0.), (x0 + side * .04, 1.20, 0.)]
    if side < 0:
        corners = [corners[1], corners[0], corners[3], corners[2]]
    mesh.quad(corners, (0., 0., 1.), [(0, 1), (1, 1), (1, 0), (0, 0)], lambda p: ((joint, joint), (1., 0.)))


def geometry():
    cloth_mesh, bark_mesh, leaf_mesh = Mesh(), Mesh(), Mesh()
    tube(cloth_mesh, [.45, .55, .65, .75, .85])           # lower body: cloth, ends at the waist ring
    tube(bark_mesh, [.85, .95, 1.05, 1.15, 1.25])          # upper body: bark, starts at the same ring
    for side, joint in ((-1, LEG_L), (1, LEG_R)):          # legs: cloth
        cloth_mesh.box((side * .11 - .07, 0., -.08), (side * .11 + .07, .47, .09), lambda p, j=joint: ((j, j), (1., 0.)))
    bark_mesh.box((-.25, 1.2, -.22), (.25, 1.62, .22), lambda p: ((HEAD, HEAD), (1., 0.)), ((0, 1), (1, 1), (1, .75), (0, .75)))  # head with the face in front
    card(leaf_mesh, (0., 1.79, 0.), .16, .19, 'z', HEAD)  # crest: two crossed leaves
    card(leaf_mesh, (0., 1.79, 0.), .16, .19, 'x', HEAD)
    arm_card(leaf_mesh, -1, ARM_L)
    arm_card(leaf_mesh, 1, ARM_R)
    return [('cloth', cloth_mesh), ('bark', bark_mesh), ('leaf', leaf_mesh)]


def quaternion(axis, angle):
    q = [0., 0., 0., math.cos(angle / 2)]
    q[axis] = math.sin(angle / 2)
    return q


X, Y, Z = 0, 1, 2
# clips: name -> (seconds per loop, [(joint, path, values at evenly spaced keys)])
CLIPS = {
    'idle': (2.0, [(BODY, 'rotation', [quaternion(X, a) for a in (0, .32, 0, -.12, 0)]),
                   (HEAD, 'rotation', [quaternion(Z, a) for a in (0, .14, 0, -.14, 0)]),
                   (ARM_L, 'rotation', [quaternion(Z, a) for a in (0, -.35, 0, .2, 0)]),
                   (ARM_R, 'rotation', [quaternion(Z, a) for a in (0, .35, 0, -.2, 0)])]),
    'walk': (1.0, [(LEG_L, 'rotation', [quaternion(X, a) for a in (0, .5, 0, -.5, 0)]),
                   (LEG_R, 'rotation', [quaternion(X, a) for a in (0, -.5, 0, .5, 0)]),
                   (ARM_L, 'rotation', [quaternion(X, a) for a in (0, -.45, 0, .45, 0)]),
                   (ARM_R, 'rotation', [quaternion(X, a) for a in (0, .45, 0, -.45, 0)]),
                   (BODY, 'rotation', [quaternion(Y, a) for a in (0, .22, 0, -.22, 0)]),
                   (HIPS, 'translation', [[0, .45 + d, 0] for d in (0, .035, 0, .035, 0)])]),
}


def rounded(values):
    return [round(float(v), 6) + 0. for v in values]


def make():
    binary, views, accessors = bytearray(), [], []

    def view(body):
        binary.extend(b'\0' * (-len(binary) % 4))
        views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(body)})
        binary.extend(body)
        return len(views) - 1

    def accessor(rows, kind, component=5126):
        flat = [v for row in rows for v in row] if isinstance(rows[0], (list, tuple)) else list(rows)
        code = {5126: 'f', 5123: 'H', 5121: 'B'}[component]
        if component == 5126:
            flat = rounded(flat)
        entry = {'bufferView': view(struct.pack('<%d%s' % (len(flat), code), *flat)), 'componentType': component, 'count': len(rows), 'type': kind}
        if kind == 'VEC3' and component == 5126 and len(rows) > 0:
            entry['min'] = [min(r[k] for r in rows) for k in range(3)]
            entry['max'] = [max(r[k] for r in rows) for k in range(3)]
            entry['min'], entry['max'] = rounded(entry['min']), rounded(entry['max'])
        accessors.append(entry)
        return len(accessors) - 1

    primitives = []
    for index, (_, mesh) in enumerate(geometry()):
        attributes = {'POSITION': accessor(mesh.pos, 'VEC3'), 'NORMAL': accessor(mesh.nrm, 'VEC3'),
                      'TEXCOORD_0': accessor(mesh.uv, 'VEC2'), 'JOINTS_0': accessor(mesh.joints, 'VEC4', 5121),
                      'WEIGHTS_0': accessor(mesh.weights, 'VEC4')}
        primitives.append({'attributes': attributes, 'indices': accessor(mesh.indices, 'SCALAR', 5123), 'material': index})
    inverse = []
    for _, _, (x, y, z) in JOINTS:
        inverse.append([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, -z, 1])
    binds = accessor(inverse, 'MAT4')
    animations = []
    for name, (seconds, channels) in CLIPS.items():
        samplers, targets = [], []
        for joint, path, values in channels:
            times = accessor([seconds * k / (len(values) - 1) for k in range(len(values))], 'SCALAR')
            output = accessor(values, 'VEC4' if path == 'rotation' else 'VEC3')
            samplers.append({'input': times, 'output': output, 'interpolation': 'LINEAR'})
            targets.append({'sampler': len(samplers) - 1, 'target': {'node': 1 + joint, 'path': path}})
        animations.append({'name': name, 'samplers': samplers, 'channels': targets})
    images = [{'bufferView': view(png(pixel)), 'mimeType': 'image/png'} for pixel in (cloth, bark, leaf)]
    nodes = [{'name': 'reed_walker', 'mesh': 0, 'skin': 0}]
    for i, (name, parent, position) in enumerate(JOINTS):
        local = position if parent < 0 else tuple(a - b for a, b in zip(position, JOINTS[parent][2]))
        node = {'name': name, 'translation': rounded(local)}
        children = [1 + k for k, (_, p, _) in enumerate(JOINTS) if p == i]
        if children:
            node['children'] = children
        nodes.append(node)
    materials = [{'name': 'cloth', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}, 'metallicFactor': 0, 'roughnessFactor': 1}},
                 {'name': 'bark', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 1}, 'metallicFactor': 0, 'roughnessFactor': 1}},
                 {'name': 'leaf', 'alphaMode': 'MASK', 'alphaCutoff': 0.5, 'doubleSided': True,
                  'pbrMetallicRoughness': {'baseColorTexture': {'index': 2}, 'metallicFactor': 0, 'roughnessFactor': 1}}]
    binary.extend(b'\0' * (-len(binary) % 4))
    doc = {'asset': {'version': '2.0', 'generator': 'Original MPL-2.0 reed walker'}, 'scene': 0, 'scenes': [{'nodes': [0, 1]}],
           'nodes': nodes, 'skins': [{'joints': list(range(1, 1 + len(JOINTS))), 'skeleton': 1, 'inverseBindMatrices': binds}],
           'animations': animations, 'meshes': [{'name': 'reed_walker', 'primitives': primitives}], 'materials': materials,
           'textures': [{'source': i} for i in range(3)], 'images': images,
           'buffers': [{'byteLength': len(binary)}], 'bufferViews': views, 'accessors': accessors}
    text = json.dumps(doc, separators=(',', ':'), sort_keys=True).encode()
    text += b' ' * (-len(text) % 4)
    body = struct.pack('<I4s', len(text), b'JSON') + text + struct.pack('<I4s', len(binary), b'BIN\0') + bytes(binary)
    return b'glTF' + struct.pack('<II', 2, len(body) + 12) + body


if __name__ == '__main__':
    pathlib.Path(__file__).with_name('reed_walker.glb').write_bytes(make())
