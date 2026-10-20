# SPDX-License-Identifier: MPL-2.0
"""The island's own ground texture under a few places, from the player's room archive.

A model material may ask for it (glTF material extras: {"wwhd_island": {"room": n, "points": [[x, z], ...]}}, world
units on the Great Sea): the converter looks at the room's background model (its visible ground) straight down at each
point, takes the colour texture of the material most of them stand on, decodes its first level (BC1, BC3 or RGBA8),
scales it to at most 256 texels a side and uses it as that material's texture, so a bank the editor's water tool
makes is painted with the grass, sand or rock it stands on. Nothing from the game is in a package: the reference is
the room and the places; the texture is read from the player's installation into their model cache.
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notebook_model as base  # noqa: E402
import game_piece_model as gp  # noqa: E402

require = gp.require
LIMIT = 256
SHADE = 216  # / 256


def room_centre(room):
    """The Great Sea: 7 x 7 squares of 100000 units, room 1 in the north-west (editor/commands.cpp sea_room_centre)."""
    require(1 <= room <= 49, 'not a room of the Great Sea')
    return ((room - 1) % 7 - 3) * 100000.0, ((room - 1) // 7 - 3) * 100000.0


def uv_reader(src, s):
    """the texture coordinates (_u0) of a BG shape's vertices"""
    attrs = src.rel(s.vertex + 0x10)
    for i in range(s.nattr):
        if src.cstr(src.rel(attrs + i * 12)) != '_u0':
            continue
        offset, fmt = src.u16(attrs + i * 12 + 6), src.u32(attrs + i * 12 + 8)
        if fmt == 0x80D:
            return [struct.unpack_from('>2f', r, offset) for r in s.records]
        if fmt == 0x808:
            return [struct.unpack_from('>2e', r, offset) for r in s.records]
        if fmt == 0x007:
            return [tuple(v / 65535 for v in struct.unpack_from('>2H', r, offset)) for r in s.records]
    return None


def under(shapes, x, z):
    """(shape, barycentric weights, triangle) of the highest BG triangle straight below (x, z) (room-local), or None."""
    best, height = None, None
    for s in shapes:
        for a, b, c in s.tris:
            pa, pb, pc = s.pos[a], s.pos[b], s.pos[c]
            if x < min(pa[0], pb[0], pc[0]) or x > max(pa[0], pb[0], pc[0]) or z < min(pa[2], pb[2], pc[2]) or z > max(pa[2], pb[2], pc[2]):
                continue
            d = (pb[2] - pc[2]) * (pa[0] - pc[0]) + (pc[0] - pb[0]) * (pa[2] - pc[2])
            if abs(d) < 1e-9:
                continue
            u = ((pb[2] - pc[2]) * (x - pc[0]) + (pc[0] - pb[0]) * (z - pc[2])) / d
            v = ((pc[2] - pa[2]) * (x - pc[0]) + (pa[0] - pc[0]) * (z - pc[2])) / d
            w = 1 - u - v
            if min(u, v, w) < -1e-6:
                continue
            y = u * pa[1] + v * pb[1] + w * pc[1]
            if height is None or y > height:
                best, height = (s, (u, v, w), (a, b, c)), y
    return best


def colour_texture(src, material):
    """The texture a BG material's albedo sampler (_a0, else the first) reads."""
    textures = src.rel(material + 0x28)
    count = src.b[material + 0x10]
    samplers = dict(src.dict(src.rel(material + 0x30)))
    first = src.rel(material + 0x2C)
    index = (samplers['_a0'] - first) // 24 if '_a0' in samplers else 0
    require(0 <= index < count, 'a BG material without a colour texture')
    return src.cstr(src.rel(textures + 8 * index))


def bc3_alpha(block):
    a0, a1 = block[0], block[1]
    bits = int.from_bytes(block[2:8], 'little')
    if a0 > a1:
        pal = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        pal = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    return [pal[(bits >> (3 * i)) & 7] for i in range(16)]


