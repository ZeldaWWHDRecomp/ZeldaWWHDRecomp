# SPDX-License-Identifier: MPL-2.0
"""Original static GLB collision extraction and portable WCOL v1 encoding.

WCOL uses big-endian integers/floats: magic/version/V/T, min XYZ/max XYZ,
V float XYZ records, then T uint16 (a,b,c,semantic) records. No guest pointers.
"""
import copy
import hashlib
import json
import math
import os
import struct
import tempfile

MAX_VERTICES = MAX_TRIANGLES = 8192  # story_world_collision_mesh.h; the 512 KiB body budget is checked at placement
MAX_EXTENT = 100000  # story_world_collision_mesh.h MaxExtent: one sea square, per axis and per coordinate
SEMANTICS = {'col_stone': 0, 'col_grass': 1, 'col_wood': 2,
             'col_sand': 3, 'col_metal': 4, 'col_wall': 5, 'col_objects': 6,
             'col_climb': 7, 'col_hookshot': 8, 'col_water': 9, 'col_shallow': 10,
             'col_waterfall': 11, 'col_ice': 12, 'col_void': 13, 'col_lava': 14}
LAVA = SEMANTICS['col_lava']


def native_size(vertices, triangles):
    """(vertices, triangles) of the game's collision: lava counts twice, its surface and its bottom (a copy of each lava
    triangle and its points, below it: story_world_collision_mesh.h LavaDepth)"""
    lava = [t for t in triangles if t[3] == LAVA]
    return len(vertices) + len({v for t in lava for v in t[:3]}), len(triangles) + len(lava)


def _document(path, api):
    api.require(os.path.getsize(path) <= api.MAX_SOURCE, 'GLB exceeds 8 MiB')
    with open(path, 'rb') as source:
        data = source.read(api.MAX_SOURCE + 1)
    api.require(len(data) >= 28 and data[:4] == b'glTF' and
                struct.unpack_from('<II', data, 4) == (2, len(data)), 'invalid GLB header')
    size, kind = struct.unpack_from('<I4s', data, 12)
    api.require(kind == b'JSON' and size % 4 == 0 and size <= len(data)-28, 'invalid GLB JSON chunk')
    count, kind = struct.unpack_from('<I4s', data, 20+size)
    api.require(kind == b'BIN\0' and count % 4 == 0 and count == len(data)-28-size, 'invalid GLB BIN chunk')
    return json.loads(data[20:20+size]), data[28+size:]


def _pack(doc, binary):
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    binary += b'\0' * (-len(binary) % 4)
    body = struct.pack('<I4s', len(text), b'JSON')+text+struct.pack('<I4s', len(binary), b'BIN\0')+binary
    return b'glTF'+struct.pack('<II', 2, 12+len(body))+body


def _accessor(doc, binary, index, position, api):
    require = api.require
    index = api.integer(index)
    require(index < len(doc['accessors']), 'collision accessor index outside input')
    a = doc['accessors'][index]
    component = a.get('componentType')
    floating = position is not False
    shape = 'VEC2' if position == 'TEXCOORD_0' else 'VEC3' if floating else 'SCALAR'
    require(a.get('type') == shape and
            component in ((5126,) if floating else (5121, 5123, 5125)) and
            (a.get('normalized') is None or a.get('normalized') is False) and 'sparse' not in a, 'unsupported collision accessor')
    count = api.integer(a['count'], 1)
    require(count <= (MAX_VERTICES if floating else MAX_TRIANGLES*3), 'collision accessor exceeds limit')
    vi = api.integer(a['bufferView'])
    require(vi < len(doc['bufferViews']), 'collision buffer view outside input')
    view = doc['bufferViews'][vi]
    require(type(view.get('buffer')) is int and view['buffer'] == 0, 'invalid collision buffer')
    start, length = api.integer(view.get('byteOffset', 0)), api.integer(view['byteLength'], 1)
    used = api.integer(doc['buffers'][0]['byteLength'])
    require(start <= used and length <= used-start and used <= len(binary), 'collision view outside buffer')
    code, width = {5126: ('f', 4), 5121: ('B', 1), 5123: ('H', 2), 5125: ('I', 4)}[component]
    components = 2 if shape == 'VEC2' else 3 if floating else 1
    packed = width*components
    offset, stride = api.integer(a.get('byteOffset', 0)), api.integer(view.get('byteStride', packed), packed)
    require(('byteStride' not in view or (4 <= stride <= 252 and stride % 4 == 0)) and stride <= 252 and stride % width == 0 and offset % width == 0 and
            offset+(count-1)*stride+packed <= length, 'invalid collision accessor span')
    rows = [struct.unpack_from('<'+str(components)+code, binary, start+offset+i*stride) for i in range(count)]
    require(all(math.isfinite(x) for row in rows for x in row), 'nonfinite collision coordinate')
    return rows if floating else [row[0] for row in rows]


