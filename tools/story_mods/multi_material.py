# SPDX-License-Identifier: MPL-2.0
"""Several materials for an original rigid mod model, from the player's own template materials.

actor_model.read_parts reads the GLB: up to 16 materials, each with one embedded PNG, and up to 32
triangle primitives. This module writes one BFRES model with one shape per primitive and one material
per glTF material. The template is a get-item model of the same family as the single-material
converter's (actor_model.py): one rigid model with an opaque material and an alpha-tested cut-out
material that share one shader archive and the same toon ramp. Every output material is a copy of one
of the two (its shader assignment, parameters, samplers and ramp) with its own colour texture. No game
data is in this file or in a mod: the template and its shader archive are read from the player's game
at conversion time and the output stays in the player's model cache.

Material kinds:
  opaque   a copy of the template's opaque material;
  mask     alpha-tested cut-out (glTF alphaMode MASK): a copy of the template's cut-out material (its
           shader writes the texture's alpha; the game's GX2 alpha test keeps alpha >= alphaCutoff),
           drawn in the opaque pass;
  doubleSided (either kind): the faces get a mirrored back copy with reversed winding and normals,
           so both sides are lit from their own side (the template shaders light back faces as if
           they faced away from the light; culling stays on).
Blended transparency is refused by the reader: the template family has no blended material, and
blending would need depth sorting the actor draw path doesn't do.
"""
import struct

import os
import sys
# Windows' embeddable Python runs isolated: the script's own folder is not on sys.path, so the
# sibling modules are found through it explicitly (issue #280).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notebook_model as base  # noqa: E402

BuildError = base.BuildError

# GX2 render state words in a BFRES 3.x material (FMAT +0x20, 12 words)
RS_FLAGS, RS_POLYGON, RS_DEPTH, RS_ALPHA, RS_ALPHA_REF = 0, 1, 2, 3, 4
MODE_OPAQUE, MODE_ALPHA_MASK = 1, 2
ALPHA_TEST_ENABLE, ALPHA_FUNC_GEQUAL = 8, 6


def require(ok, message):
    if not ok:
        raise BuildError(message)


def rgba_texture(w, template_texture, name, width, height, rgba):
    """A copy of the template's colour FTEX header holding `rgba` as one linear sRGB RGBA8 level."""
    from skinned_actor_model import append_string
    out = w.out
    dst = w.block(template_texture, 0xC0)
    out.set_rel(dst + 0xA8, append_string(out, name))
    out.put('i', dst + 0xAC, 0)
    at = out.append(rgba, 0x2000)
    s = dst + 4
    for offset, value in ((0x04, width), (0x08, height), (0x10, 1), (0x14, base.FMT_RGBA8_SRGB), (0x20, len(rgba)),
                          (0x28, 0), (0x30, base.TILE_LINEAR_ALIGNED), (0x34, 0), (0x38, 0x100), (0x3C, width)):
        out.put('I', s + offset, value)
    for k in range(13):
        out.put('I', s + 0x40 + 4 * k, 0)
    out.put('I', dst + 0x7C, 1)
    for k in range(5):
        out.put('I', dst + 0x8C + 4 * k, 0)
    out.set_rel(dst + 0xB0, at)
    out.put('i', dst + 0xB4, 0)
    out.put('II', dst + 0xB8, 0, 0)
    return dst


ARCHIVE = 'content/Common/Object/Vobj03.szs'
BFRES, SHADERS = 'Vobj03.bfres', 'vobj03.sharcfb'
OPAQUE, MASK = 'Vobj03', 'HATA_f03'  # the template's opaque and cut-out materials
LAYOUT = [('_p0', 0, 0, 0x811), ('_n0', 0, 16, 0x811), ('_u0', 0, 32, 0x80D)]


