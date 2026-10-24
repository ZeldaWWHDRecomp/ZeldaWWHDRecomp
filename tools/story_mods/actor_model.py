#!/usr/bin/env python3
"""Convert an original rigid GLB using a material from the player's own game.

Generated archives belong only in the player's package data cache. No template
or shader is included here. Coordinates are metres; conversion preserves origin.
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
import zlib

# Windows' embeddable Python runs isolated: the script's own folder is not on sys.path, so the
# sibling modules are found through it explicitly (issue #280).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notebook_model as base  # noqa: E402

BuildError = base.BuildError
MAX_SOURCE = 8 * 1024 * 1024
MAX_VERTICES = 4096
MAX_TRIANGLES = 8192
VERSION = 2


def require(ok, message):
    if not ok:
        raise BuildError(message)


def integer(value, minimum=0):
    require(type(value) is int and value >= minimum, 'invalid nonnegative integer')
    return value


def png(data):
    require(data[:8] == b'\x89PNG\r\n\x1a\n', 'expected PNG texture')
    at, compressed, header, ended = 8, bytearray(), None, False
    while at < len(data):
        require(at + 12 <= len(data), 'truncated PNG chunk')
        count, kind = struct.unpack_from('>I4s', data, at)
        require(count <= len(data) - at - 12, 'PNG chunk outside input')
        body = data[at + 8:at + 8 + count]
        require(zlib.crc32(kind + body) & 0xffffffff == struct.unpack_from('>I', data, at + 8 + count)[0], 'PNG CRC mismatch')
        if header is None:
            require(kind == b'IHDR' and count == 13, 'PNG IHDR must be first')
            w, h, depth, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', body)
            require(0 < w <= 512 and 0 < h <= 512, 'texture dimensions exceed 512')
            require(w % 64 == 0 and h % 8 == 0, 'texture dimensions must be multiples of 64 by 8')
            require(depth == 8 and color in (2, 6) and compression == filtering == interlace == 0, 'expected non-interlaced RGB/RGBA8 PNG')
            header = (w, h, color)
        elif kind == b'IHDR':
            raise BuildError('duplicate PNG IHDR')
        elif kind == b'IDAT':
            compressed.extend(body)
        elif kind == b'IEND':
            require(count == 0 and at + 12 == len(data), 'invalid PNG IEND')
            ended = True
            break
        else:
            require(kind[0] & 32, 'unsupported critical PNG chunk')
        at += 12 + count
    require(header is not None and ended, 'incomplete PNG')
    w, h, color = header
    size = h * (w * (4 if color == 6 else 3) + 1)
    decoder = zlib.decompressobj()
    try:
        raw = decoder.decompress(compressed, size + 1)
    except zlib.error as exc:
        raise BuildError('invalid PNG deflate stream') from exc
    require(len(raw) == size and decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail, 'PNG inflated size mismatch')
    stride = size // h
    require(all(raw[y * stride] <= 4 for y in range(h)), 'invalid PNG filter')
    return base.decode_png(data)


def matrix_mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def node_matrix(node):
    identity = [[float(r == c) for c in range(4)] for r in range(4)]
    if 'matrix' in node:
        require(not any(k in node for k in ('translation', 'rotation', 'scale')), 'matrix and TRS are mutually exclusive')
        values = node['matrix']
        require(len(values) == 16, 'matrix must have 16 entries')
        result = [[values[c * 4 + r] for c in range(4)] for r in range(4)]
    else:
        t, s, q = node.get('translation', [0, 0, 0]), node.get('scale', [1, 1, 1]), node.get('rotation', [0, 0, 0, 1])
        require(len(t) == len(s) == 3 and len(q) == 4, 'invalid TRS length')
        require(abs(sum(v * v for v in q) - 1) < 1e-4, 'rotation quaternion must be unit length')
        x, y, z, w = q
        result = identity
        rotation = [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)], [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
        for r in range(3):
            for c in range(3): result[r][c] = rotation[r][c] * s[c]
            result[r][3] = t[r]
    require(all(type(v) in (int, float) and math.isfinite(v) for row in result for v in row), 'non-finite transform')
    require(result[3] == [0, 0, 0, 1], 'transform must be affine')
    return result


def read_glb(path):
    require(os.path.getsize(path) <= MAX_SOURCE, 'GLB exceeds 8 MiB')
    with open(path, 'rb') as source: data = source.read(MAX_SOURCE + 1)
    require(len(data) >= 28 and data[:4] == b'glTF', 'expected GLB')
    version, length = struct.unpack_from('<II', data, 4)
    require(version == 2 and length == len(data), 'invalid GLB header')
    chunks, at = [], 12
    while at < len(data):
        require(at + 8 <= len(data), 'truncated GLB chunk')
        count, kind = struct.unpack_from('<I4s', data, at)
        require(count % 4 == 0 and count <= len(data) - at - 8, 'invalid GLB chunk size')
        chunks.append((kind, data[at + 8:at + 8 + count])); at += 8 + count
    require(len(chunks) == 2 and chunks[0][0] == b'JSON' and chunks[1][0] == b'BIN\0', 'expected JSON and BIN chunks')
    try: doc = json.loads(chunks[0][1])
    except (ValueError, UnicodeError) as exc: raise BuildError('invalid GLB JSON') from exc
    require(doc.get('asset', {}).get('version') == '2.0', 'expected glTF 2.0 asset')
    require(not any(doc.get(k) for k in ('skins', 'animations', 'extensionsUsed', 'extensionsRequired')), 'static GLB cannot use skinning, animation or extensions')
    def no_extensions(value):
        if isinstance(value, dict):
            require(not value.get('extensions'), 'GLB extensions are unsupported')
            for item in value.values(): no_extensions(item)
        elif isinstance(value, list):
            for item in value: no_extensions(item)
    no_extensions(doc)
    binary = chunks[1][1]
    buffers = doc.get('buffers', [])
    require(len(buffers) == 1 and 'uri' not in buffers[0], 'one embedded buffer required')
    declared = integer(buffers[0]['byteLength'])
    require(declared <= len(binary) <= declared + 3, 'buffer size mismatch')
    def view(index):
        v = doc['bufferViews'][integer(index)]
        require(v.get('buffer', 0) == 0, 'invalid buffer index')
        start, count = integer(v.get('byteOffset', 0)), integer(v['byteLength'])
        require(start <= declared and count <= declared - start, 'buffer view outside buffer')
        return v, binary[start:start + count]
    def accessor(index, shape, components):
        a = doc['accessors'][integer(index)]
        require(a.get('type') == shape and a.get('componentType') in components and not a.get('normalized') and 'sparse' not in a, 'unsupported accessor')
        count = integer(a['count'], 1)
        require(count <= (MAX_TRIANGLES * 3 if shape == 'SCALAR' else MAX_VERTICES), 'accessor count exceeds limit')
        v, body = view(a['bufferView']); offset = integer(a.get('byteOffset', 0))
        code, size = {5126: ('f',4),5123:('H',2),5125:('I',4),5121:('B',1)}[a['componentType']]
        width = {'VEC3':3,'VEC2':2,'SCALAR':1}[shape]; packed = width * size
        stride = integer(v.get('byteStride', packed), packed)
        require(stride <= 252 and stride % size == 0 and offset % size == 0 and offset + (count-1)*stride + packed <= len(body), 'invalid accessor span/stride')
        rows = [struct.unpack_from('<' + str(width) + code, body, offset+i*stride) for i in range(count)]
        require(all(math.isfinite(x) for row in rows for x in row), 'non-finite vertex attribute')
        return rows if width > 1 else [row[0] for row in rows]
    meshes = doc.get('meshes', [])
    require(len(meshes) == 1 and len(meshes[0].get('primitives', [])) == 1 and not meshes[0].get('weights'), 'one rigid mesh primitive required')
    prim = meshes[0]['primitives'][0]
    require(prim.get('mode',4) == 4 and not prim.get('targets') and not prim.get('extensions'), 'indexed triangle primitive required')
    att = prim['attributes']
    require(set(att) == {'POSITION','NORMAL','TEXCOORD_0'}, 'POSITION, NORMAL, TEXCOORD_0 required')
    pos = accessor(att['POSITION'],'VEC3',(5126,)); normals = accessor(att['NORMAL'],'VEC3',(5126,)); uv = accessor(att['TEXCOORD_0'],'VEC2',(5126,)); indices = accessor(prim['indices'],'SCALAR',(5121,5123,5125))
    require(len(pos) == len(normals) == len(uv), 'attribute counts differ')
    require(len(indices) % 3 == 0 and max(indices) < len(pos), 'invalid triangle indices')
    require(len(doc.get('materials', [])) == 1 and prim.get('material') == 0, 'one material required')
    material = doc['materials'][0]
    require(material.get('alphaMode','OPAQUE') == 'OPAQUE' and not material.get('extensions'), 'only opaque template material supported')
    pbr = material.get('pbrMetallicRoughness', {})
    require(pbr.get('baseColorTexture', {}).get('index') == 0 and pbr.get('baseColorTexture',{}).get('texCoord',0) == 0, 'base colour texture required')
    require(not pbr.get('metallicRoughnessTexture') and not material.get('doubleSided', False), 'unsupported material inputs')
    require(pbr.get('baseColorFactor',[1,1,1,1]) == [1,1,1,1] and not any(k in material for k in ('normalTexture','occlusionTexture','emissiveTexture')), 'unsupported material inputs')
    require(len(doc.get('textures',[])) == len(doc.get('images',[])) == 1 and doc['textures'][0].get('source') == 0, 'one embedded texture required')
    image = doc['images'][0]; require(image.get('mimeType') == 'image/png' and 'uri' not in image, 'embedded PNG required')
    _, image_data = view(image['bufferView']); w,h,_ = png(image_data)
    nodes, scenes = doc.get('nodes',[]), doc.get('scenes',[])
    require(len(scenes) == 1 and doc.get('scene',0) == 0, 'one active scene required')
    found, visited = [], set()
    def visit(index, parent):
        index = integer(index); require(index < len(nodes) and index not in visited, 'invalid/cyclic/shared node')
        visited.add(index); node = nodes[index]
        require(not any(k in node for k in ('skin','weights','extensions')), 'unsupported node')
        transform = matrix_mul(parent,node_matrix(node))
        if 'mesh' in node:
            require(node['mesh'] == 0, 'invalid mesh index'); found.append(transform)
        for child in node.get('children',[]): visit(child,transform)
    for root in scenes[0].get('nodes',[]): visit(root,node_matrix({}))
    require(len(found) == 1, 'scene must instantiate mesh exactly once')
    transform = found[0]; a,b,c = [row[:3] for row in transform[:3]]
    cross = lambda u,v: [u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
    cof = [cross(b,c),cross(c,a),cross(a,b)]; det = sum(a[k]*cof[0][k] for k in range(3))
    require(abs(det) > 1e-12 and math.isfinite(det), 'singular transform')
    placed = [tuple(100 * (sum(transform[r][k]*p[k] for k in range(3))+transform[r][3]) for r in range(3)) for p in pos]
    transformed = []
    for n in normals:
        out = [sum(cof[r][k]*n[k] for k in range(3))/det for r in range(3)]; length = math.sqrt(sum(v*v for v in out))
        require(length > 1e-12 and math.isfinite(length), 'invalid normal'); transformed.append(tuple(v/length for v in out))
    require(all(math.isfinite(v) and abs(v) <= 1000000 for p in placed for v in p), 'model exceeds coordinate range')
    if det < 0:
        indices = [value for i in range(0,len(indices),3) for value in (indices[i],indices[i+2],indices[i+1])]
    return (placed,transformed,uv,indices,image_data), {'vertices':len(pos),'triangles':len(indices)//3,'texture_width':w,'texture_height':h}


MAX_MATERIALS = 16
MAX_PRIMITIVES = 32
MAX_TEXTURE_BYTES = 1024 * 1024  # all colour textures of one model, decoded RGBA8 (one 512 x 512 texture)
KINDS = ('opaque', 'mask')


class Part:
    """One visible primitive: authored-frame positions (game units), normals, UVs, triangle indices and
    the index of its material in Parts.materials."""
    def __init__(self, pos, nrm, uv, indices, material):
        self.pos, self.nrm, self.uv, self.indices, self.material = pos, nrm, uv, indices, material


class Material:
    def __init__(self, name, image, kind, cutoff, double_sided):
        self.name, self.image, self.kind, self.cutoff, self.double_sided = name, image, kind, cutoff, double_sided


class Parts:
    """A model with one or more materials (one BFRES shape per primitive, one material per glTF material,
    one colour texture per image)."""
    def __init__(self, parts, materials, images):
        self.parts, self.materials, self.images = parts, materials, images


def plain_material(material):
    """True for the material every single-material model has used so far: opaque and single-sided."""
    return isinstance(material, dict) and material.get('alphaMode', 'OPAQUE') == 'OPAQUE' and material.get('doubleSided', False) is False


def read_parts(path):
    """A rigid model with 1 to 16 materials. Each material has one embedded PNG base colour texture; each
    triangle primitive uses one material (several primitives may share one). Material kinds: opaque
    (alphaMode OPAQUE) and cut-out (alphaMode MASK, alphaCutoff); a doubleSided material's faces are drawn
    from both sides (the writer adds a mirrored back copy, lit from its own side)."""
    require(os.path.getsize(path) <= MAX_SOURCE, 'GLB exceeds 8 MiB')
    with open(path, 'rb') as source: data = source.read(MAX_SOURCE + 1)
    require(len(data) >= 28 and data[:4] == b'glTF', 'expected GLB')
    version, length = struct.unpack_from('<II', data, 4)
    require(version == 2 and length == len(data), 'invalid GLB header')
    chunks, at = [], 12
    while at < len(data):
        require(at + 8 <= len(data), 'truncated GLB chunk')
        count, kind = struct.unpack_from('<I4s', data, at)
        require(count % 4 == 0 and count <= len(data) - at - 8, 'invalid GLB chunk size')
        chunks.append((kind, data[at + 8:at + 8 + count])); at += 8 + count
    require(len(chunks) == 2 and chunks[0][0] == b'JSON' and chunks[1][0] == b'BIN\0', 'expected JSON and BIN chunks')
    try: doc = json.loads(chunks[0][1])
    except (ValueError, UnicodeError) as exc: raise BuildError('invalid GLB JSON') from exc
    require(isinstance(doc, dict) and doc.get('asset', {}).get('version') == '2.0', 'expected glTF 2.0 asset')
    require(not any(doc.get(k) for k in ('skins', 'animations', 'extensionsUsed', 'extensionsRequired')), 'static GLB cannot use skinning, animation or extensions')
    def no_extensions(value):
        if isinstance(value, dict):
            require(not value.get('extensions'), 'GLB extensions are unsupported')
            for item in value.values(): no_extensions(item)
        elif isinstance(value, list):
            for item in value: no_extensions(item)
    no_extensions(doc)
    binary = chunks[1][1]
    buffers = doc.get('buffers', [])
    require(len(buffers) == 1 and 'uri' not in buffers[0], 'one embedded buffer required')
    declared = integer(buffers[0]['byteLength'])
    require(declared <= len(binary) <= declared + 3, 'buffer size mismatch')
    def view(index):
        v = doc['bufferViews'][integer(index)]
        require(v.get('buffer', 0) == 0, 'invalid buffer index')
        start, count = integer(v.get('byteOffset', 0)), integer(v['byteLength'])
        require(start <= declared and count <= declared - start, 'buffer view outside buffer')
        return v, binary[start:start + count]
    def accessor(index, shape, components):
        a = doc['accessors'][integer(index)]
        require(a.get('type') == shape and a.get('componentType') in components and not a.get('normalized') and 'sparse' not in a, 'unsupported accessor')
        count = integer(a['count'], 1)
        require(count <= (MAX_TRIANGLES * 3 if shape == 'SCALAR' else MAX_VERTICES), 'accessor count exceeds limit')
        v, body = view(a['bufferView']); offset = integer(a.get('byteOffset', 0))
        code, size = {5126: ('f',4),5123:('H',2),5125:('I',4),5121:('B',1)}[a['componentType']]
        width = {'VEC3':3,'VEC2':2,'SCALAR':1}[shape]; packed = width * size
        stride = integer(v.get('byteStride', packed), packed)
        require(stride <= 252 and stride % size == 0 and offset % size == 0 and offset + (count-1)*stride + packed <= len(body), 'invalid accessor span/stride')
        rows = [struct.unpack_from('<' + str(width) + code, body, offset+i*stride) for i in range(count)]
        require(all(math.isfinite(x) for row in rows for x in row), 'non-finite vertex attribute')
        return rows if width > 1 else [row[0] for row in rows]
    meshes = doc.get('meshes', [])
    require(len(meshes) == 1 and not meshes[0].get('weights'), 'one rigid visible mesh required')
    prims = meshes[0].get('primitives', [])
    require(isinstance(prims, list) and 1 <= len(prims) <= MAX_PRIMITIVES, 'the visible mesh needs 1 to %d triangle primitives' % MAX_PRIMITIVES)
    materials, textures, images = doc.get('materials', []), doc.get('textures', []), doc.get('images', [])
    require(isinstance(materials, list) and 1 <= len(materials), 'the model needs a material')
    require(len(materials) <= MAX_MATERIALS, 'the model has %d materials; at most %d are supported' % (len(materials), MAX_MATERIALS))
    # materials: each with one embedded PNG base colour texture
    out_materials, used_images, used_textures = [], [], set()
    for mi, material in enumerate(materials):
        label = 'material %d (%s)' % (mi, material.get('name', 'unnamed')) if isinstance(material, dict) else 'material %d' % mi
        require(isinstance(material, dict), label + ': invalid material')
        mode = material.get('alphaMode', 'OPAQUE')
        require(mode != 'BLEND', label + ': blended transparency (alphaMode BLEND) is not supported; use MASK for cut-out leaves, fences and foliage cards, or OPAQUE')
        require(mode in ('OPAQUE', 'MASK'), label + ': unknown alphaMode')
        cutoff = material.get('alphaCutoff', 0.5)
        require(mode == 'MASK' or 'alphaCutoff' not in material, label + ': alphaCutoff needs alphaMode MASK')
        require(type(cutoff) in (int, float) and 0 < cutoff < 1, label + ': alphaCutoff must lie between 0 and 1')
        double = material.get('doubleSided', False)
        require(type(double) is bool, label + ': invalid doubleSided')
        pbr = material.get('pbrMetallicRoughness', {})
        colour = pbr.get('baseColorTexture', {})
        require(type(colour.get('index')) is int and 0 <= colour['index'] < len(textures) and colour.get('texCoord', 0) == 0, label + ': needs a base colour texture on UV0')
        require(not pbr.get('metallicRoughnessTexture') and not any(k in material for k in ('normalTexture','occlusionTexture','emissiveTexture')), label + ': only a base colour texture is supported')
        require(pbr.get('baseColorFactor',[1,1,1,1]) == [1,1,1,1], label + ': base colour factor must be white (paint the colour into the texture)')
        texture = textures[colour['index']]; used_textures.add(colour['index'])
        require(isinstance(texture, dict) and type(texture.get('source')) is int and 0 <= texture['source'] < len(images), label + ': invalid texture')
        image = texture['source']
        if image not in used_images: used_images.append(image)
        out_materials.append(Material(material.get('name', ''), used_images.index(image), 'mask' if mode == 'MASK' else 'opaque', float(cutoff), double))
    require(all(i in used_images for i in range(len(images))), 'every embedded image must be used by a material')
    require(used_textures == set(range(len(textures))), 'every texture must be used by a material')
    image_data, texture_bytes = [], 0
    for index in used_images:
        image = images[index]
        require(isinstance(image, dict) and image.get('mimeType') == 'image/png' and 'uri' not in image, 'image %d: embedded PNG required' % index)
        _, body = view(image['bufferView'])
        w, h, _ = png(body)
        texture_bytes += w * h * 4
        require(texture_bytes <= MAX_TEXTURE_BYTES, 'the textures of one model may take at most 1 MiB (for example one 512 x 512, four 256 x 256 or sixteen 128 x 128 textures)')
        image_data.append(body)
    nodes, scenes = doc.get('nodes',[]), doc.get('scenes',[])
    require(len(scenes) == 1 and doc.get('scene',0) == 0, 'one active scene required')
    found, visited = [], set()
    def visit(index, parent):
        index = integer(index); require(index < len(nodes) and index not in visited, 'invalid/cyclic/shared node')
        visited.add(index); node = nodes[index]
        require(not any(k in node for k in ('skin','weights','extensions')), 'unsupported node')
        transform = matrix_mul(parent,node_matrix(node))
        if 'mesh' in node:
            require(node['mesh'] == 0, 'invalid mesh index'); found.append(transform)
        for child in node.get('children',[]): visit(child,transform)
    for root in scenes[0].get('nodes',[]): visit(root,node_matrix({}))
    require(len(found) == 1, 'scene must instantiate mesh exactly once')
    transform = found[0]; a,b,c = [row[:3] for row in transform[:3]]
    cross = lambda u,v: [u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
    cof = [cross(b,c),cross(c,a),cross(a,b)]; det = sum(a[k]*cof[0][k] for k in range(3))
    require(abs(det) > 1e-12 and math.isfinite(det), 'singular transform')
    parts, vertices, triangles, authored, used_materials = [], 0, 0, 0, set()
    for pi, prim in enumerate(prims):
        require(isinstance(prim, dict) and prim.get('mode',4) == 4 and not prim.get('targets') and not prim.get('extensions'), 'primitive %d: indexed triangles required' % pi)
        att = prim.get('attributes', {})
        require(set(att) == {'POSITION','NORMAL','TEXCOORD_0'}, 'primitive %d: POSITION, NORMAL, TEXCOORD_0 required' % pi)
        require(type(prim.get('material')) is int and 0 <= prim['material'] < len(materials), 'primitive %d: needs one of the model\'s materials' % pi)
        pos = accessor(att['POSITION'],'VEC3',(5126,)); normals = accessor(att['NORMAL'],'VEC3',(5126,)); uv = accessor(att['TEXCOORD_0'],'VEC2',(5126,)); indices = accessor(prim['indices'],'SCALAR',(5121,5123,5125))
        require(len(pos) == len(normals) == len(uv), 'primitive %d: attribute counts differ' % pi)
        require(len(indices) % 3 == 0 and max(indices) < len(pos), 'primitive %d: invalid triangle indices' % pi)
        # a double-sided material draws a mirrored back copy of its faces: they count twice toward the limits
        sides = 2 if out_materials[prim['material']].double_sided else 1
        vertices += sides * len(pos); triangles += sides * (len(indices) // 3); authored += len(indices) // 3
        require(vertices <= MAX_VERTICES, 'the visible mesh has more than %d vertices (all primitives together; double-sided faces count twice)' % MAX_VERTICES)
        require(triangles <= MAX_TRIANGLES, 'the visible mesh has more than %d triangles (all primitives together; double-sided faces count twice)' % MAX_TRIANGLES)
        placed = [tuple(100 * (sum(transform[r][k]*p[k] for k in range(3))+transform[r][3]) for r in range(3)) for p in pos]
        transformed = []
        for n in normals:
            out = [sum(cof[r][k]*n[k] for k in range(3))/det for r in range(3)]; length = math.sqrt(sum(v*v for v in out))
            require(length > 1e-12 and math.isfinite(length), 'invalid normal'); transformed.append(tuple(v/length for v in out))
        require(all(math.isfinite(v) and abs(v) <= 1000000 for p in placed for v in p), 'model exceeds coordinate range')
        if det < 0:
            indices = [value for i in range(0,len(indices),3) for value in (indices[i],indices[i+2],indices[i+1])]
        used_materials.add(prim['material'])
        parts.append(Part(placed, transformed, uv, indices, prim['material']))
    require(used_materials == set(range(len(materials))), 'every material must be used by a visible primitive')
    w, h, _ = base.decode_png(image_data[0])
    info = {'vertices': sum(len(p.pos) for p in parts), 'triangles': authored, 'drawn_triangles': triangles, 'texture_width': w, 'texture_height': h,
            'materials': len(out_materials), 'textures': len(image_data), 'texture_bytes': texture_bytes,
            'cutout_materials': sum(m.kind == 'mask' for m in out_materials)}
    return Parts(parts, out_materials, image_data), info


def inspect(path):
    try:
        import collision_mesh
        return collision_mesh.read_static(path)[1]
    except (KeyError, IndexError, TypeError, ValueError, OverflowError, struct.error, RecursionError) as exc: raise BuildError('malformed GLB: ' + str(exc)) from exc


def build_bfres(template, glb, identity):
    require(re.fullmatch(r'[a-z][a-z0-9_]{0,30}', identity) is not None, 'identity must be 1–31 lowercase ASCII identifier characters')
    # Template patcher centres its input; compensate by restoring authored origin.
    pos,nrm,uv,indices,image = glb
    center = [(min(p[k] for p in pos)+max(p[k] for p in pos))/2 for k in range(3)]
    patched,_ = base.build_bfres(template,glb,1.0)
    f = base.Bfres(patched); dictionary=f.rel(0x20); fm=f.dict(dictionary)[0][1]
    # The player's deed root is deliberately tilted for its get-item pose.
    # A room actor must use the authored GLB frame instead. This template is
    # rigid with a single root and no matrix palette/inverse-bind matrices.
    skeleton = f.rel(fm + 0x0C)
    require(skeleton > 0 and f.b[skeleton:skeleton + 4] == b'FSKL', 'expected rigid template skeleton')
    require((f.u16(skeleton + 8), f.u16(skeleton + 10), f.u16(skeleton + 12)) == (1, 0, 0), 'template must have one root and no skin matrix palette')
    bone = f.rel(skeleton + 0x14)
    require(bone > 0 and bone + 0x40 <= len(f.b) and f.u16(bone + 6) == 0xFFFF, 'expected root bone')
    f.put('3f', bone + 0x14, 1.0, 1.0, 1.0)
    # Identity works for either the template's Euler or quaternion mode.
    f.put('4f', bone + 0x20, 0.0, 0.0, 0.0, 1.0)
    f.put('3f', bone + 0x30, 0.0, 0.0, 0.0)
    fvtx=f.rel(fm+0x10); vbuf=f.rel(fvtx+0x18); vertices=f.rel(vbuf+0x14)
    for i,p in enumerate(pos): f.put('3f',vertices+48*i,*p)
    shape=f.dict(f.rel(fm+0x14))[0][1]; box=f.rel(shape+0x34); f.put('3f',box,*center)
    # BFRES strings carry a preceding big-endian length, then a NUL terminator.
    at=f.append(struct.pack('>I',len(identity))+identity.encode()+b'\0',4)+4
    f.set_rel(fm+4,at); f.set_rel(dictionary+8+16+8,at)
    # One-entry Patricia tree: sentinel branches to entry; key's highest bit
    # differs from empty sentinel at its final nonzero byte.
    bit=(len(identity)-1)*8+(ord(identity[-1]).bit_length()-1)
    f.put('IHH',dictionary+8,0xffffffff,1,0)
    f.put('IHH',dictionary+24,bit,0,1)
    f.append(b'', 0x1000)
    f.put('I',0xC,len(f.b))
    result = bytes(f.b)
    validate_bfres(result, identity, len(pos), len(indices))
    return result


def validate_bfres(data, identity, vertex_count, index_count):
    f = base.Bfres(data)
    require(f.u32(0xC) == len(data) and len(data) % 0x1000 == 0, 'invalid output BFRES size')
    models = f.dict(f.rel(0x20))
    require(len(models) == 1 and models[0][0] == identity, 'output dictionary identity mismatch')
    fm = models[0][1]
    require(f.cstr(f.rel(fm + 4)) == identity, 'output FMDL identity mismatch')
    fvtx = f.rel(fm + 0x10); vbuf = f.rel(fvtx + 0x18)
    require(f.u32(fvtx + 8) == vertex_count and f.u32(vbuf + 4) == vertex_count * 48, 'output vertex count mismatch')
    require(0 <= f.rel(vbuf + 0x14) <= len(data) - vertex_count * 48, 'output vertex span invalid')
    shape = f.dict(f.rel(fm + 0x14))[0][1]; mesh = f.rel(shape + 0x24); ibuf = f.rel(mesh + 0x14)
    require(f.u32(mesh + 8) == index_count and f.u32(ibuf + 4) == index_count * 4, 'output index count mismatch')
    require(0 <= f.rel(ibuf + 0x14) <= len(data) - index_count * 4, 'output index span invalid')
    texture = dict(f.dict(f.rel(0x24)))[base.TEXTURE]; surface = texture + 4
    width,height = f.u32(surface + 4), f.u32(surface + 8)
    require(f.u32(surface + 0x14) == base.FMT_RGBA8_SRGB and f.u32(surface + 0x30) == base.TILE_LINEAR_ALIGNED, 'output texture format mismatch')
    require(f.u32(surface + 0x20) == width * height * 4 and 0 <= f.rel(texture + 0xB0) <= len(data) - width * height * 4, 'output texture span invalid')


def build(game, glb_path, identity, out, collision_out=None):
    require(re.fullmatch(r'[a-z][a-z0-9_]{0,30}', identity) is not None, 'invalid archive identity')
    try:
        import collision_mesh
        with open(glb_path, 'rb') as source: source_bytes = source.read(MAX_SOURCE + 1)
        require(len(source_bytes) <= MAX_SOURCE, 'GLB exceeds 8 MiB')
        # Parse exactly the bytes hashed below, even if the author saves the
        # source again while the converter is running.
        with tempfile.TemporaryDirectory() as directory:
            snapshot = os.path.join(directory, 'source.glb')
            with open(snapshot, 'wb') as target: target.write(source_bytes)
            glb,info,collision=collision_mesh.read_static(snapshot, collision=collision_out is not None)
    except (KeyError, IndexError, TypeError, ValueError, OverflowError, struct.error, RecursionError) as exc: raise BuildError('malformed GLB: '+str(exc)) from exc
    multi = not isinstance(glb, tuple)  # Parts (the module may run as __main__: no isinstance on its classes)
    # materials that ask for the island's own ground texture (island_texture.py): read from the player's room archive
    island = {}
    try:
        jl = struct.unpack_from('<I', source_bytes, 12)[0]
        doc = json.loads(source_bytes[20:20 + jl])
        if any(isinstance(m, dict) and isinstance(m.get('extras'), dict) and 'wwhd_island' in m['extras'] for m in doc.get('materials', [])):
            import island_texture
            island = island_texture.island_images(game, doc)
    except (ValueError, UnicodeError, struct.error):
        island = {}
    if island:
        if multi:
            for m in glb.materials:
                if m.name in island:
                    glb.images[m.image] = island[m.name][1]
        else:
            name = next((m.get('name', '') for m in doc.get('materials', []) if isinstance(m, dict)), '')
            if name in island:
                glb = glb[:4] + (island[name][1],)
    digest=hashlib.sha256()
    with open(__file__, 'rb') as source: converter_source = source.read()
    with open(collision_mesh.__file__, 'rb') as source: collision_source = source.read()
    sources = [str(VERSION).encode(),converter_source,collision_source]
    if multi:
        # Several materials: the opaque + cut-out template, the writer and the material copier it shares
        # with the building converter.
        import multi_material, game_piece_model
        for module in (multi_material, game_piece_model):
            with open(module.__file__, 'rb') as source: sources.append(source.read())
        names = (multi_material.BFRES, multi_material.SHADERS)
        archive = multi_material.ARCHIVE
    else:
        names = ('Vbeso.bfres', 'vbeso.sharcfb')
        archive = base.ARCHIVE
    with open(os.path.join(game,archive),'rb') as source: files=dict(base.shaderprep.sarc(base.shaderprep.yaz0(source.read())))
    require(set(files)==set(names),'unexpected player template archive')
    template, shaders = files[names[0]], files[names[1]]
    if island:
        import island_texture
        with open(island_texture.__file__, 'rb') as source: sources.append(source.read())
        for name in sorted(island): sources.append(island[name][1])
    for body in sources+[str(bool(collision_out)).encode(),identity.encode(),source_bytes,template,shaders]:
        digest.update(struct.pack('>I',len(body))); digest.update(body)
    info.update(identity=identity,cache_key=digest.hexdigest(),converter_version=VERSION)
    if collision_out:
        require(os.path.abspath(collision_out) not in (os.path.abspath(out), os.path.abspath(out+'.json'), os.path.abspath(glb_path)), 'collision output must have a separate cache path')
        info['collision_path'] = os.path.abspath(collision_out)
    # Reuse only a complete, byte-verified cache for these exact inputs. Never
    # trust a metadata key alone: interrupted writes and damaged archives rebuild.
    try:
        require(os.path.getsize(out) <= 16 * 1024 * 1024, 'cached archive oversized')
        require(os.path.getsize(out+'.json') <= 4096, 'cached metadata oversized')
        with open(out+'.json') as source: cached = json.load(source)
        with open(out,'rb') as source: packed = source.read(16 * 1024 * 1024 + 1)
        expected = all(cached.get(key) == value for key,value in info.items())
        if collision_out:
            require(os.path.getsize(collision_out) == len(collision), 'cached collision size mismatch')
            with open(collision_out, 'rb') as source: cached_collision = source.read(len(collision)+1)
            expected = expected and cached_collision == collision
        if (expected and cached.get('archive_bytes') == len(packed) and
                cached.get('archive_sha256') == hashlib.sha256(packed).hexdigest()):
            return cached
    except (OSError, ValueError, TypeError, AttributeError, BuildError):
        pass
    if multi:
        import multi_material
        bfres=multi_material.build_bfres(template,glb,identity)
    else:
        bfres=build_bfres(template,glb,identity)
    packed=base.yaz0_stored(base.sarc({identity+'.bfres':bfres,identity+'.sharcfb':shaders}))
    info.update(archive_bytes=len(packed),archive_sha256=hashlib.sha256(packed).hexdigest())
    def publish(path, data):
        directory = os.path.dirname(os.path.abspath(path))
        os.makedirs(directory, exist_ok=True)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=directory, prefix='.actor-model-', delete=False) as target:
                temporary = target.name
                target.write(data)
            os.replace(temporary, path)
            temporary = None
        finally:
            if temporary is not None:
                os.unlink(temporary)
    publish(out, packed)
    if collision_out:
        publish(collision_out, collision)
    # Metadata commits the generation last. A crash or competing conversion
    # leaves either a verified generation or a cache miss on the next request.
    publish(out+'.json', (json.dumps(info,sort_keys=True)+'\n').encode())
    return info


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--glb',required=True); parser.add_argument('--inspect',action='store_true'); parser.add_argument('--game'); parser.add_argument('--identity'); parser.add_argument('--out'); parser.add_argument('--collision-out'); parser.add_argument('--shoreline',action='store_true'); parser.add_argument('--shoreline-material',choices=('foam','water','pond','river','waterfall','lava'),default='foam')
    args=parser.parse_args()
    try:
        if args.inspect: result=inspect(args.glb)
        else:
            require(all((args.game,args.identity,args.out)),'--game, --identity and --out required')
            if args.shoreline:
                require(not args.collision_out,'shoreline is collision-free')
                import shoreline_model
                result=shoreline_model.build(args.game,args.glb,args.identity,args.out,args.shoreline_material)
            else: result=build(args.game,args.glb,args.identity,args.out,args.collision_out)
    except (BuildError,OSError) as exc: parser.exit(1,'actor_model: '+str(exc)+'\n')
    print(json.dumps(result,sort_keys=True))


if __name__ == '__main__': main()