def encode(vertices, triangles, api):
    require = api.require
    require(0 < len(vertices) <= MAX_VERTICES and 0 < len(triangles) <= MAX_TRIANGLES, 'collision exceeds vertex/triangle limits')
    nv, nt = native_size(vertices, triangles)
    require(nv <= MAX_VERTICES and nt <= MAX_TRIANGLES, 'collision exceeds vertex/triangle limits (lava counts twice: its surface and its bottom)')
    # Round once to the exact float32 coordinates consumed by native collision.
    vertices = [struct.unpack('>3f', struct.pack('>3f', *p)) for p in vertices]
    require(all(math.isfinite(x) and abs(x) <= MAX_EXTENT for p in vertices for x in p), 'collision coordinate exceeds %d game units' % MAX_EXTENT)
    low = [min(p[k] for p in vertices) for k in range(3)]
    high = [max(p[k] for p in vertices) for k in range(3)]
    require(all(high[k]-low[k] <= MAX_EXTENT for k in range(3)), 'collision extent exceeds %d game units' % MAX_EXTENT)
    for a, b, c, semantic in triangles:
        require(max(a, b, c) < len(vertices) and min(a, b, c) >= 0 and semantic in SEMANTICS.values(), 'invalid collision triangle')
        u = [vertices[b][k]-vertices[a][k] for k in range(3)]
        v = [vertices[c][k]-vertices[a][k] for k in range(3)]
        cross = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
        require(sum(x*x for x in cross) > 1e-12, 'degenerate collision triangle')
    body = struct.pack('>4sIII6f', b'WCOL', 1, len(vertices), len(triangles), *low, *high)
    body += b''.join(struct.pack('>3f', *p) for p in vertices)
    body += b''.join(struct.pack('>4H', *t) for t in triangles)
    return body, {'collision_version': 1, 'collision_vertices': len(vertices),
                  'collision_triangles': len(triangles), 'collision_minimum': low,
                  'collision_maximum': high, 'collision_bytes': len(body),
                  'collision_sha256': hashlib.sha256(body).hexdigest()}