def vertex_layout(src, fvtx):
    attrs = src.rel(fvtx + 0x10)
    return [(src.cstr(src.rel(attrs + 12 * a)), src.b[attrs + 12 * a + 4], src.u16(attrs + 12 * a + 6), src.u32(attrs + 12 * a + 8))
            for a in range(src.b[fvtx + 4])]


def check_template(src, opaque, mask):
    """The template model, its opaque and cut-out materials and a rigid shape with the float layout."""
    models = src.dict(src.rel(0x20))
    require(len(models) == 1, 'unexpected model count in the template')
    fm = models[0][1]
    skeleton = src.rel(fm + 0x0C)
    require(skeleton > 0 and src.b[skeleton:skeleton + 4] == b'FSKL' and (src.u16(skeleton + 8), src.u16(skeleton + 10), src.u16(skeleton + 12)) == (1, 0, 0),
            'template must have one root and no skin matrix palette')
    materials = src.dict(src.rel(fm + 0x18))
    by_name = dict(materials)
    require(opaque in by_name and mask in by_name, 'the template has no opaque and cut-out material')
    textures = dict(src.dict(src.rel(0x24)))
    kinds = {}
    for kind, name in (('opaque', opaque), ('mask', mask)):
        material = by_name[name]
        refs = src.rel(material + 0x28)
        names = [src.cstr(src.rel(refs + 8 * i)) for i in range(src.b[material + 0x10])]
        require(names and all(n in textures for n in names), 'the template material names a missing texture')
        state = struct.unpack_from('>12I', src.b, src.rel(material + 0x20))
        require((state[RS_FLAGS] & 3) == (MODE_OPAQUE if kind == 'opaque' else MODE_ALPHA_MASK), 'unexpected template material kind')
        kinds[kind] = (material, names)
    shape = None
    for _, at in src.dict(src.rel(fm + 0x14)):
        fvtx = src.rel(fm + 0x10) + 0x20 * src.u16(at + 0x12)
        if (src.u16(at + 0xE) == [n for n, _ in materials].index(opaque) and vertex_layout(src, fvtx) == LAYOUT and src.b[fvtx + 5] == 1 and
                src.u16(src.rel(fvtx + 0x18) + 0xC) == 48 and src.b[at + 0x16] == 0 and src.b[at + 0x17] == 1 and src.u32(src.rel(at + 0x24)) == 4):
            shape = (at, fvtx)
            break
    require(shape is not None, 'unexpected template mesh (one rigid triangle list with float position, normal and UV expected)')
    return fm, shape[1], shape[0], kinds, textures


def drawn_parts(model):
    """The parts as drawn: a double-sided material's part gets a mirrored back copy (reversed winding,
    negated normals) appended to its own vertex and index lists."""
    out = []
    for part in model.parts:
        if model.materials[part.material].double_sided:
            n = len(part.pos)
            back = [i + n for t in range(0, len(part.indices), 3) for i in (part.indices[t], part.indices[t + 2], part.indices[t + 1])]
            part = type(part)(part.pos + part.pos, part.nrm + [tuple(-v for v in x) for x in part.nrm], part.uv + part.uv,
                              part.indices + back, part.material)
        out.append(part)
    return out


def build_bfres(template, model, identity, opaque=OPAQUE, mask=MASK):
    """BFRES 3.4.0.1 with one model `identity`: model.parts -> shapes, model.materials -> materials."""
    import game_piece_model as gp
    from skinned_actor_model import append_dictionary, append_string
    require(1 <= len(model.parts) and 1 <= len(model.materials) and 1 <= len(model.images), 'empty model')
    out = base.Bfres(template[:0x70])
    out.b[0x18:0x68] = bytes(0x50)  # no string table, no other resource groups
    w = gp.Writer(template, out)
    texture_at, texture_names, colour = write_textures(w, model, opaque, mask)
    out.set_rel(0x24, append_dictionary(out, [(t, texture_at[t]) for t in texture_names]))
    out.put('H', 0x52, len(texture_names))
    dst_model = write_model(w, model, identity, texture_at, colour, opaque, mask)
    out.set_rel(0x20, append_dictionary(out, [(identity, dst_model)]))
    out.put('H', 0x50, 1)
    out.set_rel(0x14, append_string(out, identity))
    out.append(b'', 0x1000)
    out.put('I', 0xC, len(out.b))
    result = bytes(out.b)
    validate_bfres(result, identity, model)
    return result


