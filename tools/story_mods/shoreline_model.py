#!/usr/bin/env python3
"""Player-local coast material and animation on original mod geometry.

Island materials serve as templates (and Fire Mountain's lava): "foam", Outset's animated coast foam; "water", the spring pool's surface of
Mother & Child Isles with its moving ripples (a pond standing above the sea); "river", the flowing water of the same
island's spring outflow (its texture scrolls along V: V grows downstream); "waterfall", Forest Haven's falling water
(the same flow texture, scrolling three times as fast; V grows downwards); "lava", the moving lava of Fire Mountain
(the volcano object's own lava material and its texture animation). A water or river surface fades out towards
its open edges (vertex alpha 0 on the mesh's boundary, 1 inside), as the island's own water does; a waterfall gets
the vertex colours Forest Haven's falls give that material (its blue channel from 1 at the top to 0.3 at the foot).

No game data is shipped: the native material, textures and animation are read
from the player's installation and the output stays in their model cache.
"""
import hashlib
import json
import math
import os
import re
import struct
import tempfile

import sys
# Windows' embeddable Python runs isolated: the script's own folder is not on sys.path, so the
# sibling modules are found through it explicitly (issue #280).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notebook_model as base  # noqa: E402
from skinned_actor_model import append_dictionary, append_string

VERSION = 4
# Each kind: the island room whose water model (model1) holds its template materials (or a room of another stage and
# its model: stage, room, model), and the material of each part:
# "surface" the water itself, "waves" lines on it, "edge" the foam along a shore or a lip, "splash" where water lands
# (the falls' sibuki). One room per kind: its model1 shader archive serves every part.
KINDS = {
    'foam': (44, {'surface': 'SC_01_mizu_v_x'}),
    'water': (9, {'surface': 'Txe_water_v_3_', 'edge': 'Txe_water_v_4_', 'splash': 'Txe_water_v'}),
    # an island's calm shore water, as Outset's own: the blue of its shallows (namisoko), the wave lines over them (nami)
    # and the coast foam along the shore
    'pond': (44, {'surface': 'SC_01_mizu_v_3_', 'waves': 'SC_01_mizuB_v_x', 'edge': 'SC_01_mizu_v_x'}),
    'river': (41, {'surface': 'SC_00_m_v_x', 'edge': 'SC_00_m', 'splash': 'SC_00_m_2_'}),
    'waterfall': (41, {'surface': 'SC_00_m_v_x', 'edge': 'SC_00_m', 'splash': 'SC_00_m_2_'}),
    # lava: Fire Mountain's own (the volcano's object, Ykzyg: the lava flowing from its crater, model qkzyg), its lava
    # layers and glowing grains moving with the object's texture animation, as on the mountain at sea
    'lava': (('object', 'Ykzyg', 'qkzyg'), {'surface': 'Qkzyg_yougn_only_b_yougan_v'}),
}
ROLES = ('surface', 'waves', 'edge', 'splash')
# Fire Mountain's lava runs in streams down the mountain: its first texture (Txy_yougan2) is a mask of vertical bands
# laid by the texture coordinates: the molten lava glows bright where it is 1, between the streams it is a dull crust
# (0). A pool is molten lava all over, so the mask is one value there: 255 (the converter fills that texture with it;
# its point colours black, as most of the mountain's).
FILLED_TEXTURES = {'lava': [('Txy_yougan2', 255)]}
MATERIALS = {kind: parts['surface'] for kind, (_, parts) in KINDS.items()}
MATERIAL = MATERIALS['foam']
# the room archive whose water model (model1) holds each template material
TEMPLATE_ROOMS = {kind: room for kind, (room, _) in KINDS.items()}
PACKS = ('first_szs_permanent.pack', 'szs_permanent0.pack', 'szs_permanent1.pack', 'szs_permanent2.pack')


def require(ok, message):
    if not ok:
        raise base.BuildError(message)


