#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Reproduce original floating models and authored collision; no external assets."""
import json
import math
from pathlib import Path
import struct
import zlib

FACES=((0,1,2),(0,2,3),(4,6,5),(4,7,6),(0,5,1),(0,4,5),
       (1,6,2),(1,5,6),(2,7,3),(2,6,7),(3,4,0),(3,7,4))


def box(x,z,bottom,top):
    points=[(-x,bottom,-z),(x,bottom,-z),(x,bottom,z),(-x,bottom,z),
            (-x,top,-z),(x,top,-z),(x,top,z),(-x,top,z)]
    return points,list(FACES)


def buoy():
    points=[];faces=[]
    for y in (-250,250):
        for k in range(8):
            angle=2*math.pi*k/8;points.append((70*math.cos(angle),y,70*math.sin(angle)))
    points.extend(((0,-250,0),(0,250,0)))
    for k in range(8):
        n=(k+1)%8
        faces.extend(((k,k+8,n),(n,k+8,n+8),(16,k,n),(17,n+8,k+8)))
    return points,faces


def geometry(name):
    if name=='buoy':return buoy(),None,'col_metal'
    if name=='raft':return box(250,350,-50,0),box(250,350,-350,200),'col_wood'
    if name=='flotsam':return box(100,180,-40,0),box(100,180,-250,200),'col_wood'
    raise ValueError(name)


def make(name):
    (points,faces),envelope,surface=geometry(name)
    pos,normal,uv=[],[],[]
    for face in faces:
        a,b,c=[points[i] for i in face];u=[b[k]-a[k] for k in range(3)];v=[c[k]-a[k] for k in range(3)]
        n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]);length=math.sqrt(sum(x*x for x in n))
        pos.extend(tuple(x/100 for x in point) for point in (a,b,c));normal.extend([tuple(x/length for x in n)]*3);uv.extend(((0,0),(1,0),(1,1)))
    color={'raft':(135,92,48),'buoy':(218,102,35),'flotsam':(111,80,47)}[name]
    rows=b''.join(b'\0'+b''.join(bytes(tuple(min(255,c+(20 if (x//4+y//4)%2 else 0)) for c in color)+(255,)) for x in range(64)) for y in range(16))
    def chunk(kind,body):return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body)&0xffffffff)
    packed=b'\x78\x01\x01'+struct.pack('<HH',len(rows),len(rows)^0xffff)+rows+struct.pack('>I',zlib.adler32(rows)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',64,16,8,6,0,0,0))+chunk(b'IDAT',packed)+chunk(b'IEND',b'')
    binary=bytearray();views=[];accessors=[]
    def part(data):
        binary.extend(b'\0'*(-len(binary)%4));index=len(views);views.append({'buffer':0,'byteOffset':len(binary),'byteLength':len(data)});binary.extend(data);return index
    def floats(rows,width):
        vi=part(struct.pack('<%df'%(len(rows)*width),*(x for row in rows for x in row)));index=len(accessors)
        accessors.append({'bufferView':vi,'componentType':5126,'count':len(rows),'type':'VEC'+str(width)});return index
    def indices(values):
        vi=part(struct.pack('<%dH'%len(values),*values));index=len(accessors)
        accessors.append({'bufferView':vi,'componentType':5123,'count':len(values),'type':'SCALAR'});return index
    rp=floats(pos,3);rn=floats(normal,3);ru=floats(uv,2);ri=indices(list(range(len(pos))));image=part(png)
    cp=floats([tuple(x/100 for x in point) for point in points],3);ci=indices([i for face in faces for i in face])
    primitives=[{'attributes':{'POSITION':cp},'indices':ci,'material':1}]
    materials=[{'pbrMetallicRoughness':{'baseColorTexture':{'index':0},'metallicFactor':0,'roughnessFactor':1}},{'name':surface}]
    if envelope:
        points,faces=envelope
        ep=floats([tuple(x/100 for x in point) for point in points],3)
        # Side walls only: the object envelope never supplies elevated ground.
        ei=indices([i for face in faces[4:] for i in face])
        primitives.append({'attributes':{'POSITION':ep},'indices':ei,'material':2})
        materials.append({'name':'col_objects'})
    doc={'asset':{'version':'2.0','generator':'Original floating example, MPL-2.0'},'scene':0,'scenes':[{'nodes':[0,1]}],
         'nodes':[{'mesh':0,'name':name},{'mesh':1,'name':name+'_col'}],
         'meshes':[{'primitives':[{'attributes':{'POSITION':rp,'NORMAL':rn,'TEXCOORD_0':ru},'indices':ri,'material':0}]},{'name':name+'_col','primitives':primitives}],
         'buffers':[{'byteLength':len(binary)}],'bufferViews':views,'accessors':accessors,'materials':materials,'textures':[{'source':0}],'images':[{'bufferView':image,'mimeType':'image/png'}]}
    text=json.dumps(doc,separators=(',',':')).encode();text+=b' '*(-len(text)%4);binary.extend(b'\0'*(-len(binary)%4))
    body=struct.pack('<I4s',len(text),b'JSON')+text+struct.pack('<I4s',len(binary),b'BIN\0')+binary
    return b'glTF'+struct.pack('<II',2,len(body)+12)+body


if __name__=='__main__':
    for name in ('raft','buoy','flotsam'):Path(__file__).with_name(name+'.glb').write_bytes(make(name))
