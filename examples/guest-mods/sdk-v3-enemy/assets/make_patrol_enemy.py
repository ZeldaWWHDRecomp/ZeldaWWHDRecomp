#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Original MPL-2.0 patrol enemy, adapted from Lukas S coral wanderer."""
import json
import math
import pathlib
import struct
import zlib


def make():
    positions, normals, uv, indices, joints, weights = [], [], [], [], [], []
    def box(lo, hi, influence):
        x,y,z=lo;X,Y,Z=hi
        faces=[([(x,y,Z),(X,y,Z),(X,Y,Z),(x,Y,Z)],(0,0,1)), ([(X,y,z),(x,y,z),(x,Y,z),(X,Y,z)],(0,0,-1)), ([(X,y,Z),(X,y,z),(X,Y,z),(X,Y,Z)],(1,0,0)), ([(x,y,z),(x,y,Z),(x,Y,Z),(x,Y,z)],(-1,0,0)), ([(x,Y,Z),(X,Y,Z),(X,Y,z),(x,Y,z)],(0,1,0)), ([(x,y,z),(X,y,z),(X,y,Z),(x,y,Z)],(0,-1,0))]
        for vertices,normal in faces:
            at=len(positions)
            for p in vertices:
                positions.append(p);normals.append(normal);uv.append(((p[0]+.4)/.8,1-p[1]/1.8))
                j,w=influence(p);joints.append((*j,0,0));weights.append((*w,0.,0.))
            indices.extend(at+i for i in (0,1,2,0,2,3))
    # Floating geometric torso, little feet and a broad lantern-shaped head.
    box((-.24,.30,-.16),(.24,1.05,.16),lambda p:((0,1),(.2+.6*(1.05-p[1])/.75,.2+.6*(p[1]-.30)/.75)))
    box((-.33,1.05,-.22),(.33,1.58,.22),lambda p:((1,2),(max(0.,(1.58-p[1])/.53),min(1.,(p[1]-1.05)/.53))))
    box((-.23,.04,-.14),(-.06,.30,.20),lambda p:((0,1),(1.,0.)))
    box((.06,.04,-.14),(.23,.30,.20),lambda p:((0,1),(1.,0.)))
    box((-.10,1.58,-.10),(.10,1.78,.10),lambda p:((2,1),(1.,0.)))
    def chunk(kind,body):return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body)&0xffffffff)
    rgba=bytearray()
    for y in range(128):
        for x in range(128):
            eye=(28<x<46 or 82<x<100) and 28<y<43
            stripe=76<y<85
            rgba.extend((20,34,52,255) if eye else ((245,194,74,255) if stripe else (68+(x//16)%2*15,142,154+(y//16)%2*12,255)))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',128,128,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(b'\0'+rgba[y*512:(y+1)*512] for y in range(128))))+chunk(b'IEND',b'')
    binary=bytearray();views=[];accessors=[]
    def add(values,width,shape,ctype=5126):
        code={5126:'f',5123:'H',5121:'B'}[ctype];flat=[v for row in values for v in row] if width>1 else values
        body=struct.pack('<'+str(len(flat))+code,*flat);binary.extend(b'\0'*(-len(binary)%4));view=len(views);views.append(dict(buffer=0,byteOffset=len(binary),byteLength=len(body)));binary.extend(body);index=len(accessors);accessors.append(dict(bufferView=view,componentType=ctype,count=len(values),type=shape));return index
    attrs={key:add(data,width,shape,ctype) for key,data,width,shape,ctype in [('POSITION',positions,3,'VEC3',5126),('NORMAL',normals,3,'VEC3',5126),('TEXCOORD_0',uv,2,'VEC2',5126),('JOINTS_0',joints,4,'VEC4',5121),('WEIGHTS_0',weights,4,'VEC4',5126)]}
    triangles=add(indices,1,'SCALAR',5123)
    inverse=[]
    for y in [0.,.30,1.05]:inverse.append([1,0,0,0,0,1,0,0,0,0,1,0,0,-y,0,1])
    binds=add(inverse,16,'MAT4')
    animations=[]
    for name,bone,axis,angle in [('idle',3,1,.12),('walk',2,2,.14),('flinch',3,0,-.45),('stun',3,2,.55),('death',2,0,math.pi/2)]:
        times=add([0.,.5,1.],1,'SCALAR');qs=[]
        for a in ([0.,angle,angle] if name=='death' else [0.,angle,0.]):
            q=[0.,0.,0.,math.cos(a/2)];q[axis]=math.sin(a/2);qs.append(q)
        values=add(qs,4,'VEC4');animations.append(dict(name=name,samplers=[dict(input=times,output=values,interpolation='LINEAR')],channels=[dict(sampler=0,target=dict(node=bone,path='rotation'))]))
    binary.extend(b'\0'*(-len(binary)%4));imageview=len(views);views.append(dict(buffer=0,byteOffset=len(binary),byteLength=len(png)));binary.extend(png)
    doc=dict(asset=dict(version='2.0',generator='Original MPL-2.0 patrol enemy by Lukas S'),scene=0,scenes=[dict(nodes=[0,1])],nodes=[dict(name='sprite_mesh',mesh=0,skin=0),dict(name='root',children=[2]),dict(name='body',translation=[0,.30,0],children=[3]),dict(name='head',translation=[0,.75,0])],skins=[dict(joints=[1,2,3],skeleton=1,inverseBindMatrices=binds)],animations=animations,buffers=[dict(byteLength=len(binary))],bufferViews=views,accessors=accessors,meshes=[dict(primitives=[dict(attributes=attrs,indices=triangles,material=0)])],materials=[dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=0),metallicFactor=0,roughnessFactor=1))],textures=[dict(source=0)],images=[dict(bufferView=imageview,mimeType='image/png')])
    text=json.dumps(doc,separators=(',',':')).encode();text+=b' '*(-len(text)%4);binary.extend(b'\0'*(-len(binary)%4));body=struct.pack('<I4s',len(text),b'JSON')+text+struct.pack('<I4s',len(binary),b'BIN\0')+binary
    return b'glTF'+struct.pack('<II',2,len(body)+12)+body

if __name__=='__main__':pathlib.Path(__file__).with_name('patrol_enemy.glb').write_bytes(make())
