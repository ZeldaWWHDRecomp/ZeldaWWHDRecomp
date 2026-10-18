#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Generate an original interactive tower with an original painted atlas."""
import json
import math
from pathlib import Path
import struct
import zlib

FACES=((0,1,2),(0,2,3),(4,6,5),(4,7,6),(0,5,1),(0,4,5),
       (1,6,2),(1,5,6),(2,7,3),(2,6,7),(3,4,0),(3,7,4))

def box(lo,hi):
    x,y,z=lo;X,Y,Z=hi
    return [(x,y,z),(X,y,z),(X,y,Z),(x,y,Z),(x,Y,z),(X,Y,z),(X,Y,Z),(x,Y,Z)],list(FACES)

def quad(points):return points,[(0,1,2),(0,2,3)]

def make():
    # Outward winding; the front (-Z) face is reserved for vine climbing.
    platform=box((-800,-1000,-800),(800,0,800))
    tower=box((-300,0,-300),(300,1200,300))
    climb=quad([(-300,0,-300),(-300,1200,-300),(300,1200,-300),(300,0,-300)])
    patch=box((300,800,-80),(306,1000,80))
    beam=box((-200,770,-355),(200,830,-305))
    # The beam leaves clear climb lanes at x +/-250. A separate patch offers hookshot ground.
    render=[(platform,0),(tower,1),(patch,2),(beam,3)]
    collision=[(platform,'col_stone'),((tower[0],tower[1][:4]+tower[1][6:]),'col_stone'),
               (climb,'col_climb'),(patch,'col_hookshot'),(beam,'col_wood')]
    pos=[];normal=[];uv=[]
    for (points,faces),tile in render:
        for face in faces:
            a,b,c=[points[i] for i in face];u=[b[k]-a[k] for k in range(3)];v=[c[k]-a[k] for k in range(3)]
            n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]);length=math.sqrt(sum(x*x for x in n))
            pos.extend(tuple(x/100 for x in p) for p in (a,b,c));normal.extend([tuple(x/length for x in n)]*3)
            uv.extend((((tile*16+1)/64,1/16),((tile*16+15)/64,1/16),((tile*16+15)/64,15/16)))
    colors=((109,116,127),(90,143,65),(207,160,53),(132,85,42))
    rows=b''.join(b'\0'+b''.join(bytes(tuple(min(255,c+(16 if (x//4+y//4)%2 else 0)) for c in colors[x//16])+(255,)) for x in range(64)) for y in range(16))
    def chunk(kind,body):return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body)&0xffffffff)
    packed=b'\x78\x01\x01'+struct.pack('<HH',len(rows),len(rows)^0xffff)+rows+struct.pack('>I',zlib.adler32(rows)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',64,16,8,6,0,0,0))+chunk(b'IDAT',packed)+chunk(b'IEND',b'')
    binary=bytearray();views=[];accessors=[]
    def part(data):
        binary.extend(b'\0'*(-len(binary)%4));i=len(views);views.append({'buffer':0,'byteOffset':len(binary),'byteLength':len(data)});binary.extend(data);return i
    def floats(rows,width):
        vi=part(struct.pack('<%df'%(len(rows)*width),*(x for row in rows for x in row)));i=len(accessors)
        accessors.append({'bufferView':vi,'componentType':5126,'count':len(rows),'type':'VEC'+str(width)});return i
    def indices(values):
        vi=part(struct.pack('<%dH'%len(values),*values));i=len(accessors)
        accessors.append({'bufferView':vi,'componentType':5123,'count':len(values),'type':'SCALAR'});return i
    rp=floats(pos,3);rn=floats(normal,3);ru=floats(uv,2);ri=indices(list(range(len(pos))));image=part(png)
    materials=[{'name':'tower_atlas','pbrMetallicRoughness':{'baseColorTexture':{'index':0},'metallicFactor':0,'roughnessFactor':1}}]
    primitives=[]
    for (points,faces),surface in collision:
        names=[m.get('name') for m in materials]
        if surface not in names:materials.append({'name':surface});names.append(surface)
        cp=floats([tuple(x/100 for x in p) for p in points],3);ci=indices([i for f in faces for i in f])
        primitives.append({'attributes':{'POSITION':cp},'indices':ci,'material':names.index(surface)})
    doc={'asset':{'version':'2.0','generator':'Original interactive tower, MPL-2.0'},'scene':0,'scenes':[{'nodes':[0,1]}],
         'nodes':[{'mesh':0,'name':'tower'},{'mesh':1,'name':'tower_col'}],
         'meshes':[{'primitives':[{'attributes':{'POSITION':rp,'NORMAL':rn,'TEXCOORD_0':ru},'indices':ri,'material':0}]},{'name':'tower_col','primitives':primitives}],
         'buffers':[{'byteLength':len(binary)}],'bufferViews':views,'accessors':accessors,'materials':materials,'textures':[{'source':0}],'images':[{'bufferView':image,'mimeType':'image/png'}]}
    text=json.dumps(doc,separators=(',',':')).encode();text+=b' '*(-len(text)%4);binary.extend(b'\0'*(-len(binary)%4))
    body=struct.pack('<I4s',len(text),b'JSON')+text+struct.pack('<I4s',len(binary),b'BIN\0')+binary
    return b'glTF'+struct.pack('<II',2,len(body)+12)+body

if __name__=='__main__':Path(__file__).with_name('tower.glb').write_bytes(make())
