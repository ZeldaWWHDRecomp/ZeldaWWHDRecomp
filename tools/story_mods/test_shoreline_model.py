"""Original synthetic coast-template tests; no player resources required."""
import struct
import unittest

import actor_model
import shoreline_model as coast
import test_actor_model


def template():
    f = coast.base.Bfres(test_actor_model.static_template())
    model, vertex, shape = 0x200, 0x300, 0x400
    dictionary, name_at = coast.single_dictionary(f, 'model1', model)
    f.set_rel(0x20, dictionary)
    f.set_rel(model + 4, name_at)
    material = f.append(b'FMAT' + bytes(72),4)
    f.put('BBHHHHH',material+0x10,2,2,4,0,80,80,0)
    info=f.append(struct.pack('>HHIi',1,0,0,4)+bytes(4),4)
    f.set_rel(info+4,coast.append_string(f,'draw_buffer_type'))
    f.set_rel(material+0x1C,coast.append_dictionary(f,[('draw_buffer_type',info)]))
    f.set_rel(material+0x20,f.append(bytes(range(48)),4))
    shader=f.append(bytes(28),4)
    f.set_rel(shader,coast.append_string(f,'original_shader'))
    f.set_rel(shader+4,coast.append_string(f,'original_coast'))
    f.set_rel(material+0x24,shader)
    texture_refs=f.append(bytes(16),4)
    for i in range(2):f.set_rel(texture_refs+8*i,coast.append_string(f,'Txa_umi_kiwa_01'))
    f.set_rel(material+0x28,texture_refs)
    samplers=f.append(bytes(48),4)
    f.set_rel(material+0x2C,samplers)
    f.set_rel(material+0x30,coast.append_dictionary(f,[('_a0',samplers),('_a1',samplers+24)]))
    params=f.append(bytes(80),4)
    names=['texmtx0','texmtx1','texpivot0','texpivot1']
    entries=[]
    for i,name in enumerate(names):
        f.set_rel(params+i*20+16,coast.append_string(f,name))
        entries.append((name,params+i*20))
    f.set_rel(material+0x34,params)
    f.set_rel(material+0x38,coast.append_dictionary(f,entries))
    f.set_rel(material+0x3C,f.append(bytes(80),4))
    dictionary, _ = coast.single_dictionary(f, coast.MATERIAL, material)
    f.set_rel(model + 0x18, dictionary)
    texture=f.append(b'FTEX'+bytes(188),4)
    f.put('I',texture+0x24,8)
    f.set_rel(texture+0xA8,coast.append_string(f,'Txa_umi_kiwa_01'))
    f.set_rel(texture+0xB0,f.append(bytes(range(8)),4))
    f.set_rel(0x24,coast.append_dictionary(f,[('Txa_umi_kiwa_01',texture)]))
    f.put('H', shape + 0xE, 0)
    f.set_rel(shape + 0x20, vertex)
    attrs = f.append(bytes(48),4)
    for i,(name,offset,fmt) in enumerate([('_p0',16,0x811),('_n0',32,0x811),('_c0',0,0x813),('_u0',48,0x80D)]):
        text = f.append(name.encode()+b'\0',4)
        f.set_rel(attrs+i*12,text)
        f.put('HI',attrs+i*12+6,offset,fmt)
    f.b[vertex+4] = 4
    f.set_rel(vertex+0x10,attrs)
    animation = f.append(b'FSHU'+bytes(60),4)
    f.put('I', animation+0x10,101)
    f.put('H', animation+0x14,1)
    row=f.append(bytes(32),4)
    f.put('HHH',row,4,1,1)
    f.set_rel(row+0x10,coast.append_string(f,coast.MATERIAL))
    param_infos=f.append(bytes(64),4)
    for i,name in enumerate(names):f.set_rel(param_infos+i*16+12,coast.append_string(f,name))
    f.set_rel(row+0x14,param_infos)
    curve=f.append(bytes(36),4)
    f.put('H',curve+2,2)
    f.set_rel(curve+28,f.append(struct.pack('>2f',0,100),4))
    f.set_rel(curve+32,f.append(struct.pack('>8f',1,0,0,0,2,0,0,0),4))
    f.set_rel(row+0x18,curve)
    f.set_rel(row+0x1C,f.append(bytes(8),4))
    f.set_rel(animation+0x2C,row)
    dictionary,_ = coast.single_dictionary(f,'model1',animation)
    f.set_rel(0x34,dictionary)
    return bytes(f.b), material, animation


class ShorelineTests(unittest.TestCase):
    def geometry(self):
        return ([(0,12,0),(200,12,0),(0,12,100)],[(0,1,0)]*3,[(0,0),(2,0),(0,1)],[0,1,2],b'original placeholder')

    def test_original_geometry_native_material_and_animation(self):
        original, material, animation = template()
        data = coast.build_bfres(original,self.geometry(),'mod_coast')
        f = coast.base.Bfres(data)
        model_name,model=f.dict(f.rel(0x20))[0];self.assertEqual(model_name,'mod_coast')
        animation_name,animation=f.dict(f.rel(0x34))[0];self.assertEqual(animation_name,'mod_coast')
        new_material=f.dict(f.rel(model+0x18))[0][1];self.assertEqual(data[f.rel(new_material+0x20):f.rel(new_material+0x20)+48],bytes(range(48)))
        self.assertEqual(f.cstr(f.rel(animation+4)),'mod_coast')
        self.assertEqual(f.rel(animation+0x24),model)
        self.assertEqual(f.u32(animation+0x10),101)
        texture=f.dict(f.rel(0x24))[0][1]
        refs=f.rel(new_material+0x28)
        self.assertEqual(f.rel(refs+4),texture)
        self.assertEqual(f.rel(refs+12),texture)
        self.assertEqual(bytes(f.b[f.rel(texture+0xB0):f.rel(texture+0xB0)+8]),bytes(range(8)))
        self.assertLess(len(data),1024*1024)
        self.assertEqual(f.u32(0xC),len(data))
        self.assertEqual(len(data)%4096,0)
        shape=f.dict(f.rel(model+0x14))[0][1]
        self.assertEqual(f.u16(shape+0xE),0)
        self.assertEqual(f.u16(shape+0x1A),1)
        vertex=f.rel(shape+0x20)
        self.assertEqual(f.u32(vertex+8),3)
        buffer=f.rel(vertex+0x18)
        self.assertEqual(f.u16(buffer+0xC),64)
        start=f.rel(buffer+0x14)
        for i in range(3):
            self.assertEqual(struct.unpack_from('>4f',data,start+i*64),(1,1,1,1))
            self.assertEqual(struct.unpack_from('>3f',data,start+i*64+16),tuple(self.geometry()[0][i]))
            self.assertEqual(struct.unpack_from('>2f',data,start+i*64+48),tuple(self.geometry()[2][i]))
        bone=f.rel(f.rel(model+0xC)+0x14)
        self.assertEqual(struct.unpack_from('>3f',data,bone+0x30),(0,0,0))

    def test_missing_or_unsupported_player_template_refused(self):
        source,_,animation=template()
        f=coast.base.Bfres(source)
        f.put('I',animation+0x10,102)
        with self.assertRaises(actor_model.BuildError):
            coast.build_bfres(bytes(f.b),self.geometry(),'mod_coast')
        with self.assertRaises(actor_model.BuildError):
            coast.build_bfres(source,self.geometry(),'../coast')
        geometry=list(self.geometry());geometry[3]=[]
        with self.assertRaises(actor_model.BuildError):
            coast.build_bfres(source,geometry,'mod_coast')


if __name__ == '__main__':
    unittest.main()
