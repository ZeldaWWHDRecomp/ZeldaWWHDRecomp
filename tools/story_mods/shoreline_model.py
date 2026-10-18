#!/usr/bin/env python3
"""Player-local coast material and animation on original mod geometry.

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

import notebook_model as base
from skinned_actor_model import append_dictionary, append_string

VERSION = 1
MATERIAL = 'SC_01_mizu_v_x'


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


def build_bfres(template, geometry, identity):
    require(re.fullmatch(r'[a-z][a-z0-9_]{0,30}', identity) is not None, 'invalid coast identity')
    f = base.Bfres(template)
    models = dict(f.dict(f.rel(0x20)))
    require('model1' in models, 'player coast model unavailable')
    model = models['model1']
    materials = f.dict(f.rel(model + 0x18))
    require(MATERIAL in dict(materials), 'player coast material unavailable')
    material_index = [name for name, _ in materials].index(MATERIAL)
    shapes = f.dict(f.rel(model + 0x14))
    selected = [at for _, at in shapes if f.u16(at + 0xE) == material_index]
    require(len(selected) == 1, 'ambiguous player coast shape')
    shape = selected[0]
    vertex = f.rel(shape + 0x20)
    attrs = f.rel(vertex + 0x10)
    layout = {f.cstr(f.rel(attrs + i*12)): (f.b[attrs+i*12+4], f.u16(attrs+i*12+6), f.u32(attrs+i*12+8)) for i in range(f.b[vertex+4])}
    require(layout == {'_p0': (0,16,0x811), '_n0': (0,32,0x811), '_c0': (0,0,0x813), '_u0': (0,48,0x80D)} and f.b[vertex+5] == 1, 'unsupported player coast vertex layout')
    positions, normals, uv, indices, _ = geometry
    require(0 < len(indices)//3 <= 1024, 'coast must have 1..1024 triangles')
    require(len(positions) == len(normals) == len(uv), 'coast attribute counts differ')
    require(all(math.isfinite(v) for rows in (positions, normals, uv) for row in rows for v in row), 'non-finite coast geometry')
    # Replace every reachable geometry entry. Only the original material and
    # texture animation remain reachable; no island shape is instantiated.
    dictionary, name_at = single_dictionary(f, identity, model)
    f.set_rel(0x20, dictionary)
    f.put('H', 0x50, 1)
    f.set_rel(model + 4, name_at)
    shape_dictionary, _ = single_dictionary(f, 'coast', shape)
    f.set_rel(model + 0x14, shape_dictionary)
    f.put('HH', model + 0x20, 1, 1)
    f.put('I', model + 0x28, len(positions))
    f.set_rel(model + 0x10, vertex)
    f.put('H', vertex + 6, 0)
    f.put('I', vertex + 8, len(positions))
    f.b[vertex + 0xC] = 0
    vertex_bytes = b''.join(struct.pack('>4f3f4x3f4x2f8x', 1,1,1,1, *p, *n, *t) for p,n,t in zip(positions,normals,uv))
    buffer = f.rel(vertex + 0x18)
    f.put('I', buffer + 4, len(vertex_bytes))
    f.put('H', buffer + 0xC, 64)
    f.set_rel(buffer + 0x14, f.append(vertex_bytes, 0x1000))
    f.put('HHHH', shape + 0xC, 0, material_index, 0, 0)
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
    skeleton = f.rel(model + 0xC)
    bones = f.rel(skeleton + 0x14)
    for i in range(f.u16(skeleton + 8)):
        bone = bones + 64*i
        f.put('3f', bone + 0x14, 1,1,1)
        f.put('4f', bone + 0x20, 0,0,0,1)
        f.put('3f', bone + 0x30, 0,0,0)
    animation_entries = f.dict(f.rel(0x34))
    require(len(animation_entries) == 1 and animation_entries[0][0] == 'model1', 'player coast animation unavailable')
    animation = animation_entries[0][1]
    require(f.b[animation:animation+4] == b'FSHU' and f.u32(animation + 0x10) == 101, 'unsupported player coast animation')
    animation_dictionary, _ = single_dictionary(f, identity, animation)
    f.set_rel(0x34, animation_dictionary)
    f.set_rel(animation + 4, name_at)
    f.set_rel(animation + 0x24, model)
    f.append(b'',0x1000)
    f.put('I', 0xC, len(f.b))
    return compact(bytes(f.b), identity)


def compact(data, identity):
    """Keep only the coast's reachable resource graph, within the model budget."""
    source = base.Bfres(data)
    out = base.Bfres(data[:0x70])
    out.b[0x18:0x68] = bytes(0x50)

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

    model = source.dict(source.rel(0x20))[0][1]
    dst_model = block(model,0x30)
    out.set_rel(dst_model+4,append_string(out,identity))
    out.put('i',dst_model+8,0)
    out.put('i',dst_model+0x1C,0)
    out.put('H',dst_model+0x26,0)
    out.put('H',dst_model+0x24,1)
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
    shape = source.dict(source.rel(model+0x14))[0][1]
    dst_shape = block(shape,0x40)
    out.set_rel(dst_shape+4,append_string(out,'coast'))
    out.put('H',dst_shape+0xE,0)
    out.put('i',dst_shape+0x28,0)
    out.put('i',dst_shape+0x2C,0)
    out.put('I',dst_shape+0x3C,0)
    for offset,size in [(0x30,12),(0x34,24),(0x38,2)]:
        pointer(dst_shape,shape,offset,size)
    vertex = source.rel(shape+0x20)
    dst_vertex = block(vertex,0x20)
    attributes = source.rel(vertex+0x10)
    dst_attributes = block(attributes,48)
    attr_entries = []
    for i in range(4):
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
    out.set_rel(dst_model+0x10,dst_vertex)
    out.set_rel(dst_shape+0x20,dst_vertex)
    mesh = source.rel(shape+0x24)
    dst_mesh = block(mesh,28)
    pointer(dst_mesh,mesh,0x10,8)
    buffer = source.rel(mesh+0x14)
    dst_buffer = block(buffer,24)
    out.set_rel(dst_buffer+0x14,block(source.rel(buffer+0x14),source.u32(buffer+4),0x1000))
    out.set_rel(dst_mesh+0x14,dst_buffer)
    out.set_rel(dst_shape+0x24,dst_mesh)
    out.set_rel(dst_model+0x14,append_dictionary(out,[('coast',dst_shape)]))
    material = dict(source.dict(source.rel(model+0x18)))[MATERIAL]
    dst_material = block(material,0x4C)
    out.set_rel(dst_material+4,append_string(out,MATERIAL))
    out.put('H',dst_material+0xC,0)
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
    require(data[material+0x11]==2,'unsupported coast samplers')
    dst_textures=block(textures,16)
    for i in range(2):
        out.set_rel(dst_textures+8*i,text(source.rel(textures+8*i)))
        out.put('I',dst_textures+8*i+4,0)
    out.set_rel(dst_material+0x28,dst_textures)
    samplers=source.rel(material+0x2C)
    dst_samplers=block(samplers,48)
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
    out.set_rel(dst_model+0x18,append_dictionary(out,[(MATERIAL,dst_material)]))
    texture=dict(source.dict(source.rel(0x24)))['Txa_umi_kiwa_01']
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
    for i in range(2):
        out.set_rel(dst_textures+8*i+4,dst_texture)
    out.set_rel(0x24,append_dictionary(out,[('Txa_umi_kiwa_01',dst_texture)]))
    out.put('H',0x52,1)
    animation=source.dict(source.rel(0x34))[0][1]
    dst_animation=block(animation,0x34)
    out.set_rel(dst_animation+4,append_string(out,identity))
    out.put('i',dst_animation+8,0)
    out.put('H',dst_animation+0x14,1)
    out.put('H',dst_animation+0x16,0)
    out.set_rel(dst_animation+0x24,dst_model)
    out.set_rel(dst_animation+0x28,out.append(struct.pack('>H',0xFFFF),4))
    out.put('i',dst_animation+0x30,0)
    rows=source.rel(animation+0x2C)
    matches=[rows+i*32 for i in range(source.u16(animation+0x14)) if source.cstr(source.rel(rows+i*32+0x10))==MATERIAL]
    require(len(matches)==1,'player coast animation binding unavailable')
    row=matches[0]
    dst_row=block(row,32)
    out.put('II',dst_row+8,0,0)
    out.set_rel(dst_row+0x10,append_string(out,MATERIAL))
    param_count,curve_count,constant_count=source.u16(row),source.u16(row+2),source.u16(row+4)
    out.put('I',dst_animation+0x18,param_count*4)
    out.put('I',dst_animation+0x1C,curve_count)
    out.put('I',dst_animation+0x20,0)
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
    out.set_rel(dst_animation+0x2C,dst_row)
    out.set_rel(0x34,append_dictionary(out,[(identity,dst_animation)]))
    out.put('H',0x5A,1)
    out.set_rel(0x14,append_string(out,identity))
    out.append(b'',0x1000)
    out.put('I',0xC,len(out.b))
    require(len(out.b)<1024*1024,'coast resource exceeds its budget')
    return bytes(out.b)