def read_static(path, collision=False):
    # Late import keeps actor_model.read_glb usable by the skinned converter.
    import actor_model as api
    require = api.require
    try:
        doc, binary = _document(path, api)
        # Never discard unsupported data while projecting the render mesh.
        def check(value):
            if isinstance(value, dict):
                require(not value.get('extensions'), 'GLB extensions are unsupported')
                for child in value.values(): check(child)
            elif isinstance(value, list):
                for child in value: check(child)
        check(doc)
        require(not any(doc.get(k) for k in ('skins', 'animations', 'extensionsUsed', 'extensionsRequired')), 'static GLB cannot use skinning, animation or extensions')
        nodes, meshes = doc.get('nodes', []), doc.get('meshes', [])
        require(0 < len(nodes) <= 256 and 0 < len(meshes) <= 64, 'invalid static scene size')
        clean = copy.deepcopy(doc)
        sources, render = [], []
        used_meshes = set()
        for i, node in enumerate(nodes):
            if 'mesh' not in node: continue
            mi = api.integer(node['mesh'])
            require(mi < len(meshes), 'node mesh outside input')
            used_meshes.add(mi)
            named = str(node.get('name', '')).endswith('_col') or str(meshes[mi].get('name', '')).endswith('_col')
            (sources if named else render).append((i, mi))
            clean['nodes'][i].pop('mesh', None)
        require(len(render) == 1 and used_meshes == set(range(len(meshes))), 'one render mesh instance and no unused meshes required')
        ri, rm = render[0]
        render_mesh = copy.deepcopy(meshes[rm])
        primitives = render_mesh.get('primitives', [])
        require(isinstance(primitives, list) and 1 <= len(primitives) <= api.MAX_PRIMITIVES, 'the visible mesh needs 1 to %d primitives' % api.MAX_PRIMITIVES)
        # The visible materials, in first-use order; collision-only materials are left out.
        used = []
        for prim in primitives:
            material = api.integer(prim['material'])
            require(material < len(doc.get('materials', [])), 'render material outside input')
            if material not in used: used.append(material)
            prim['material'] = used.index(material)
        material = used[0]
        clean['nodes'][ri]['mesh'] = 0
        clean['meshes'] = [render_mesh]
        clean['materials'] = [doc['materials'][m] for m in used]
        # One plain opaque material keeps the original single-material conversion byte for byte.
        legacy = len(primitives) == 1 and api.plain_material(doc['materials'][material])
        reader = api.read_glb if legacy else api.read_parts
        if not sources:
            geometry, info = reader(path)
        else:
            with tempfile.TemporaryDirectory() as directory:
                projected = os.path.join(directory, 'render.glb')
                with open(projected, 'wb') as target:
                    target.write(_pack(clean, binary))
                geometry, info = reader(projected)
        # Obtain every collision node world matrix using the same hierarchy.
        world, visited = {}, set()
        def visit(index, parent):
            index = api.integer(index)
            require(index < len(nodes) and index not in visited, 'invalid/cyclic/shared node')
            visited.add(index)
            node = nodes[index]
            require(not any(k in node for k in ('skin', 'weights', 'extensions')), 'unsupported node')
            m = api.matrix_mul(parent, api.node_matrix(node))
            world[index] = m
            for child in node.get('children', []): visit(child, m)
        for root in doc['scenes'][0]['nodes']: visit(root, api.node_matrix({}))
        require(all(i in visited for i, _ in sources+render), 'mesh outside active scene')
        vertices, triangles, welded = [], [], {}
        def append(points, indices, semantic):
            require(len(indices) % 3 == 0 and max(indices) < len(points), 'invalid collision triangle indices')
            require(len(triangles)+len(indices)//3 <= MAX_TRIANGLES, 'collision exceeds triangle limit')
            remap = []
            for p in points:
                p = struct.unpack('>3f', struct.pack('>3f', *p))
                if p not in welded:
                    require(len(vertices) < MAX_VERTICES, 'collision exceeds vertex limit')
                    welded[p] = len(vertices); vertices.append(p)
                remap.append(welded[p])
            for at in range(0, len(indices), 3):
                triangles.append(tuple(remap[x] for x in indices[at:at+3])+(semantic,))
        raw_vertices = 0
        for ni, mi in sources:
            mesh = meshes[mi]
            require(mesh.get('weights') is None and not mesh.get('extensions'), 'unsupported collision mesh')
            require(0 < len(mesh.get('primitives', [])) <= 64, 'collision primitives exceed limit')
            m = world[ni]
            a,b,c = [row[:3] for row in m[:3]]
            det = a[0]*(b[1]*c[2]-b[2]*c[1])-a[1]*(b[0]*c[2]-b[2]*c[0])+a[2]*(b[0]*c[1]-b[1]*c[0])
            require(math.isfinite(det) and abs(det) > 1e-12, 'singular collision transform')
            for prim in mesh['primitives']:
                require(prim.get('mode', 4) == 4 and prim.get('targets') is None and prim.get('extensions') is None, 'collision requires unmorphed triangles')
                attrs = prim.get('attributes', {})
                require('POSITION' in attrs and set(attrs) <= {'POSITION', 'NORMAL', 'TEXCOORD_0'}, 'unsupported collision attributes')
                pos = _accessor(doc, binary, attrs['POSITION'], True, api)
                indices = _accessor(doc, binary, prim['indices'], False, api)
                for key in ('NORMAL', 'TEXCOORD_0'):
                    if key in attrs:
                        require(len(_accessor(doc, binary, attrs[key], key, api)) == len(pos), 'collision attribute counts differ')
                raw_vertices += len(pos)
                require(raw_vertices <= MAX_VERTICES, 'collision source vertices exceed limit')
                mat = api.integer(prim['material'])
                require(mat < len(doc['materials']), 'collision material outside input')
                name = doc['materials'][mat].get('name')
                require(name in SEMANTICS, 'unknown or unsupported collision material: '+str(name))
                placed = [tuple(100*(sum(m[r][k]*p[k] for k in range(3))+m[r][3]) for r in range(3)) for p in pos]
                if det < 0:
                    indices = [x for at in range(0, len(indices), 3) for x in (indices[at], indices[at+2], indices[at+1])]
                append(placed, indices, SEMANTICS[name])
        if not sources and collision:
            # Collision from the visible triangles: each visible material's col_* name selects its surface.
            pieces = [(geometry[0], geometry[3], doc['materials'][material])] if legacy else \
                [(part.pos, part.indices, doc['materials'][used[part.material]]) for part in geometry.parts]
            for points, indices, source_material in pieces:
                matname = source_material.get('name', '')
                require(isinstance(matname, str) and (not matname.startswith('col_') or matname in SEMANTICS), 'unknown or unsupported collision material: '+matname)
                append(points, indices, SEMANTICS.get(matname, 0))
        data = None
        if sources or collision:
            data, metadata = encode(vertices, triangles, api)
            info.update(metadata)
        return geometry, info, data
    except (KeyError, IndexError, AttributeError, TypeError, ValueError, OverflowError, struct.error, RecursionError) as exc:
        raise api.BuildError('malformed collision GLB: '+str(exc)) from exc
