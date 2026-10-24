"""Original synthetic coast-template tests; no player resources required."""
import struct
import unittest

import actor_model
import shoreline_model as coast
import test_actor_model


def template(material_name=None):
    material_name = material_name or coast.MATERIAL
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
    dictionary, _ = coast.single_dictionary(f, material_name, material)
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
    f.set_rel(row+0x10,coast.append_string(f,material_name))
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

    def test_water_surface_fades_at_its_open_edges(self):
        # a 2x2 grid of quads: the eight border points are on open edges (alpha 0), the middle point is inside (1)
        pts = [(x * 100.0, 5.0, z * 100.0) for z in range(3) for x in range(3)]
        idx = []
        for z in range(2):
            for x in range(2):
                a = z * 3 + x
                idx += [a, a + 3, a + 1, a + 1, a + 3, a + 4]
        self.assertEqual(coast.edge_alpha(pts, idx), [0, 0, 0, 0, 1, 0, 0, 0, 0])
        original, _, _ = template(coast.MATERIALS['water'])
        geometry = (pts, [(0, 1, 0)] * 9, [(p[0] / 100, p[2] / 100) for p in pts], idx, b'')
        data = coast.build_bfres(original, geometry, 'mod_pond', coast.MATERIALS['water'])
        f = coast.base.Bfres(data)
        model = f.dict(f.rel(0x20))[0][1]
        self.assertEqual(f.dict(f.rel(model + 0x18))[0][0], coast.MATERIALS['water'])
        start = f.rel(f.rel(f.rel(f.dict(f.rel(model + 0x14))[0][1] + 0x20) + 0x18) + 0x14)
        self.assertEqual([struct.unpack_from('>4f', data, start + i * 64)[3] for i in range(9)], [0, 0, 0, 0, 1, 0, 0, 0, 0])
        # each kind's parts get the colours the game's own models of that material have: a waterfall Forest Haven's
        # (blue 1 at the top, 0.3 at the foot), a river's stream red 0.5 at its open edges, the pond's waves fading out
        fall = [(p[0], 200 - p[2], 0) for p in pts]
        colours = [tuple(round(v, 4) for v in c) for c in coast.part_colours('waterfall', 'surface', fall, idx)]
        self.assertEqual((colours[0], colours[4], colours[8]), ((0.2, 0.0, 1.0, 1.0), (0.2, 0.0, 0.65, 1.0), (0.2, 0.0, 0.3, 1.0)))
        self.assertEqual([c[0] for c in coast.part_colours('river', 'surface', pts, idx)], [0.5, 0.5, 0.5, 0.5, 0.0, 0.5, 0.5, 0.5, 0.5])
        self.assertEqual([c[3] for c in coast.part_colours('pond', 'waves', pts, idx)], [0, 0, 0, 0, 1, 0, 0, 0, 0])
        self.assertEqual(set(coast.part_colours('pond', 'edge', pts, idx)), {(1.0, 1.0, 1.0, 1.0)})
        # a part's colours are written as given
        data = coast.build_parts(template(coast.MATERIALS['waterfall'])[0], [(coast.MATERIALS['waterfall'], (fall,) + geometry[1:], coast.part_colours('waterfall', 'surface', fall, idx))], 'mod_fall')
        f = coast.base.Bfres(data)
        model = f.dict(f.rel(0x20))[0][1]
        start = f.rel(f.rel(f.rel(f.dict(f.rel(model + 0x14))[0][1] + 0x20) + 0x18) + 0x14)
        self.assertEqual(tuple(round(v, 4) for v in struct.unpack_from('>4f', data, start + 8 * 64)), (0.2, 0.0, 0.3, 1.0))
        # one template room per kind, every part's material named
        for kind, (room, parts) in coast.KINDS.items():
            self.assertEqual(coast.TEMPLATE_ROOMS[kind], room)
            self.assertTrue(set(parts) <= set(coast.ROLES) and 'surface' in parts)
        # the foam keeps full alpha; the water template needs its own material in the player's island
        with self.assertRaises(actor_model.BuildError):
            coast.build_bfres(template()[0], geometry, 'mod_pond', coast.MATERIALS['water'])

    @unittest.skipUnless(__import__('os').environ.get('WWHD_GAME_DIR'), 'set WWHD_GAME_DIR to read the templates from a game folder')
    def test_both_templates_from_the_players_game(self):
        import os
        pts = [(x * 100.0, 5.0, z * 100.0) for z in range(3) for x in range(3)]
        idx = [0, 3, 1, 1, 3, 4, 1, 4, 2, 2, 4, 5, 3, 6, 4, 4, 6, 7, 4, 7, 5, 5, 7, 8]
        geometry = (pts, [(0, 1, 0)] * 9, [(p[0] / 100, p[2] / 100) for p in pts], idx, b'')
        # foam: Outset's coast (one texture, normals); water: the spring pool's surface (three textures, no normals);
        # river and waterfall: the flowing water of Mother & Child Isles and Forest Haven (one texture each)
        for kind, ntex in (('foam', 1), ('water', 3), ('river', 1), ('waterfall', 1)):
            tpl, _ = coast.player_template(os.environ['WWHD_GAME_DIR'], kind)
            data = coast.build_bfres(tpl, geometry, 'mod_' + kind, coast.MATERIALS[kind])
            f = coast.base.Bfres(data)
            self.assertEqual(f.u16(0x52), ntex)
            self.assertEqual(f.dict(f.rel(0x34))[0][0], 'mod_' + kind)
            self.assertLess(len(data), 1024 * 1024)
            model = f.dict(f.rel(0x20))[0][1]
            vertex = f.rel(f.dict(f.rel(model + 0x14))[0][1] + 0x20)
            buffer = f.rel(vertex + 0x18)
            stride, start = f.u16(buffer + 0xC), f.rel(buffer + 0x14)
            alphas = [struct.unpack_from('>f', data, start + i * stride + 12)[0] for i in range(9)]
            self.assertEqual(alphas, [1] * 9 if kind in ('foam', 'waterfall') else [0, 0, 0, 0, 1, 0, 0, 0, 0])

    @unittest.skipUnless(__import__('os').environ.get('WWHD_GAME_DIR'), 'set WWHD_GAME_DIR to read the templates from a game folder')
    def test_lava_template_from_the_players_game(self):
        import os
        # lava: Fire Mountain's (the volcano object Ykzyg, its model qkzyg), with its own texture animation
        self.assertEqual(coast.template_model('lava'), 'qkzyg')
        pts = [(x * 100.0, 5.0, z * 100.0) for z in range(3) for x in range(3)]
        idx = [0, 3, 1, 1, 3, 4, 1, 4, 2, 2, 4, 5, 3, 6, 4, 4, 6, 7, 4, 7, 5, 5, 7, 8]
        geometry = (pts, [(0, 1, 0)] * 9, [(p[0] / 220, p[2] / 220) for p in pts], idx, b'')
        tpl, shader = coast.player_template(os.environ['WWHD_GAME_DIR'], 'lava')
        data = coast.build_parts(tpl, [(coast.MATERIALS['lava'], geometry, coast.part_colours('lava', 'surface', pts, idx))], 'mod_lava', 'qkzyg')
        f = coast.base.Bfres(data)
        self.assertEqual(f.u16(0x52), 3)  # its lava layers and grains
        self.assertEqual(f.dict(f.rel(0x34))[0][0], 'mod_lava')
        model = f.dict(f.rel(0x20))[0][1]
        self.assertEqual(f.dict(f.rel(model + 0x18))[0][0], 'Qkzyg_yougn_only_b_yougan_v')
        animation = f.dict(f.rel(0x34))[0][1]
        rows = f.rel(animation + 0x2C)
        self.assertEqual(f.cstr(f.rel(rows + 0x10)), 'Qkzyg_yougn_only_b_yougan_v')  # its scroll moves with it
        self.assertLess(len(data), 1024 * 1024)
        # the streams' mask is one value all over (the pool glows everywhere): our own constant, none of its picture
        textures = dict(f.dict(f.rel(0x24)))
        mask = textures['Txy_yougan2']
        image = f.rel(mask + 0xB0)
        size = f.u32(mask + 4 + 0x20)
        self.assertEqual(bytes(f.b[image:image + size]), bytes((255, 255, 0, 0, 0, 0, 0, 0)) * (size // 8))

    def test_missing_or_unsupported_player_template_refused(self):
        source,_,animation=template()
        f=coast.base.Bfres(source)
        f.put('I',animation+0x10,1001)
        with self.assertRaises(actor_model.BuildError):
            coast.build_bfres(bytes(f.b),self.geometry(),'mod_coast')
        with self.assertRaises(actor_model.BuildError):
            coast.build_bfres(source,self.geometry(),'../coast')
        geometry=list(self.geometry());geometry[3]=[]
        with self.assertRaises(actor_model.BuildError):
            coast.build_bfres(source,geometry,'mod_coast')


if __name__ == '__main__':
    unittest.main()