def single_dictionary(f, name, target):
    encoded = name.encode('ascii')
    text = f.append(struct.pack('>I', len(encoded)) + encoded + b'\0', 4) + 4
    at = f.append(bytes(40), 4)
    f.put('II', at, 40, 1)
    f.put('IHH', at + 8, 0xffffffff, 1, 0)
    f.put('IHH', at + 24, (len(encoded)-1)*8+(encoded[-1].bit_length()-1), 0, 1)
    f.set_rel(at + 32, text)
    f.set_rel(at + 36, target)
    return at, text


def edge_alpha(positions, indices):
    """per vertex: 0 on the mesh's open edges (an edge of one triangle; welded by position), 1 inside"""
    key = lambda i: tuple(round(v, 4) for v in positions[i])
    edges = {}
    for t in range(0, len(indices) - 2, 3):
        tri = [key(indices[t + k]) for k in range(3)]
        for k in range(3):
            e = tuple(sorted((tri[k], tri[(k + 1) % 3])))
            edges[e] = edges.get(e, 0) + 1
    open_points = {p for e, n in edges.items() if n == 1 for p in e}
    return [0.0 if key(i) in open_points else 1.0 for i in range(len(positions))]


def build_bfres(template, geometry, identity, material_name=MATERIAL):
    """One part: the template material `material_name` on `geometry` (positions, normals, uv, indices, image)."""
    return build_parts(template, [(material_name, geometry, None)], identity)