def write_textures(w, model, opaque=OPAQUE, mask=MASK):
    """The model's textures into w.out (w: a game_piece_model.Writer of the template): (texture name -> offset,
    names in order, the colour texture name of each image)."""
    import game_piece_model as gp
    src = w.src
    fm, fvtx, shape, kinds, all_textures = check_template(src, opaque, mask)
    # textures: one per image, plus the template materials' other textures (the toon ramp) once
    texture_at, texture_names = {}, []
    used_kinds = sorted({m.kind for m in model.materials})
    for kind in used_kinds:
        for t in kinds[kind][1][1:]:
            if t not in texture_at:
                texture_at[t], _ = gp.copy_texture(w, all_textures[t])
                texture_names.append(t)
    colour_template = all_textures[kinds['opaque'][1][0]]
    colour = []
    for i, png in enumerate(model.images):
        width, height, rgba = base.decode_png(png)
        name = 'mm_colour%02d' % i
        texture_at[name] = rgba_texture(w, colour_template, name, width, height, rgba)
        texture_names.append(name)
        colour.append(name)
    return texture_at, texture_names, colour


def write_model(w, model, identity, texture_at, colour, opaque=OPAQUE, mask=MASK):
    """The model `identity` (its skeleton, materials and shapes) into w.out; its textures are at texture_at
    (write_textures). Returns the model's offset."""
    import game_piece_model as gp
    from skinned_actor_model import append_dictionary, append_string
    src, out, template = w.src, w.out, w.data
    fm, fvtx, shape, kinds, all_textures = check_template(src, opaque, mask)

    # the model and its one-root skeleton (identity rest pose: the authored GLB frame)
    dst_model = w.block(fm, 0x30)
    out.set_rel(dst_model + 4, append_string(out, identity))
    out.put('i', dst_model + 8, 0)
    out.put('i', dst_model + 0x1C, 0)
    skeleton = src.rel(fm + 0xC)
    dst_skeleton = w.block(skeleton, 0x24)
    out.put('HHH', dst_skeleton + 8, 1, 0, 0)  # one bone, no smooth or rigid skin matrices
    dst_bone = w.block(src.rel(skeleton + 0x14), 0x40)
    out.set_rel(dst_bone, append_string(out, 'root'))
    out.put('HHHH', dst_bone + 4, 0, 0xFFFF, 0xFFFF, 0xFFFF)
    out.put('3f', dst_bone + 0x14, 1, 1, 1)
    out.put('4f', dst_bone + 0x20, 0, 0, 0, 1)
    out.put('3f', dst_bone + 0x30, 0, 0, 0)
    out.put('H', dst_bone + 0xE, 0)  # no user data
    out.put('i', dst_bone + 0x3C, 0)
    out.set_rel(dst_skeleton + 0x10, append_dictionary(out, [('root', dst_bone)]))
    out.set_rel(dst_skeleton + 0x14, dst_bone)
    for offset in (0x18, 0x1C, 0x20):
        out.put('i', dst_skeleton + offset, 0)
    out.set_rel(dst_model + 0xC, dst_skeleton)

    # materials: the template material, each with its own colour texture and kind
    materials = []
    for i, m in enumerate(model.materials):
        require(m.kind in kinds, 'unknown material kind')
        material, ref_names = kinds[m.kind]
        textures = dict(texture_at)
        textures[ref_names[0]] = texture_at[colour[m.image]]
        name = 'mm_material%02d' % i
        dst = gp.copy_material(w, material, i, name, None, textures)
        refs = out.rel(dst + 0x28)
        out.set_rel(refs, append_string(out, colour[m.image]))
        state = out.rel(dst + 0x20)
        words = list(struct.unpack_from('>12I', out.b, state))
        if m.kind == 'mask':  # the template's own alpha test, with the author's cutoff
            words[RS_ALPHA] = ALPHA_TEST_ENABLE | ALPHA_FUNC_GEQUAL
            words[RS_ALPHA_REF] = struct.unpack('>I', struct.pack('>f', m.cutoff))[0]
        struct.pack_into('>12I', out.b, state, *words)
        materials.append((name, dst))
    out.set_rel(dst_model + 0x18, append_dictionary(out, materials))

    # shapes: one per primitive, each with its own vertex buffer
    count = len(model.parts)
    dst_vertex_array = out.append(bytes(0x20 * count), 4)
    attributes = src.rel(fvtx + 0x10)
    nattr = src.b[fvtx + 4]
    shapes, vertices = [], 0
    for index, part in enumerate(drawn_parts(model)):
        dst_vertex = dst_vertex_array + 0x20 * index
        out.b[dst_vertex:dst_vertex + 0x20] = template[fvtx:fvtx + 0x20]
        out.put('H', dst_vertex + 6, index)
        out.put('I', dst_vertex + 8, len(part.pos))
        out.b[dst_vertex + 0xC] = 0
        dst_attributes = w.block(attributes, 12 * nattr)
        entries = []
        for a in range(nattr):
            name = src.cstr(src.rel(attributes + 12 * a))
            out.set_rel(dst_attributes + 12 * a, append_string(out, name))
            entries.append((name, dst_attributes + 12 * a))
        out.set_rel(dst_vertex + 0x10, dst_attributes)
        out.set_rel(dst_vertex + 0x14, append_dictionary(out, entries))
        out.put('I', dst_vertex + 0x1C, 0)
        data = bytearray()
        for p, n, t in zip(part.pos, part.nrm, part.uv):
            data += struct.pack('>3f4x3f4x2f8x', *p, *n, *t)
        dst_buffer = w.block(src.rel(fvtx + 0x18), 24)
        out.put('I', dst_buffer + 4, len(data))
        out.set_rel(dst_buffer + 0x14, out.append(bytes(data), 0x1000))
        out.set_rel(dst_vertex + 0x18, dst_buffer)
        dst_shape = w.block(shape, 0x40)
        sname = 'shape%d' % index
        out.set_rel(dst_shape + 4, append_string(out, sname))
        out.put('HHHH', dst_shape + 0xC, index, part.material, 0, index)
        out.put('H', dst_shape + 0x14, 0)
        out.b[dst_shape + 0x16] = 0
        out.b[dst_shape + 0x17] = 1
        out.put('i', dst_shape + 0x28, 0)
        out.put('i', dst_shape + 0x2C, 0)
        out.put('I', dst_shape + 0x3C, 0)
        out.set_rel(dst_shape + 0x20, dst_vertex)
        mesh = src.rel(shape + 0x24)
        dst_mesh = w.block(mesh, 28)
        index_bytes = struct.pack('>%dI' % len(part.indices), *part.indices)
        out.put('II', dst_mesh, 4, 9)
        out.put('I', dst_mesh + 8, len(part.indices))
        out.put('H', dst_mesh + 0xC, 1)
        out.put('I', dst_mesh + 0x18, 0)
        out.set_rel(dst_mesh + 0x10, out.append(struct.pack('>II', 0, len(part.indices)), 4))
        dst_index = w.block(src.rel(mesh + 0x14), 24)
        out.put('I', dst_index + 4, len(index_bytes))
        out.set_rel(dst_index + 0x14, out.append(index_bytes, 0x1000))
        out.set_rel(dst_mesh + 0x14, dst_index)
        out.set_rel(dst_shape + 0x24, dst_mesh)
        lo = [min(p[k] for p in part.pos) for k in range(3)]
        hi = [max(p[k] for p in part.pos) for k in range(3)]
        mid = [(a + b) * .5 for a, b in zip(lo, hi)]
        ext = [(b - a) * .5 for a, b in zip(lo, hi)]
        out.put('f', dst_shape + 0x1C, sum(v * v for v in ext) ** .5)
        out.put('H', dst_shape + 0x1A, 1)
        out.set_rel(dst_shape + 0x30, out.append(struct.pack('>6H', 0, 0, 0, 0, 0, 1), 4))
        out.set_rel(dst_shape + 0x34, out.append(struct.pack('>6f', *mid, *ext), 4))
        out.set_rel(dst_shape + 0x38, out.append(struct.pack('>H', 0), 4))
        shapes.append((sname, dst_shape))
        vertices += len(part.pos)
    out.set_rel(dst_model + 0x10, dst_vertex_array)
    out.set_rel(dst_model + 0x14, append_dictionary(out, shapes))
    out.put('HHHH', dst_model + 0x20, count, count, len(materials), 0)
    out.put('I', dst_model + 0x28, vertices)
    return dst_model


