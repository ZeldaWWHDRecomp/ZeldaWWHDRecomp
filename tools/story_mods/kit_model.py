#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Kit models: a package's own model drawn with the materials of a room of the game, as a world object.

A world object's model (docs/world-objects.md) normally brings its own pictures. A kit model's texture-free materials
are game materials instead: like a mod stage's ground model (docs/mod-stages.md, "Kit materials"), each names one of a
kit room's materials, with that room's textures, shader archives and texture animations, read from the player's own
game files when the game starts (kit_materials.py: shared with the stage builder). Materials with a texture of their
own (an embedded PNG, as an ordinary mod model's: up to 512 a side, 1 MiB together) may be part of the same model.

Names: a texture-free material is the kit material the recipe's "materials" maps it to, else the one of its own name (a
Blender copy suffix ".001" ignored), else the recipe's "material"; an unnamed or col_* material takes "material" or
the kit room's main material. Any other name is refused with the kit room's names. A material that is a game material
(by its name, the mapping, an export_glb material's "wwhd" extras, or an image named after a game texture) may not
embed a texture: a package never carries the game's textures.

It is drawn like a game piece (game_piece_model.py), in at most two parts: the game materials of each kit model they
come from (part a from the kit room's model, part b from its model1, ...), each lit with the background light set of its
kit model's number (model: BG0, model1: BG1, ...) in the room the object stands in, and the own-texture materials (one
part, first, lit as an object is: light 0, multi_material.py's template materials). A part of game materials whose
kit model has a texture SRT animation for them (falling water, foam, lava) plays it, as on the kit room itself.

The output is a game piece set of one piece (the same files and metadata as game_piece_model.py writes, read by
the same runtime code): <out> = Yaz0(SARC) with <identity>.bfres (the textures once, one model per part), one
<part>.sharcfb per part (the shader archive the game looks up by model name), <out>.<id>.wcol (the collision, WCOL
v1 as collision_mesh.encode writes it) and <out>.json (metadata). Positions keep the GLB's own origin (metres x 100),
as world objects do. Collision: the meshes and nodes named *_col with col_* materials, else the visible triangles.
"far": true (a model used only as a distant model) draws game textures larger than 256 texels a side smaller.

usage: kit_model.py --game GAME --recipe RECIPE.json --identity gk... --out OUT.szs     prints the metadata

The recipe (format 1): {"kit_model_format": 1, "id": "island", "glb": "/abs/island.glb",
                        "kit": {"stage": "sea", "room": 41}, "material": "<kit material>",
                        "materials": {"<glb material>": "<kit material>"}, "far": false}
Nothing from the game is in this file, in a recipe or in a package: everything of the game comes from the
player's installation at build time and goes only to the player's model cache.
"""
import argparse
import hashlib
import json
import os
import re
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notebook_model as base  # noqa: E402
import collision_mesh  # noqa: E402
import game_piece_model as gp  # noqa: E402
import stage_builder as sb  # noqa: E402
import kit_materials as kmat  # noqa: E402  (the kit materials, shared with mod stages)
from skinned_actor_model import append_dictionary, append_string  # noqa: E402

VERSION = 2
FORMAT = 1
BuildError = base.BuildError
MAX_TRIANGLES = 20000          # visible, all parts together (a mod stage room's)
MAX_PART_VERTICES = 65535
MAX_PARTS = 2                  # kit models a model's materials may come from (a game piece draws two parts)
MAX_EXTENT = collision_mesh.MAX_EXTENT  # game units from the model's origin (the collision limit of world objects)
MAX_ARCHIVE = gp.MAX_ARCHIVE
FAR_TEXTURE = 256             # a distant model's game textures: larger ones are drawn at most this size
STAGE = re.compile(r'[A-Za-z0-9_]{1,7}')
ID = re.compile(r'[a-z0-9]+([-_][a-z0-9]+)*')


def require(ok, message):
    if not ok:
        raise BuildError(message)


def check_recipe(r):
    """the rules story_kit_models.h checks first (the runtime writes the recipe from the manifest)"""
    require(isinstance(r, dict) and r.get('kit_model_format') == FORMAT, 'unsupported kit_model_format')
    require(set(r) <= {'kit_model_format', 'id', 'glb', 'kit', 'material', 'materials', 'far'}, 'unknown recipe field')
    require(isinstance(r.get('far', False), bool), 'far is true or false')
    require(isinstance(r.get('id'), str) and len(r['id']) <= 32 and ID.fullmatch(r['id']), 'invalid model id')
    require(isinstance(r.get('glb'), str) and r['glb'].endswith('.glb') and os.path.isabs(r['glb']), 'the model must be an absolute .glb path')
    kit = r.get('kit')
    require(isinstance(kit, dict) and set(kit) == {'stage', 'room'} and isinstance(kit['stage'], str) and STAGE.fullmatch(kit['stage']) and
            isinstance(kit['room'], int) and not isinstance(kit['room'], bool) and 0 <= kit['room'] <= 63, 'kit is a stage of the game and a room number')
    require(r.get('material') is None or (isinstance(r['material'], str) and 0 < len(r['material']) <= 64), 'invalid material')
    mats = r.get('materials', {})
    require(isinstance(mats, dict) and len(mats) <= 16 and all(isinstance(k, str) and isinstance(v, str) and 0 < len(k) <= 64 and 0 < len(v) <= 64
                                                               for k, v in mats.items()), 'materials maps up to 16 model materials to kit materials')


def weld(shape):
    """one record per distinct vertex (the GLB's triangles often repeat their corners)"""
    seen, records, pos, remap = {}, [], [], []
    for r, p in zip(shape.records, shape.pos):
        k = seen.get(r)
        if k is None:
            k = seen[r] = len(records)
            records.append(r)
            pos.append(p)
        remap.append(k)
    shape.records, shape.pos = records, pos
    shape.tris = [(remap[a], remap[b], remap[c]) for a, b, c in shape.tris]
    return shape


def kit_collision(prims):
    """WCOL bytes and metadata: the *_col meshes (or the visible triangles), welded, in the model's own units"""
    vertices, triangles, _ = sb.collision_from(prims, [])
    used, out_vertices, out_tris = {}, [], []
    for a, b, c, surface, _exit in triangles:
        row = []
        for k in (a, b, c):
            if k not in used:
                used[k] = len(out_vertices)
                out_vertices.append(vertices[k])
            row.append(used[k])
        out_tris.append((row[0], row[1], row[2], collision_mesh.SEMANTICS[sb.SURFACES[surface]]))
    require(out_tris, 'the model has no collision triangles')
    require(len(out_vertices) <= collision_mesh.MAX_VERTICES and len(out_tris) <= collision_mesh.MAX_TRIANGLES,
            'the collision has %d vertices and %d triangles; a world object\'s collision may have %d of each (give the model a simpler *_col mesh)' %
            (len(out_vertices), len(out_tris), collision_mesh.MAX_TRIANGLES))

    class Api:
        @staticmethod
        def require(ok, message):
            require(ok, message)
    return collision_mesh.encode(out_vertices, out_tris, Api)


def glb_materials(path):
    """the GLB's materials as the reader sees them: name -> {'png': the embedded base colour PNG or None, 'mode',
    'cutoff', 'double', 'export': True for a material of the editor's export_glb (its "wwhd" extras), 'image_name'}"""
    with open(path, 'rb') as f:
        b = f.read(sb.MAX_GLB + 1)
    jlen = struct.unpack_from('<I', b, 12)[0]
    doc = json.loads(b[20:20 + jlen])
    binary = b''
    if 20 + jlen + 8 <= len(b):
        blen = struct.unpack_from('<I', b, 20 + jlen)[0]
        binary = b[28 + jlen:28 + jlen + blen]
    textures, images, views = doc.get('textures', []), doc.get('images', []), doc.get('bufferViews', [])
    out = {}
    for i, m in enumerate(doc.get('materials', [])):
        require(isinstance(m, dict), 'material %d: invalid material' % i)
        name = m.get('name', '')
        require(isinstance(name, str), 'material %d: invalid name' % i)
        pbr = m.get('pbrMetallicRoughness') or {}
        colour = pbr.get('baseColorTexture')
        entry = {'png': None, 'mode': m.get('alphaMode', 'OPAQUE'), 'cutoff': m.get('alphaCutoff', 0.5), 'double': m.get('doubleSided', False),
                 'export': isinstance(m.get('extras'), dict) and isinstance(m['extras'].get('wwhd'), dict) and 'kit_material' in m['extras']['wwhd'],
                 'image_name': None, 'index': i}
        if colour is not None:
            require(isinstance(colour, dict) and isinstance(colour.get('index'), int) and 0 <= colour['index'] < len(textures) and
                    colour.get('texCoord', 0) == 0, 'material %d (%s): needs a base colour texture on UV0' % (i, name))
            source = textures[colour['index']].get('source')
            require(isinstance(source, int) and 0 <= source < len(images), 'material %d (%s): invalid texture' % (i, name))
            image = images[source]
            require(image.get('mimeType') == 'image/png' and 'uri' not in image and isinstance(image.get('bufferView'), int) and
                    image['bufferView'] < len(views), 'material %d (%s): its texture must be an embedded PNG' % (i, name))
            v = views[image['bufferView']]
            entry['png'] = bytes(binary[v.get('byteOffset', 0):v.get('byteOffset', 0) + v['byteLength']])
            entry['image_name'] = image.get('name')
            entry['factor'] = pbr.get('baseColorFactor', [1, 1, 1, 1])
        require(name not in out or (out[name]['png'] is None and entry['png'] is None), 'two of the model\'s materials are named "%s"' % name)
        out[name] = entry
    return out


def classify(prims, materials, names, recipe, src_textures):
    """each visible material name -> ('kit', kit material) or ('own', its entry). A material with a texture of its own
    is the model's own; one without names a game material (`materials`, its own name, then `material`); an unnamed one
    (or a col_* one) takes `material` or the kit room's main material. Refused: a game material with an embedded
    texture (a package never carries the game's textures) and a name the kit room doesn't have."""
    out = {}
    for p in prims:
        if p['collision'] or p['material'] in out:
            continue
        name = p['material']
        entry = materials.get(name, {'png': None, 'export': False, 'image_name': None})
        named = names.resolve(name)
        if entry['png'] is not None:
            game = named or (entry['export'] and name) or (entry['image_name'] in src_textures and entry['image_name'])
            require(not game, 'material "%s" is a game material (%s) but embeds a texture: a package never carries the game\'s textures. '
                    'Remove its image (Blender: the Image Texture node), the game draws its own from the player\'s files; or give '
                    'the material a name of your own for a texture of your own' % (name, game))
            out[name] = ('own', entry)
            continue
        if named is None:
            if name == '' or name.startswith('col_') or recipe.get('material'):
                named = names.default
            else:
                raise BuildError('material "%s" has no texture, and %s' % (name, names.unknown(name)))
        out[name] = ('kit', named)
    return out


def own_parts(prims, kinds):
    """the own-texture materials' primitives as actor_model.Parts (one opaque/cut-out material per own material)"""
    import actor_model as am
    order = [n for n in kinds if kinds[n][0] == 'own']
    images, materials, parts, total = [], [], [], 0
    for n in order:
        e = kinds[n][1]
        label = 'material "%s"' % n
        require(e['mode'] in ('OPAQUE', 'MASK'), label + ': blended transparency (alphaMode BLEND) is not supported; use MASK or OPAQUE')
        require(isinstance(e['cutoff'], (int, float)) and 0 < e['cutoff'] < 1 and isinstance(e['double'], bool), label + ': invalid alphaCutoff or doubleSided')
        require(e.get('factor', [1, 1, 1, 1]) == [1, 1, 1, 1], label + ': base colour factor must be white (paint the colour into the texture)')
        w, h, _ = am.png(e['png'])
        if e['png'] not in images:
            images.append(e['png'])
            total += w * h * 4
            require(total <= am.MAX_TEXTURE_BYTES, 'the model\'s own textures may take at most 1 MiB (for example one 512 x 512, four 256 x 256 '
                    'or sixteen 128 x 128 textures); game materials do not count')
        materials.append(am.Material(n, images.index(e['png']), 'mask' if e['mode'] == 'MASK' else 'opaque', float(e['cutoff']), e['double']))
    vertices = triangles = 0
    for p in prims:
        if p['collision'] or p['material'] not in order:
            continue
        mat = order.index(p['material'])
        sides = 2 if materials[mat].double_sided else 1
        vertices += sides * len(p['positions'])
        triangles += sides * len(p['triangles'])
        parts.append(am.Part(list(p['positions']), list(p['normals']), [tuple(u) for u in p['uvs']], [i for t in p['triangles'] for i in t], mat))
    require(vertices <= am.MAX_VERTICES and triangles <= am.MAX_TRIANGLES, 'the model\'s own-texture materials have more than %d vertices or %d '
            'triangles (double-sided faces count twice)' % (am.MAX_VERTICES, am.MAX_TRIANGLES))
    require(len(parts) <= am.MAX_PRIMITIVES, 'the model\'s own-texture materials have more than %d primitives' % am.MAX_PRIMITIVES)
    return am.Parts(parts, materials, images), total


def build_set(game, recipe, identity):
    check_recipe(recipe)
    require(re.fullmatch(r'gk[0-9a-f]{29}', identity) is not None, 'invalid kit model identity')
    prims = sb.read_glb(recipe['glb'])
    kit = recipe['kit']
    files, src = kmat.room_model(game, kit['stage'], kit['room'])
    data = bytes(src.b)
    default = recipe.get('material')
    names = kmat.Names(src, kit['stage'], kit['room'], recipe.get('materials', {}), default)
    if names.default is None:
        names.default = kmat.main_material(src)
    mats = names.materials
    all_textures = dict(src.dict(src.rel(0x24)))
    kinds = classify(prims, glb_materials(recipe['glb']), names, recipe, all_textures)
    for p in prims:
        require(all(abs(x) <= MAX_EXTENT for v in p['positions'] for x in v), 'the model reaches farther than %d units from its origin' % MAX_EXTENT)
    visible = [p for p in prims if not p['collision']]
    tris = sum(len(p['triangles']) for p in visible)
    require(0 < tris <= MAX_TRIANGLES, 'the model has %d visible triangles; a kit model may have 1 to %d' % (tris, MAX_TRIANGLES))
    kit_prims = [p for p in visible if kinds[p['material']][0] == 'kit']
    by_model, _ = kmat.kit_shapes(src, mats, kit_prims, lambda p: kinds[p['material']][1])
    for shapes in by_model.values():
        for sh in shapes:
            weld(sh)
    own, own_bytes = (None, 0)
    if any(k == 'own' for k, _ in kinds.values()):
        own, own_bytes = own_parts(prims, kinds)
    kit_models = [n for n, _ in src.dict(src.rel(0x20))]
    order = sorted(by_model, key=kit_models.index)
    for kit_model in order:
        require(kit_model in kmat.LIGHTS, 'the kit room\'s model "%s" has no background light set (its materials can\'t be used)' % kit_model)
    require(len(order) + (1 if own else 0) <= MAX_PARTS,
            'the model is drawn in %d parts (%s); at most %d: game materials of one of the kit room\'s models and textures of your own, '
            'or game materials of two of its models' % (len(order) + (1 if own else 0), ', '.join((['your own textures'] if own else []) +
                                                                                                 ['the kit room\'s ' + m for m in order]), MAX_PARTS))
    # the output: the own part's textures (its pictures and the template's toon ramp), the game textures, then one model per part
    out = base.Bfres(data[:0x70])
    out.b[0x18:0x68] = bytes(0x50)
    w = gp.Writer(data, out)
    entries, shaders, models, parts, animations = [], {}, [], [], []
    if own:
        import multi_material as mm
        with open(os.path.join(game, mm.ARCHIVE), 'rb') as f:
            tfiles = dict(base.shaderprep.sarc(base.shaderprep.yaz0(f.read())))
        require(set(tfiles) == {mm.BFRES, mm.SHADERS}, 'unexpected player template archive')
        wo = gp.Writer(tfiles[mm.BFRES], out)
        own_at, own_names, colour = mm.write_textures(wo, own)
        entries += [(t, own_at[t]) for t in own_names]
    used_kit = sorted({kinds[p['material']][1] for p in kit_prims})
    textures, texture_bytes = {}, 0
    for t in kmat.texture_names(src, mats, used_kit):
        require(t in all_textures, 'a kit material names a missing texture')
        require(t not in dict(entries), 'a game texture has the name of one of the model\'s own textures: ' + t)
        if recipe.get('far') and max(src.u32(all_textures[t] + 8), src.u32(all_textures[t] + 12)) > FAR_TEXTURE:
            # a distant view (seen from the squares around): the game's own distant views have small textures too
            try:
                textures[t], size = gp.smaller_texture(w, all_textures[t], FAR_TEXTURE // 2)
            except BuildError:
                textures[t], size = gp.copy_texture(w, all_textures[t])
        else:
            textures[t], size = gp.copy_texture(w, all_textures[t])
        texture_bytes += size
        entries.append((t, textures[t]))
    out.set_rel(0x24, append_dictionary(out, entries))
    out.put('H', 0x52, len(entries))
    lo, hi = [1e30] * 3, [-1e30] * 3
    for p in visible:
        for v in p['positions']:
            for j in range(3):
                lo[j], hi[j] = min(lo[j], v[j]), max(hi[j], v[j])
    if own:
        name = identity[:2] + identity[-4:] + 'o'
        models.append((name, mm.write_model(wo, own, name, own_at, colour)))
        shaders[name + '.sharcfb'] = tfiles[mm.SHADERS]
        parts.append({'model': name, 'light': 0, 'kind': 'own', 'triangles': sum(len(q.indices) // 3 for q in mm.drawn_parts(own)),
                      'materials': [m.name for m in own.materials]})
    animated = dict(src.dict(src.rel(0x34))) if src.rel(0x34) else {}
    for k, kit_model in enumerate(order):
        shapes = by_model[kit_model]
        for sh in shapes:
            require(len(sh.records) <= MAX_PART_VERTICES, 'a part of the model has more than %d vertices' % MAX_PART_VERTICES)
        name = identity[:2] + identity[-4:] + 'abcd'[k]
        dst, used = kmat.write_kit_model(w, src, kit_model, name, shapes, textures)
        models.append((name, dst))
        archive = kit_model + '.sharcfb'
        require(archive in files, 'the kit room has no shader archive ' + archive)
        shaders[name + '.sharcfb'] = files[archive]
        part = {'model': name, 'light': kmat.LIGHTS[kit_model], 'kind': 'kit', 'triangles': sum(len(sh.tris) for sh in shapes), 'materials': used,
                'kit_model': kit_model}
        # the room's texture SRT animation of these materials (falling water, foam, lava), played on this part
        if kit_model in animated and kmat.animated(src, kit_model, used):
            animation = gp.copy_animation(w, kit_model, used, name, dst)
            if animation is not None:
                require(not animations, 'the model\'s game materials are animated in two of the kit room\'s models; a model plays one texture animation')
                animations.append((name, animation))
                part['animation'] = name
        parts.append(part)
    out.set_rel(0x20, append_dictionary(out, models))
    out.put('H', 0x50, len(models))
    if animations:
        out.set_rel(0x34, append_dictionary(out, animations))
        out.put('H', 0x5A, len(animations))
    out.set_rel(0x14, append_string(out, identity))
    out.append(b'', 0x1000)
    out.put('I', 0xC, len(out.b))
    bfres = bytes(out.b)
    members = {identity + '.bfres': bfres}
    members.update(shaders)
    packed = base.sarc(members)
    require(len(packed) <= MAX_ARCHIVE, 'the model\'s archive takes %.1f MiB; at most 12 MiB (its game textures, shader archives and models)'
            % (len(packed) / 1048576.0))
    collision, collision_info = kit_collision(prims)
    piece = {'id': recipe['id'], 'parts': parts, 'origin': [0.0, 0.0, 0.0], 'minimum': lo, 'maximum': hi, 'collision': collision_info}
    meta = {'set': 'kit:' + recipe['id'], 'identity': identity, 'pieces': [piece], 'models': [m for m, _ in models],
            'animations': [a for a, _ in animations], 'texture_bytes': texture_bytes, 'own_texture_bytes': own_bytes,
            'shader_bytes': sum(len(v) for v in shaders.values()), 'bfres_bytes': len(bfres), 'archive_bytes': len(packed), 'triangles': tris,
            'materials': {n: (k[1] if k[0] == 'kit' else None) for n, k in sorted(kinds.items())}}
    return packed, collision, meta


def build(game, recipe_path, identity, out):
    with open(recipe_path, 'rb') as f:
        recipe_bytes = f.read(64 * 1024 + 1)
    require(len(recipe_bytes) <= 64 * 1024, 'recipe too large')
    try:
        recipe = json.loads(recipe_bytes)
    except ValueError as exc:
        raise BuildError('the recipe is not JSON') from exc
    check_recipe(recipe)
    digest = hashlib.sha256()
    import multi_material, actor_model
    for path in (__file__, gp.__file__, sb.__file__, collision_mesh.__file__, kmat.__file__, multi_material.__file__, actor_model.__file__):
        with open(path, 'rb') as f:
            digest.update(f.read())
    digest.update(identity.encode() + b'\0' + recipe_bytes)
    with open(recipe['glb'], 'rb') as f:
        digest.update(hashlib.sha256(f.read()).digest())
    kit = recipe['kit']
    room = gp.room_files(game, '%s_Room%d.szs' % (kit['stage'], kit['room']))
    for name in sorted(room):
        digest.update(name.encode() + b'\0' + hashlib.sha256(room[name]).digest())
    # the template of the model's own-texture materials (read only when it has some; hashed always, it is small)
    template = os.path.join(game, multi_material.ARCHIVE)
    if os.path.isfile(template):
        with open(template, 'rb') as f:
            digest.update(b'template\0' + hashlib.sha256(f.read()).digest())
    cached = gp.cached_meta(out, digest.hexdigest())
    if cached and cached.get('converter_version') == gp.VERSION and cached.get('kit_converter_version') == VERSION:
        return cached
    packed, collision, meta = build_set(game, recipe, identity)
    archive = base.yaz0_stored(packed)
    meta.update(converter_version=gp.VERSION, kit_converter_version=VERSION, cache_key=digest.hexdigest(),
                archive_sha256=hashlib.sha256(archive).hexdigest())
    directory = os.path.dirname(os.path.abspath(out))
    os.makedirs(directory, exist_ok=True)

    def publish(path, body):
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=directory, prefix='.kit-model-', delete=False) as target:
                temporary = target.name
                target.write(body)
            os.replace(temporary, path)
            temporary = None
        finally:
            if temporary is not None:
                os.unlink(temporary)
    publish(out, archive)
    piece = meta['pieces'][0]
    path = '%s.%s.wcol' % (out, piece['id'])
    publish(path, collision)
    piece['collision_file'] = os.path.basename(path)
    publish(out + '.json', (json.dumps(meta, sort_keys=True) + '\n').encode())
    return meta


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', required=True)
    parser.add_argument('--recipe', required=True)
    parser.add_argument('--identity', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    try:
        meta = build(args.game, args.recipe, args.identity, args.out)
    except (BuildError, OSError, KeyError, IndexError, struct.error, ValueError) as exc:
        parser.exit(1, 'kit_model: %s\n' % exc)
    print(json.dumps(meta, sort_keys=True))


if __name__ == '__main__':
    main()