def decode(src, texture):
    """width, height and RGBA8 texels of a texture's first level."""
    surf = texture + 4
    width, height, fmt, mode, swizzle, pitch = (src.u32(surf + 4), src.u32(surf + 8), src.u32(surf + 0x14), src.u32(surf + 0x30),
                                               src.u32(surf + 0x34), src.u32(surf + 0x3C))
    data = src.rel(texture + 0xB0)
    rgba = bytearray(width * height * 4)
    if fmt & 0xFF in (0x31, 0x33):
        bpp = 64 if fmt & 0xFF == 0x31 else 128
        wb, hb = width // 4, height // 4
        require(width % 4 == 0 and height % 4 == 0, 'unsupported texture size')
        p = pitch if mode == 1 else -(-wb // 32) * 32
        for by in range(hb):
            for bx in range(wb):
                at = data + gp.tiled_address(bx, by, p, bpp, mode, swizzle)
                if bpp == 64:
                    texels = gp.bc1_texels(bytes(src.b[at:at + 8]))
                else:
                    alpha = bc3_alpha(bytes(src.b[at:at + 8]))
                    texels = [t[:3] + (a,) for t, a in zip(gp.bc1_texels(bytes(src.b[at + 8:at + 16])), alpha)]
                for i, t in enumerate(texels):
                    x, y = bx * 4 + (i & 3), by * 4 + (i >> 2)
                    rgba[(y * width + x) * 4:(y * width + x) * 4 + 4] = bytes(t)
    elif fmt & 0xFF == 0x1A and mode == 1:
        for y in range(height):
            rgba[y * width * 4:(y + 1) * width * 4] = src.b[data + y * pitch * 4:data + y * pitch * 4 + width * 4]
    else:
        raise base.BuildError('unsupported island texture format')
    return width, height, bytes(rgba)


def band(width, height, rgba, v):
    """The texture's rows around V = v (a ground atlas: grass, a path, sand, one above the other), 64 texels high,
    its last rows blended into the rows above the band so that it repeats both ways without a seam."""
    rows = min(64, height)
    top = int(round((v % 1.0) * height)) - rows // 2
    row = lambda i: rgba[(i % height) * width * 4:(i % height + 1) * width * 4]
    picked = [bytearray(row(top + i)) for i in range(rows)]
    blend = rows // 4
    for i in range(blend):  # the band's end fades into what lies just above its start
        w = (i + 1) / (blend + 1)
        above = row(top - blend + i)
        line = picked[rows - blend + i]
        for k in range(len(line)):
            line[k] = int(round(line[k] * (1 - w) + above[k] * w))
    return width, rows, b''.join(bytes(r) for r in picked)


def scaled(width, height, rgba, limit=LIMIT):
    """Box-filtered to at most `limit` a side, then repeated to a width of 64 and a height of 8 at least."""
    factor = 1
    while width // factor > limit or height // factor > limit:
        factor *= 2
    nw, nh = width // factor, height // factor
    out = bytearray(nw * nh * 4)
    for y in range(nh):
        for x in range(nw):
            acc = [0, 0, 0, 0]
            for dy in range(factor):
                for dx in range(factor):
                    at = ((y * factor + dy) * width + x * factor + dx) * 4
                    for k in range(4):
                        acc[k] += rgba[at + k]
            n = factor * factor
            out[(y * nw + x) * 4:(y * nw + x) * 4 + 4] = bytes((v + n // 2) // n for v in acc)
    rx = -(-64 // nw) if nw < 64 else 1
    ry = -(-8 // nh) if nh < 8 else 1
    if rx > 1 or ry > 1:
        tiled = bytearray(nw * rx * nh * ry * 4)
        for y in range(nh * ry):
            for x in range(nw * rx):
                tiled[(y * nw * rx + x) * 4:(y * nw * rx + x) * 4 + 4] = out[((y % nh) * nw + x % nw) * 4:((y % nh) * nw + x % nw) * 4 + 4]
        out, nw, nh = tiled, nw * rx, nh * ry
    require(nw % 64 == 0 and nh % 8 == 0, 'the island texture cannot be fitted to a model texture')
    return nw, nh, bytes(out)


def png(width, height, rgba):
    """An RGBA PNG (stored DEFLATE: the same bytes everywhere)."""
    raw = b''.join(b'\0' + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    z = bytearray(b'\x78\x01')
    for at in range(0, len(raw), 65535):
        chunk = raw[at:at + 65535]
        z += bytes([1 if at + 65535 >= len(raw) else 0]) + struct.pack('<HH', len(chunk), len(chunk) ^ 0xFFFF) + chunk
    z += struct.pack('>I', zlib.adler32(raw))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) + chunk(b'IDAT', bytes(z)) + chunk(b'IEND', b'')


def pick(game, room, points):
    """(texture name, PNG bytes) of the island's ground under the points (world x, z), or None when it can't be read.
    The texture is the colour texture of the BG material most points stand on, cut to the band of it they use."""
    cx, cz = room_centre(room)
    files = gp.room_files(game, 'sea_Room%d.szs' % room)
    bfres = files.get('Room%d.bfres' % room)
    require(bfres is not None, 'the room has no model')
    src = base.Bfres(bfres)
    _, shapes, materials = gp.read_model(src, 'model')
    votes, vs, uvs = {}, {}, {}
    for x, z in points:
        hit = under(shapes, x - cx, z - cz)
        if hit is None:
            continue
        s, weights, tri = hit
        votes[s.material] = votes.get(s.material, 0) + 1
        if id(s) not in uvs:
            uvs[id(s)] = uv_reader(src, s)
        uv = uvs[id(s)]
        if uv:
            vs.setdefault(s.material, []).append(sum(w * uv[i][1] for w, i in zip(weights, tri)))
    material_at = dict(materials)
    textures = dict(src.dict(src.rel(0x24)))
    for name, _ in sorted(votes.items(), key=lambda kv: (-kv[1], kv[0])):
        try:
            texture = colour_texture(src, material_at[name])
            w, h, rgba = decode(src, textures[texture])
            if name in vs and h > 64:
                ordered = sorted(v % 1.0 for v in vs[name])
                w, h, rgba = band(w, h, rgba, ordered[len(ordered) // 2])
            w, h, rgba = scaled(w, h, rgba)
            # opaque (the bank is not a cut-out), and a little darker: the island is drawn with its background light,
            # a model with the brighter light of objects
            rgba = bytes(255 if i % 4 == 3 else v * SHADE // 256 for i, v in enumerate(rgba))
            return texture, png(w, h, rgba)
        except (base.BuildError, KeyError, ValueError, IndexError, struct.error):
            continue
    return None


def island_images(game, doc):
    """{material name: (texture name, PNG)} for the materials of a GLB document that ask for the island's texture."""
    out = {}
    for m in doc.get('materials', []):
        ask = m.get('extras', {}).get('wwhd_island') if isinstance(m, dict) and isinstance(m.get('extras'), dict) else None
        if not isinstance(ask, dict):
            continue
        room, points = ask.get('room'), ask.get('points')
        if type(room) is not int or not isinstance(points, list) or not 1 <= len(points) <= 256:
            continue
        pts = [(float(p[0]), float(p[1])) for p in points if isinstance(p, list) and len(p) == 2 and all(isinstance(v, (int, float)) for v in p)]
        try:
            found = pick(game, room, pts)
        except (base.BuildError, OSError, KeyError, ValueError, IndexError, struct.error):
            found = None
        if found:
            out[m.get('name', '')] = found
    return out