def vertex_layout(f, vertex):
    attrs = f.rel(vertex + 0x10)
    layout = {f.cstr(f.rel(attrs + i*12)): (f.b[attrs+i*12+4], f.u16(attrs+i*12+6), f.u32(attrs+i*12+8)) for i in range(f.b[vertex+4])}
    # one interleaved buffer: position, texture coordinates and optional colour (4 floats) and normal (3 floats)
    want = {'_p0': 0x811, '_u0': 0x80D, '_c0': 0x813, '_n0': 0x811}
    require('_p0' in layout and '_u0' in layout and set(layout) <= set(want) and f.b[vertex+5] == 1 and
            all(layout[k][0] == 0 and layout[k][2] == want[k] for k in layout), 'unsupported player coast vertex layout')
    sizes = {0x811: 12, 0x813: 16, 0x80D: 8}
    stride = f.u16(f.rel(vertex + 0x18) + 0xC) if f.rel(vertex + 0x18) else 0
    extent = max(layout[k][1] + sizes[want[k]] for k in layout)
    if not (extent <= stride <= 64 and stride % 4 == 0):  # the template's own stride, else the records' extent
        stride = -(-extent // 16) * 16
    require(stride <= 64, 'unsupported player coast vertex layout')
    return layout, stride


def open_points(positions, indices):
    key = lambda i: tuple(round(v, 4) for v in positions[i])
    alpha = edge_alpha(positions, indices)
    return [a == 0.0 for a in alpha]


def part_colours(kind, role, positions, indices):
    """per vertex colour (r, g, b, a) of a part, as the game's own models of that material have them: the still
    and the flowing water of Mother & Child Isles white, fading out (alpha) at the mesh's open edges; Forest Haven's
    stream and falls (red: their foam, blue: from 1 down to 0.3 at a fall's foot) and its splashes (black)"""
    if kind in ('river', 'waterfall') and role == 'surface':
        if kind == 'waterfall':
            top = max(p[1] for p in positions)
            bottom = min(p[1] for p in positions)
            span = top - bottom or 1.0
            return [(0.2, 0.0, 1.0 - 0.7 * (top - p[1]) / span, 1.0) for p in positions]
        edge = open_points(positions, indices)
        return [(0.5 if e else 0.0, 0.0, 1.0, 1.0) for e in edge]
    if kind in ('river', 'waterfall') and role == 'splash':
        return [(0.0, 0.0, 0.0, 1.0)] * len(positions)
    if kind == 'lava':
        # Fire Mountain's lava is black in most of its points (its molten cells; lighter where it thins out)
        return [(0.0, 0.0, 0.0, 1.0)] * len(positions)
    alpha = edge_alpha(positions, indices) if (kind == 'water' and role == 'surface') or (kind == 'pond' and role == 'waves') else [1.0] * len(positions)
    return [(1.0, 1.0, 1.0, a) for a in alpha]


def template_model(kind):
    """the name of the template room's (or object's) model a kind's materials are in"""
    room = TEMPLATE_ROOMS[kind]
    return room[2] if isinstance(room, tuple) else 'model1'


def object_archive(game, name):
    """the files of an object archive of the player's game (Object/<name>.szs, or one in the permanent packs)"""
    archive = name + '.szs'
    raw = None
    loose = os.path.join(game, 'content/Common/Object', archive)
    if os.path.isfile(loose):
        with open(loose, 'rb') as source:
            raw = source.read()
    for pack in PACKS if raw is None else ():
        path = os.path.join(game, 'content/Common/Pack', pack)
        if not os.path.isfile(path):
            continue
        with open(path, 'rb') as source:
            packed = dict(base.shaderprep.sarc(source.read()))
        if archive in packed:
            raw = packed[archive]
            break
    require(raw is not None, 'player coast template unavailable')
    return dict(base.shaderprep.sarc(base.shaderprep.yaz0(raw)))


def build_parts(template, parts, identity, model_name='model1'):
    """parts: [(template material, (positions, normals, uv, indices, ...), colours or None)], each drawn with its own copy
    of the shape that material has in the player's island model1 (one shape and vertex buffer per part)."""
    require(re.fullmatch(r'[a-z][a-z0-9_]{0,30}', identity) is not None, 'invalid coast identity')
    require(1 <= len(parts) <= len(ROLES), 'a shoreline has 1 to 4 parts')
    f = base.Bfres(template)
    models = dict(f.dict(f.rel(0x20)))
    require(model_name in models, 'player coast model unavailable')
    model = models[model_name]
    materials = f.dict(f.rel(model + 0x18))
    names = [name for name, _ in materials]
    shapes = f.dict(f.rel(model + 0x14))
    require(sum(len(g[3]) // 3 for _, g, _ in parts) <= 1024 and all(len(g[3]) for _, g, _ in parts), 'coast must have 1..1024 triangles')
    kept_shapes, kept_materials = [], []
    # each part's template shape, chosen before any is changed (a changed shape names its new material index)
    chosen = []
    for material_name, _, _ in parts:
        require(material_name in names, 'player coast material unavailable')
        require(material_name not in [m for m, _ in chosen], 'a shoreline uses each material once')
        material_index = names.index(material_name)
        selected = [at for _, at in shapes if f.u16(at + 0xE) == material_index]
        require(len(selected) >= 1, 'player coast shape unavailable')
        chosen.append((material_name, selected[0]))
    for i, (material_name, geometry, colours) in enumerate(parts):
        material_index = names.index(material_name)
        shape = chosen[i][1]
        vertex = f.rel(shape + 0x20)
        layout, stride = vertex_layout(f, vertex)
        positions, normals, uv, indices = geometry[0], geometry[1], geometry[2], geometry[3]
        require(len(positions) == len(normals) == len(uv), 'coast attribute counts differ')
        require(all(math.isfinite(v) for rows in (positions, normals, uv) for row in rows for v in row), 'non-finite coast geometry')
        if colours is None:
            colours = [(1.0, 1.0, 1.0, a) for a in (edge_alpha(positions, indices) if material_name == MATERIALS['water'] else [1.0] * len(positions))]
        records = []
        for p, n, t, c in zip(positions, normals, uv, colours):
            r = bytearray(stride)
            if '_c0' in layout:
                struct.pack_into('>4f', r, layout['_c0'][1], *c)
            struct.pack_into('>3f', r, layout['_p0'][1], *p)
            if '_n0' in layout:
                struct.pack_into('>3f', r, layout['_n0'][1], *n)
            struct.pack_into('>2f', r, layout['_u0'][1], *t)
            records.append(bytes(r))
        vertex_bytes = b''.join(records)
        f.put('H', vertex + 6, i)
        f.put('I', vertex + 8, len(positions))
        f.b[vertex + 0xC] = 0
        buffer = f.rel(vertex + 0x18)
        f.put('I', buffer + 4, len(vertex_bytes))
        f.put('H', buffer + 0xC, stride)
        f.set_rel(buffer + 0x14, f.append(vertex_bytes, 0x1000))
        f.put('HHHH', shape + 0xC, i, i, 0, i)
        f.put('H', shape + 0x14, 0)
        f.b[shape + 0x16] = 0
        f.b[shape + 0x17] = 1
        f.b[shape + 0x18] = 0
        f.b[shape + 0x19] = 0
        f.put('H', shape + 0x1A, 1)
        f.put('i', shape + 0x28, 0)
        f.put('i', shape + 0x2C, 0)
        mesh = f.rel(shape + 0x24)
        index_bytes = struct.pack('>%dI' % len(indices), *indices)
        f.put('II', mesh, 4, 9)
        f.put('I', mesh + 8, len(indices))
        f.put('H', mesh + 0xC, 1)
        f.put('I', mesh + 0x18, 0)
        group = f.append(struct.pack('>II', 0, len(indices)), 4)
        f.set_rel(mesh + 0x10, group)
        index_buffer = f.rel(mesh + 0x14)
        f.put('I', index_buffer + 4, len(index_bytes))
        f.set_rel(index_buffer + 0x14, f.append(index_bytes, 0x1000))
        low = [min(p[k] for p in positions) for k in range(3)]
        high = [max(p[k] for p in positions) for k in range(3)]
        center = [(a+b)*.5 for a,b in zip(low,high)]
        extent = [(b-a)*.5 for a,b in zip(low,high)]
        f.put('f', shape + 0x1C, math.sqrt(sum(v*v for v in extent)))
        f.set_rel(shape + 0x30, f.append(struct.pack('>6H',0,0,0,0,0,1),4))
        f.set_rel(shape + 0x34, f.append(struct.pack('>6f',*center,*extent),4))
        f.set_rel(shape + 0x38, f.append(struct.pack('>H',0),4))
        kept_shapes.append(shape)
        kept_materials.append((material_name, materials[material_index][1]))
    skeleton = f.rel(model + 0xC)
    bones = f.rel(skeleton + 0x14)
    for i in range(f.u16(skeleton + 8)):
        bone = bones + 64*i
        f.put('3f', bone + 0x14, 1,1,1)
        f.put('4f', bone + 0x20, 0,0,0,1)
        f.put('3f', bone + 0x30, 0,0,0)
    animation_entries = dict(f.dict(f.rel(0x34)))
    require(model_name in animation_entries and (model_name != 'model1' or len(animation_entries) == 1), 'player coast animation unavailable')
    animation = animation_entries[model_name]
    require(f.b[animation:animation+4] == b'FSHU' and 1 < f.u32(animation + 0x10) <= 1000, 'unsupported player coast animation')
    f.append(b'',0x1000)
    f.put('I', 0xC, len(f.b))
    return compact(bytes(f.b), identity, model, kept_shapes, kept_materials, animation)


def compact(data, identity, model, shapes, materials, animation):
    """Keep only the coast's reachable resource graph (its shapes, their materials and textures, the materials'
    texture animation rows), within the model budget."""
    source = base.Bfres(data)
    out = base.Bfres(data[:0x70])
    out.b[0x18:0x68] = bytes(0x50)
    n = len(shapes)

    def block(at, size, alignment=4):
        require(0 < at <= len(data)-size, 'player coast resource span invalid')
        return out.append(data[at:at+size], alignment)

    def text(at):
        return append_string(out, source.cstr(at))

    def pointer(dst, original, offset, size):
        target = source.rel(original+offset)
        if target:
            out.set_rel(dst+offset, block(target,size))
        else:
            out.put('i',dst+offset,0)

    def strings(original):
        return append_dictionary(out,[(name,text(value)) for name,value in source.dict(original)])

    dst_model = block(model,0x30)
    out.set_rel(dst_model+4,append_string(out,identity))
    out.put('i',dst_model+8,0)
    out.put('i',dst_model+0x1C,0)
    out.put('HHHH',dst_model+0x20,n,n,n,0)
    out.set_rel(0x20,append_dictionary(out,[(identity,dst_model)]))
    out.put('H',0x50,1)
    skeleton = source.rel(model+0xC)
    dst_skeleton = block(skeleton,0x24)
    out.put('HHH',dst_skeleton+8,1,0,0)
    bone = source.rel(skeleton+0x14)
    dst_bone = block(bone,0x40)
    out.set_rel(dst_bone,append_string(out,'root'))
    out.put('HHHH',dst_bone+4,0,0xFFFF,0xFFFF,0xFFFF)
    out.set_rel(dst_skeleton+0x10,append_dictionary(out,[('root',dst_bone)]))
    out.set_rel(dst_skeleton+0x14,dst_bone)
    for offset in (0x18,0x1C,0x20):
        out.put('i',dst_skeleton+offset,0)
    out.set_rel(dst_model+0xC,dst_skeleton)
    # the vertex buffers: one array, in the parts' order
    vertices = [source.rel(shape+0x20) for shape in shapes]
    dst_vertices = [block(vertex,0x20) for vertex in vertices]
    require(all(dst_vertices[i] == dst_vertices[0] + 0x20*i for i in range(n)), 'player coast vertex array not contiguous')
    for vertex, dst_vertex in zip(vertices, dst_vertices):
        attributes = source.rel(vertex+0x10)
        nattr = source.b[vertex+4]
        dst_attributes = block(attributes,12*nattr)
        attr_entries = []
        for i in range(nattr):
            name = source.cstr(source.rel(attributes+i*12))
            out.set_rel(dst_attributes+i*12,append_string(out,name))
            attr_entries.append((name,dst_attributes+i*12))
        out.set_rel(dst_vertex+0x10,dst_attributes)
        out.set_rel(dst_vertex+0x14,append_dictionary(out,attr_entries))
        out.put('I',dst_vertex+0x1C,0)
        buffer = source.rel(vertex+0x18)
        dst_buffer = block(buffer,24)
        out.set_rel(dst_buffer+0x14,block(source.rel(buffer+0x14),source.u32(buffer+4),0x1000))
        out.set_rel(dst_vertex+0x18,dst_buffer)
    out.set_rel(dst_model+0x10,dst_vertices[0])
    shape_entries = []
    for i, shape in enumerate(shapes):
        dst_shape = block(shape,0x40)
        name = 'coast' if i == 0 else 'coast%d' % i
        out.set_rel(dst_shape+4,append_string(out,name))
        out.put('HHHH',dst_shape+0xC,i,i,0,i)
        out.put('i',dst_shape+0x28,0)
        out.put('i',dst_shape+0x2C,0)
        out.put('I',dst_shape+0x3C,0)
        for offset,size in [(0x30,12),(0x34,24),(0x38,2)]:
            pointer(dst_shape,shape,offset,size)
        out.set_rel(dst_shape+0x20,dst_vertices[i])
        mesh = source.rel(shape+0x24)
        dst_mesh = block(mesh,28)
        pointer(dst_mesh,mesh,0x10,8)
        buffer = source.rel(mesh+0x14)
        dst_buffer = block(buffer,24)
        out.set_rel(dst_buffer+0x14,block(source.rel(buffer+0x14),source.u32(buffer+4),0x1000))
        out.set_rel(dst_mesh+0x14,dst_buffer)
        out.set_rel(dst_shape+0x24,dst_mesh)
        shape_entries.append((name,dst_shape))
    out.set_rel(dst_model+0x14,append_dictionary(out,shape_entries))
    all_textures=dict(source.dict(source.rel(0x24)))
    copied={}
    unique=[]
    material_entries=[]
    for index,(material_name,material) in enumerate(materials):
        dst_material = block(material,0x4C)
        out.set_rel(dst_material+4,append_string(out,material_name))
        out.put('H',dst_material+0xC,index)
        infos = []
        for name,value in source.dict(source.rel(material+0x1C)):
            require(source.u16(value)==1 and data[value+2]==0,'unsupported coast render info')
            dst_info=block(value,16)
            out.set_rel(dst_info+4,append_string(out,name))
            infos.append((name,dst_info))
        out.set_rel(dst_material+0x1C,append_dictionary(out,infos))
        pointer(dst_material,material,0x20,48)
        shader = source.rel(material+0x24)
        dst_shader = block(shader,28)
        for offset in (0,4):
            out.set_rel(dst_shader+offset,text(source.rel(shader+offset)))
        for offset in (0x10,0x14,0x18):
            target=source.rel(shader+offset)
            out.set_rel(dst_shader+offset,strings(target)) if target else out.put('i',dst_shader+offset,0)
        out.set_rel(dst_material+0x24,dst_shader)
        textures=source.rel(material+0x28)
        ntex,nsamp=data[material+0x10],data[material+0x11]
        require(1<=ntex<=4 and nsamp==ntex,'unsupported coast samplers')
        dst_textures=block(textures,8*ntex)
        texture_names=[source.cstr(source.rel(textures+8*i)) for i in range(ntex)]
        for i in range(ntex):
            out.set_rel(dst_textures+8*i,text(source.rel(textures+8*i)))
            out.put('I',dst_textures+8*i+4,0)
        out.set_rel(dst_material+0x28,dst_textures)
        samplers=source.rel(material+0x2C)
        dst_samplers=block(samplers,24*nsamp)
        sampler_entries=[]
        for name,value in source.dict(source.rel(material+0x30)):
            dst=dst_samplers+value-samplers
            out.set_rel(dst+0xC,append_string(out,name))
            sampler_entries.append((name,dst))
        out.set_rel(dst_material+0x2C,dst_samplers)
        out.set_rel(dst_material+0x30,append_dictionary(out,sampler_entries))
        params=source.rel(material+0x34)
        count=source.u16(material+0x12)
        dst_params=block(params,count*20)
        param_entries=[]
        for name,value in source.dict(source.rel(material+0x38)):
            dst=dst_params+value-params
            out.set_rel(dst+16,append_string(out,name))
            param_entries.append((name,dst))
        out.set_rel(dst_material+0x34,dst_params)
        out.set_rel(dst_material+0x38,append_dictionary(out,param_entries))
        pointer(dst_material,material,0x3C,source.u16(material+0x16))
        pointer(dst_material,material,0x44,(count+7)//8)
        out.put('i',dst_material+0x40,0)
        out.put('H',dst_material+0x1A,0)
        out.put('I',dst_material+0x48,0)
        material_entries.append((material_name,dst_material))
        for name in texture_names:
            if name not in copied:
                require(name in all_textures,'player coast texture unavailable')
                texture=all_textures[name]
                dst_texture=block(texture,0xC0)
                for offset in (0xA8,0xAC):
                    target=source.rel(texture+offset)
                    out.set_rel(dst_texture+offset,text(target)) if target else out.put('i',dst_texture+offset,0)
                for offset,size in [(0xB0,source.u32(texture+0x24)),(0xB4,source.u32(texture+0x2C))]:
                    if size:
                        out.set_rel(dst_texture+offset,block(source.rel(texture+offset),size,0x2000))
                    else:
                        out.put('i',dst_texture+offset,0)
                out.put('II',dst_texture+0xB8,0,0)
                copied[name]=dst_texture
                unique.append(name)
        for i,name in enumerate(texture_names):
            out.set_rel(dst_textures+8*i+4,copied[name])
    out.set_rel(dst_model+0x18,append_dictionary(out,material_entries))
    out.set_rel(0x24,append_dictionary(out,[(name,copied[name]) for name in unique]))
    out.put('H',0x52,len(unique))
    # the materials' texture animation: the template's rows for them, in their order
    rows=source.rel(animation+0x2C)
    row_of={source.cstr(source.rel(rows+i*32+0x10)):rows+i*32 for i in range(source.u16(animation+0x14))}
    kept=[(name,row_of[name]) for name,_ in materials if name in row_of]
    require(kept and materials[0][0] in row_of,'player coast animation binding unavailable')
    dst_animation=block(animation,0x34)
    out.set_rel(dst_animation+4,append_string(out,identity))
    out.put('i',dst_animation+8,0)
    out.put('H',dst_animation+0x14,len(kept))
    out.put('H',dst_animation+0x16,0)
    out.set_rel(dst_animation+0x24,dst_model)
    out.set_rel(dst_animation+0x28,out.append(struct.pack('>%dH' % len(kept),*([0xFFFF]*len(kept))),4))
    out.put('i',dst_animation+0x30,0)
    dst_rows=[block(row,32) for _,row in kept]
    require(all(dst_rows[i]==dst_rows[0]+32*i for i in range(len(kept))),'player coast animation rows not contiguous')
    params_total=curves_total=0
    for (name,row),dst_row in zip(kept,dst_rows):
        # where this row's curves and results begin, after the rows before it (as the game's own animations count them)
        out.put('II',dst_row+8,curves_total,params_total*4)
        out.set_rel(dst_row+0x10,append_string(out,name))
        param_count,curve_count,constant_count=source.u16(row),source.u16(row+2),source.u16(row+4)
        params_total+=param_count
        curves_total+=curve_count
        params=source.rel(row+0x14)
        dst_params=block(params,param_count*16)
        for i in range(param_count):
            out.set_rel(dst_params+i*16+12,text(source.rel(params+i*16+12)))
        out.set_rel(dst_row+0x14,dst_params)
        pointer(dst_row,row,0x1C,constant_count*8)
        curves=source.rel(row+0x18)
        dst_curves=block(curves,curve_count*36)
        for i in range(curve_count):
            original=curves+i*36
            dst=dst_curves+i*36
            require(source.u16(original)==0,'unsupported player coast curve encoding')
            keys=source.u16(original+2)
            pointer(dst,original,28,keys*4)
            pointer(dst,original,32,keys*16)
        out.set_rel(dst_row+0x18,dst_curves)
    out.put('I',dst_animation+0x18,params_total*4)
    out.put('I',dst_animation+0x1C,curves_total)
    # the baked size, as the game's own animations have it: every curve, a value per frame and its header
    out.put('I',dst_animation+0x20,curves_total*(source.u32(animation+0x10)*4+20))
    out.set_rel(dst_animation+0x2C,dst_rows[0])
    out.set_rel(0x34,append_dictionary(out,[(identity,dst_animation)]))
    out.put('H',0x5A,1)
    out.set_rel(0x14,append_string(out,identity))
    out.append(b'',0x1000)
    out.put('I',0xC,len(out.b))
    require(len(out.b)<1024*1024,'coast resource exceeds its budget')
    return bytes(out.b)


def room_archive(game, stage, room):
    """the files of a room archive of the player's game: a loose stage file, or one in the permanent packs"""
    archive = '%s_Room%d.szs' % (stage, room)
    raw = None
    loose = os.path.join(game, 'content/Common/Stage', archive)
    if os.path.isfile(loose):
        with open(loose, 'rb') as source:
            raw = source.read()
    for pack in PACKS if raw is None else ():
        path = os.path.join(game, 'content/Common/Pack', pack)
        if not os.path.isfile(path):
            continue
        with open(path, 'rb') as source:
            packed = dict(base.shaderprep.sarc(source.read()))
        if archive in packed:
            raw = packed[archive]
            break
    require(raw is not None, 'player coast template unavailable')
    return dict(base.shaderprep.sarc(base.shaderprep.yaz0(raw)))


def player_template(game, kind='foam'):
    room = TEMPLATE_ROOMS[kind]
    model = template_model(kind)
    if isinstance(room, tuple) and room[0] == 'object':
        files = object_archive(game, room[1])
        bfres = room[1] + '.bfres'
    else:
        stage, room = (room[0], room[1]) if isinstance(room, tuple) else ('sea', room)
        files = room_archive(game, stage, room)
        bfres = 'Room%d.bfres' % room
    require(bfres in files and model + '.sharcfb' in files, 'player coast template unavailable')
    template = files[bfres]
    for texture, value in FILLED_TEXTURES.get(kind, ()):
        template = fill_texture(template, texture, value)
    return template, files[model + '.sharcfb']


def fill_texture(template, name, value):
    """the template with a one-channel (BC4) texture filled with one value (0..255) in its image and all its levels:
    our own constant, nothing of the texture's own picture stays"""
    f = base.Bfres(template)
    textures = dict(f.dict(f.rel(0x24)))
    require(name in textures, 'player coast texture unavailable')
    texture = textures[name]
    surface = texture + 4
    require(f.u32(surface + 0x14) == 0x34, 'player coast texture of another format')
    block = bytes((value, value, 0, 0, 0, 0, 0, 0))
    for data, size in ((f.rel(texture + 0xB0), f.u32(surface + 0x20)), (f.rel(texture + 0xB4), f.u32(surface + 0x28))):
        if data and size:
            require(size % 8 == 0 and data + size <= len(f.b), 'player coast texture data outside the file')
            f.b[data:data + size] = block * (size // 8)
    return bytes(f.b)


def shoreline_parts(geometry, kind):
    """The parts of a read shoreline model, each with its role: one plain material is the surface; several
    materials are named after their roles (surface, edge, splash), and triangles of one role are merged."""
    if isinstance(geometry, tuple):
        return [('surface', geometry)]
    roles = {}
    for part in geometry.parts:
        role = geometry.materials[part.material].name
        require(role in ROLES and role in KINDS[kind][1], 'a %s shoreline\'s materials are named %s' % (kind, ', '.join(KINDS[kind][1])))
        pos, nrm, uv, idx = roles.setdefault(role, ([], [], [], []))
        base_index = len(pos)
        pos.extend(part.pos)
        nrm.extend(part.nrm)
        uv.extend(part.uv)
        idx.extend(base_index + i for i in part.indices)
    require('surface' in roles, 'a shoreline needs its surface')
    return [(role, (*roles[role], b'')) for role in ROLES if role in roles]


def build(game, glb_path, identity, out, kind='foam'):
    require(kind in KINDS, 'unknown shoreline material')
    import collision_mesh
    with open(glb_path,'rb') as source:
        source_bytes=source.read(8*1024*1024+1)
    require(len(source_bytes)<=8*1024*1024,'coast GLB exceeds 8 MiB')
    with tempfile.TemporaryDirectory() as directory:
        snapshot=os.path.join(directory,'coast.glb')
        with open(snapshot,'wb') as target:
            target.write(source_bytes)
        geometry, info, _ = collision_mesh.read_static(snapshot)
    parts = shoreline_parts(geometry, kind)
    require(kind != 'foam' or len(parts) == 1, 'a coast foam shoreline has one opaque material (the game supplies the foam)')
    template, shader = player_template(game, kind)
    result = build_parts(template, [(KINDS[kind][1][role], g, part_colours(kind, role, g[0], g[3])) for role, g in parts], identity,
                         template_model(kind))
    packed = base.yaz0_stored(base.sarc({identity+'.bfres':result,identity+'.sharcfb':shader}))
    digest = hashlib.sha256()
    for path in (__file__, collision_mesh.__file__, base.__file__, os.path.join(os.path.dirname(__file__),'actor_model.py'), os.path.join(os.path.dirname(__file__),'skinned_actor_model.py')):
        with open(path,'rb') as source:
            digest.update(source.read())
    digest.update(source_bytes)
    for body in (identity.encode(), kind.encode(), template, shader):
        digest.update(body)
    info.update(identity=identity, cache_key=digest.hexdigest(), converter_version=VERSION, shoreline=True, shoreline_material=kind, archive_bytes=len(packed), archive_sha256=hashlib.sha256(packed).hexdigest())
    info['triangles'] = sum(len(g[3]) // 3 for _, g in parts)
    os.makedirs(os.path.dirname(os.path.abspath(out)),exist_ok=True)
    for path, body in [(out,packed),(out+'.json',(json.dumps(info,sort_keys=True)+'\n').encode())]:
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=os.path.dirname(os.path.abspath(path)),delete=False) as target:
                temporary = target.name
                target.write(body)
            os.replace(temporary,path)
        finally:
            if temporary and os.path.exists(temporary):
                os.unlink(temporary)
    return info
