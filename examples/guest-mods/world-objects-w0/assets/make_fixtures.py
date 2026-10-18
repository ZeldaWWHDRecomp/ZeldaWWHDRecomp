#!/usr/bin/env python3
"""Original fixed W0 render models, in metres. Collision is handmade in the runtime."""
import json
import math
from pathlib import Path
import struct
import zlib

FACES = ((0,1,2),(0,2,3),(4,6,5),(4,7,6),(0,5,1),(0,4,5),
         (1,6,2),(1,5,6),(2,7,3),(2,6,7),(3,4,0),(3,7,4))

def prism(x, near, far, bottom, ntop, ftop):
    return [(-x,bottom,near),(x,bottom,near),(x,bottom,far),(-x,bottom,far),
            (-x,ntop,near),(x,ntop,near),(x,ftop,far),(-x,ftop,far)]

def geometry(name):
    if name == 'platform': return [prism(300,-300,300,-150,0,0),prism(150,-900,-300,-170,-150,0)]
    if name == 'buoy': return [prism(70,-70,70,-100,100,100)]
    if name == 'raft': return [prism(250,-350,350,-50,0,0)]
    raise ValueError(name)

def make(name):
    pos, normal, uv = [], [], []
    for vertices in geometry(name):
        for face in FACES:
            a,b,c = [vertices[i] for i in face]
            u,v = [b[i]-a[i] for i in range(3)], [c[i]-a[i] for i in range(3)]
            n = (u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
            length = math.sqrt(sum(x*x for x in n))
            pos.extend(tuple(x/100 for x in p) for p in (a,b,c)); normal.extend([tuple(x/length for x in n)]*3); uv.extend(((0,0),(1,0),(1,1)))
    colors = {'platform':(165,150,115),'buoy':(210,85,35),'raft':(125,85,45)}
    color = colors[name]
    rows = b''.join(b'\0'+b''.join(bytes(tuple(min(255,c+(20 if (x//4+y//4)%2 else 0)) for c in color)+(255,)) for x in range(64)) for y in range(16))
    def chunk(kind,data): return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    # Stored DEFLATE gives deterministic output across zlib implementations.
    packed = b'\x78\x01\x01'+struct.pack('<HH',len(rows),len(rows)^0xffff)+rows+struct.pack('>I',zlib.adler32(rows)&0xffffffff)
    png = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',64,16,8,6,0,0,0))+chunk(b'IDAT',packed)+chunk(b'IEND',b'')
    parts = [struct.pack('<%df'%(len(pos)*3),*(x for p in pos for x in p)),struct.pack('<%df'%(len(normal)*3),*(x for p in normal for x in p)),struct.pack('<%df'%(len(uv)*2),*(x for p in uv for x in p)),struct.pack('<%dH'%len(pos),*range(len(pos))),png]
    binary=bytearray();views=[]
    for part in parts:
        binary.extend(b'\0'*(-len(binary)%4));views.append({'buffer':0,'byteOffset':len(binary),'byteLength':len(part)});binary.extend(part)
    doc={'asset':{'version':'2.0','generator':'Original W0 proof geometry, MPL-2.0'},'scene':0,'scenes':[{'nodes':[0]}],'nodes':[{'mesh':0}],'buffers':[{'byteLength':len(binary)}],'bufferViews':views,
         'accessors':[{'bufferView':i,'componentType':5123 if i==3 else 5126,'count':len(pos),'type':('VEC3','VEC3','VEC2','SCALAR')[i]} for i in range(4)],
         'meshes':[{'primitives':[{'attributes':{'POSITION':0,'NORMAL':1,'TEXCOORD_0':2},'indices':3,'material':0}]}],'materials':[{'pbrMetallicRoughness':{'baseColorTexture':{'index':0},'metallicFactor':0,'roughnessFactor':1}}],'textures':[{'source':0}],'images':[{'bufferView':4,'mimeType':'image/png'}]}
    text=json.dumps(doc,separators=(',',':')).encode();text+=b' '*(-len(text)%4);binary.extend(b'\0'*(-len(binary)%4))
    body=struct.pack('<I4s',len(text),b'JSON')+text+struct.pack('<I4s',len(binary),b'BIN\0')+binary
    return b'glTF'+struct.pack('<II',2,len(body)+12)+body

if __name__ == '__main__':
    for name in ('platform','buoy','raft'): Path(__file__).with_name(name+'.glb').write_bytes(make(name))