def validate_bfres(data, identity, model):
    """Read the output back: counts, spans and texture formats match the model."""
    f = base.Bfres(data)
    require(f.u32(0xC) == len(data) and len(data) % 0x1000 == 0, 'invalid output BFRES size')
    models = f.dict(f.rel(0x20))
    require(len(models) == 1 and models[0][0] == identity and f.cstr(f.rel(models[0][1] + 4)) == identity, 'output identity mismatch')
    fm = models[0][1]
    shapes = f.dict(f.rel(fm + 0x14))
    materials = f.dict(f.rel(fm + 0x18))
    require(len(shapes) == len(model.parts) and len(materials) == len(model.materials), 'output shape or material count mismatch')
    textures = dict(f.dict(f.rel(0x24)))
    for (_, shape), part in zip(shapes, drawn_parts(model)):
        require(f.u16(shape + 0xE) == part.material, 'output shape material mismatch')
        fvtx = f.rel(shape + 0x20); vbuf = f.rel(fvtx + 0x18)
        require(f.u32(fvtx + 8) == len(part.pos) and f.u32(vbuf + 4) == len(part.pos) * 48 and
                0 < f.rel(vbuf + 0x14) <= len(data) - len(part.pos) * 48, 'output vertex span invalid')
        mesh = f.rel(shape + 0x24); ibuf = f.rel(mesh + 0x14)
        require(f.u32(mesh + 8) == len(part.indices) and f.u32(ibuf + 4) == len(part.indices) * 4 and
                0 < f.rel(ibuf + 0x14) <= len(data) - len(part.indices) * 4, 'output index span invalid')
    for (_, at), m in zip(materials, model.materials):
        refs = f.rel(at + 0x28)
        texture = f.rel(refs + 4)
        require(textures.get(f.cstr(f.rel(refs))) == texture, 'output material texture mismatch')
        surface = texture + 4
        width, height = f.u32(surface + 4), f.u32(surface + 8)
        require(f.u32(surface + 0x14) == base.FMT_RGBA8_SRGB and f.u32(surface + 0x30) == base.TILE_LINEAR_ALIGNED and
                f.u32(surface + 0x20) == width * height * 4 and 0 < f.rel(texture + 0xB0) <= len(data) - width * height * 4,
                'output texture span invalid')
        flags, alpha = f.u32(f.rel(at + 0x20)), f.u32(f.rel(at + 0x20) + 12)
        require((flags & 3, bool(alpha & ALPHA_TEST_ENABLE)) == ((MODE_ALPHA_MASK, True) if m.kind == 'mask' else (flags & 3, False)),
                'output material kind mismatch')