def player_template(game):
    path = os.path.join(game, 'content/Common/Pack/szs_permanent2.pack')
    with open(path,'rb') as source:
        packed = dict(base.shaderprep.sarc(source.read()))
    require('sea_Room44.szs' in packed, 'player Outset coast unavailable')
    files = dict(base.shaderprep.sarc(base.shaderprep.yaz0(packed['sea_Room44.szs'])))
    return files['Room44.bfres'], files['model1.sharcfb']


def build(game, glb_path, identity, out):
    import collision_mesh
    with open(glb_path,'rb') as source:
        source_bytes=source.read(8*1024*1024+1)
    require(len(source_bytes)<=8*1024*1024,'coast GLB exceeds 8 MiB')
    with tempfile.TemporaryDirectory() as directory:
        snapshot=os.path.join(directory,'coast.glb')
        with open(snapshot,'wb') as target:
            target.write(source_bytes)
        geometry, info, _ = collision_mesh.read_static(snapshot)
    template, shader = player_template(game)
    result = build_bfres(template, geometry, identity)
    packed = base.yaz0_stored(base.sarc({identity+'.bfres':result,identity+'.sharcfb':shader}))
    digest = hashlib.sha256()
    for path in (__file__, collision_mesh.__file__, base.__file__, os.path.join(os.path.dirname(__file__),'actor_model.py'), os.path.join(os.path.dirname(__file__),'skinned_actor_model.py')):
        with open(path,'rb') as source:
            digest.update(source.read())
    digest.update(source_bytes)
    for body in (identity.encode(), template, shader):
        digest.update(body)
    info.update(identity=identity, cache_key=digest.hexdigest(), converter_version=VERSION, shoreline=True, archive_bytes=len(packed), archive_sha256=hashlib.sha256(packed).hexdigest())
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
