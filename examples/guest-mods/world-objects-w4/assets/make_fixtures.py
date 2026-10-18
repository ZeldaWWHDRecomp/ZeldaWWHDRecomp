#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Reproduce Horizon Isle, its distant silhouette and opaque foam ribbon; no external assets or libraries."""
import json
import math
from pathlib import Path
import struct
import zlib


def make(kind="island"):
    vertices, faces = [], []
    def ring(x,z,y):
        start=len(vertices);vertices.extend(((-x,y,-z),(x,y,-z),(x,y,z),(-x,y,z)))
        return list(range(start,start+4))
    def quad(a,b,c,d,material):
        faces.extend(((a,b,c,material),(a,c,d,material)))
    bottom=ring(1200,1000,-900)
    beach=ring(1200,1000,320)
    grass=ring(700,550,520)
    summit=ring(180,150,4000)
    for i in range(4):
        j=(i+1)%4
        quad(beach[i],beach[j],bottom[j],bottom[i],0)
        quad(beach[i],grass[i],grass[j],beach[j],0)
        quad(grass[i],summit[i],summit[j],grass[j],1)
    quad(*bottom,0);quad(summit[0],summit[3],summit[2],summit[1],1)
    # A sloped wooden pier joins the shore at y320, rising to y450 offshore.
    start=len(vertices)
    vertices.extend(((-110,-100,-1900),(110,-100,-1900),(110,-100,-950),(-110,-100,-950),
                     (-110,450,-1900),(110,450,-1900),(110,320,-950),(-110,320,-950)))
    box=((0,1,2),(0,2,3),(4,6,5),(4,7,6),(0,5,1),(0,4,5),
         (1,6,2),(1,5,6),(2,7,3),(2,6,7),(3,4,0),(3,7,4))
    faces.extend(tuple(start+i for i in face)+(2,) for face in box)
    if kind == 'island-foam':
        vertices=[];faces=[]
        inner=ring(1205,1005,12);outer=ring(1680,1480,12)
        for i in range(4):
            j=(i+1)%4
            quad(inner[i],inner[j],outer[j],outer[i],0)
    elif kind == 'island-lod':
        # The matching mountain silhouette omits the narrow pier and sea bottom.
        faces=[f for f in faces[:28] if not all(vertices[i][1]<0 for i in f[:3])]
    elif kind != 'island':
        raise ValueError(kind)
    positions,normals,uv=[],[],[]
    for face_index,(a,b,c,material) in enumerate(faces):
        p,q,r=vertices[a],vertices[b],vertices[c]
        u=[q[k]-p[k] for k in range(3)];v=[r[k]-p[k] for k in range(3)]
        normal=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]);length=math.sqrt(sum(x*x for x in normal))
        positions.extend(tuple(x/100 for x in point) for point in (p,q,r));normals.extend([tuple(x/length for x in normal)]*3)
        left=(material*20+1)/64;right=(material*20+18)/64
        if kind == 'island-foam':
            edge=face_index//2
            start,end=vertices[edge],vertices[(edge+1)%4]
            length=math.hypot(end[0]-start[0],end[2]-start[2])/1000
            corners={edge:(0,0),(edge+1)%4:(length,0),4+edge:(0,1),4+(edge+1)%4:(length,1)}
            uv.extend(corners[index] for index in (a,b,c))
        else:
            uv.extend(((left,.1),(right,.1),(right,.9)))
    colors=((235,245,237),(220,237,233),(243,250,245)) if kind=="island-foam" else ((205,183,125),(64,133,67),(128,87,47))
    rows=b''.join(b'\0'+b''.join(bytes(tuple(min(255,c+(12 if (x//4+y//4)%2 else 0)) for c in colors[min(x//20,2)])+(255,)) for x in range(64)) for y in range(16))
    def chunk(kind,body):return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body)&0xffffffff)
    packed=b'\x78\x01\x01'+struct.pack('<HH',len(rows),len(rows)^0xffff)+rows+struct.pack('>I',zlib.adler32(rows)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',64,16,8,6,0,0,0))+chunk(b'IDAT',packed)+chunk(b'IEND',b'')
    binary=bytearray();views=[];accessors=[]
    def part(body):
        binary.extend(b'\0'*(-len(binary)%4));index=len(views);views.append({'buffer':0,'byteOffset':len(binary),'byteLength':len(body)});binary.extend(body);return index
    def floats(rows,width):
        vi=part(struct.pack('<%df'%(len(rows)*width),*(x for row in rows for x in row)))
        index=len(accessors);accessors.append({'bufferView':vi,'componentType':5126,'count':len(rows),'type':'VEC'+str(width)});return index
    def indices(values):
        vi=part(struct.pack('<%dH'%len(values),*values));index=len(accessors);accessors.append({'bufferView':vi,'componentType':5123,'count':len(values),'type':'SCALAR'});return index
    pos=floats(positions,3);normal=floats(normals,3);texcoord=floats(uv,2);ind=indices(list(range(len(positions))));image=part(png)
    colpos=floats([tuple(x/100 for x in point) for point in vertices],3)
    primitives=[]
    for material in range(3):
        ix=indices([i for face in faces if face[3]==material for i in face[:3]])
        primitives.append({'attributes':{'POSITION':colpos},'indices':ix,'material':material+1})
    doc={'asset':{'version':'2.0','generator':'Original island example, MPL-2.0'},'scene':0,'scenes':[{'nodes':[0,1]}],
         'nodes':[{'mesh':0,'name':'island'},{'mesh':1,'name':'island_col'}],
         'meshes':[{'name':'island','primitives':[{'attributes':{'POSITION':pos,'NORMAL':normal,'TEXCOORD_0':texcoord},'indices':ind,'material':0}]},{'name':'island_col','primitives':primitives}],
         'buffers':[{'byteLength':len(binary)}],'bufferViews':views,'accessors':accessors,
         'materials':[{'pbrMetallicRoughness':{'baseColorTexture':{'index':0},'metallicFactor':0,'roughnessFactor':1}},{'name':'col_sand'},{'name':'col_grass'},{'name':'col_wood'}],
         'textures':[{'source':0}],'images':[{'bufferView':image,'mimeType':'image/png'}]}
    if kind!='island':
        doc['nodes']=doc['nodes'][:1];doc['scenes'][0]['nodes']=[0]
        doc['meshes']=doc['meshes'][:1];doc['materials']=doc['materials'][:1]
    text=json.dumps(doc,separators=(',',':')).encode();text+=b' '*(-len(text)%4);binary.extend(b'\0'*(-len(binary)%4))
    body=struct.pack('<I4s',len(text),b'JSON')+text+struct.pack('<I4s',len(binary),b'BIN\0')+binary
    return b'glTF'+struct.pack('<II',2,len(body)+12)+body


if __name__=='__main__':
    for kind in ('island','island-lod','island-foam'):
        Path(__file__).with_name(kind+'.glb').write_bytes(make(kind))
