"""Merging what several game processes of the models step recorded (model_sweep.py, several processes at once): each
process starts from a copy of the same caches and adds to it; the merged caches hold every record of every copy once.

  - Vulkan pipeline recipes (<cache>.recipes, "WWVKPR02", gfx/vulkan/pipeline_recipe.cpp): the base's recipes, then
    each part's new ones; a recipe in several parts gets the archive groups of all of them (up to 8, the runtime's
    limit), base and part order kept. The shader table is rebuilt (each distinct shader once).
  - The SPIR-V cache (vulkan-shaders/spirv.bin, "WWVKSC04", gfx/vulkan/shader_disk_cache.h): records by key.
  - The Metal recipe file (shaders.bin: appended records): the base, then each part's records the base lacks.
  - The placements and places the rooms list (actors-indexed.txt, places-indexed.txt of each part: "<order> <line>"):
    in the order one process would have read them, at most VARIANTS placements per actor name (dsweep.cpp harvest).
Only the files' own structure is read; nothing of the game is interpreted.
"""
import hashlib
import os
from pathlib import Path
import struct
import zlib

VARIANTS = 4          # dsweep.cpp kVariants
MAX_GROUPS = 8        # gfx/vulkan/pipeline_recipe.h kPipelineRecipeMaxGroups


def _replace(path, data):
    path = Path(path)
    temporary = path.with_name(path.name + '.merge')
    temporary.write_bytes(data)
    os.replace(temporary, path)


# --- Vulkan pipeline recipes -----------------------------------------------------------------------------------------
def _recipe_layout(words):
    """(word index of each stage's shader index, word index of the group count) of a version 2 recipe record."""
    i = 52 + 1                      # key (208 bytes), dynamic uniforms
    stages = []
    for _ in range(2):
        stages.append(i)
        i += 1
        i += 1 + 4 * words[i]       # descriptors
    i += 3                          # rendering
    i += 1 + words[i]               # formats
    i += 1 + 3 * words[i]           # bindings
    i += 1 + 4 * words[i]           # attributes
    i += 52                         # fixed state groups
    i += 1 + 8 * words[i]           # attachments
    i += 1 + words[i]               # dynamics
    if i >= len(words) or i + 1 + words[i] != len(words):
        raise ValueError('recipe record layout')
    return stages, i


def read_recipes(data):
    """-> (header bytes 8..56, [shader entry bytes], [(key, record words, shader indices, groups)])."""
    if len(data) < 68 or data[:8] != b'WWVKPR02':
        raise ValueError('not a version 2 recipe database')
    count, crc = struct.unpack_from('<II', data, 56)
    if zlib.crc32(data[64:]) != crc:
        raise ValueError('recipe database checksum')
    shaders = struct.unpack_from('<I', data, 64)[0]
    at, table = 68, []
    for _ in range(shaders):
        size = struct.unpack_from('<I', data, at + 4)[0]
        end = at + 16 + ((size + 3) & ~3)
        table.append(data[at:end])
        at = end
    records = []
    for _ in range(count):
        size = struct.unpack_from('<I', data, at)[0]
        at += 4
        if size % 4 or at + size > len(data):
            raise ValueError('recipe record bounds')
        words = list(struct.unpack_from('<%dI' % (size // 4), data, at))
        at += size
        stages, groups_at = _recipe_layout(words)
        records.append((bytes(data[at - size:at - size + 208]), words, [words[i] for i in stages],
                        words[groups_at + 1:]))
    if at != len(data):
        raise ValueError('trailing recipe bytes')
    return bytes(data[8:56]), table, records


def merge_recipes(base, parts):
    """The merged database (bytes) of base and parts (bytes each; a part of another device or runtime is left out)."""
    header, base_table, base_records = read_recipes(base)
    tables = [(base_table, base_records)]
    for data in parts:
        part_header, table, records = read_recipes(data)
        if part_header == header:
            tables.append((table, records))
    shaders, shader_index = [], {}
    merged, by_key = [], {}
    for table, records in tables:
        for key, words, indices, groups in records:
            entries = [table[i] for i in indices]
            if key in by_key:
                existing = merged[by_key[key]]
                for group in groups:
                    if group not in existing[2] and len(existing[2]) < MAX_GROUPS:
                        existing[2].append(group)
                continue
            mapped = []
            for entry in entries:
                if entry not in shader_index:
                    shader_index[entry] = len(shaders)
                    shaders.append(entry)
                mapped.append(shader_index[entry])
            by_key[key] = len(merged)
            merged.append([words, mapped, list(groups)])
    body = bytearray(struct.pack('<I', len(shaders)))
    for entry in shaders:
        body += entry
    for words, mapped, groups in merged:
        stages, groups_at = _recipe_layout(words)
        out = list(words[:groups_at])
        for at, index in zip(stages, mapped):
            out[at] = index
        out += [len(groups)] + groups
        body += struct.pack('<I', 4 * len(out)) + struct.pack('<%dI' % len(out), *out)
    head = b'WWVKPR02' + header + struct.pack('<II', len(merged), zlib.crc32(body))
    return head + bytes(body)


# --- SPIR-V cache ----------------------------------------------------------------------------------------------------
def _fnv(data):
    h = 14695981039346656037
    for byte in data:
        h = ((h ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def read_spirv(data):
    """-> (fingerprint, [module bytes], [(key, record bytes without the module index, module index)])."""
    if len(data) < 48 or data[:8] != b'WWVKSC04':
        raise ValueError('not a version 4 SPIR-V cache')
    fingerprint, length, _, count = struct.unpack_from('<QQQQ', data, 8)
    if length != len(data) - 40:
        raise ValueError('SPIR-V cache length')
    modules_count = struct.unpack_from('<Q', data, 40)[0]
    at, modules = 48, []
    for _ in range(modules_count):
        packed = struct.unpack_from('<I', data, at + 4)[0]
        end = at + 8 + ((packed + 3) & ~3)
        modules.append(bytes(data[at:end]))
        at = end
    records = []
    for _ in range(count):
        record = data[at:at + 40]
        records.append((struct.unpack_from('<Q', record)[0], bytes(record[:36]), struct.unpack_from('<I', record, 36)[0]))
        at += 40
    if at != len(data):
        raise ValueError('trailing SPIR-V cache bytes')
    return fingerprint, modules, records


def merge_spirv(base, parts):
    fingerprint, base_modules, base_records = read_spirv(base)
    sets = [(base_modules, base_records)]
    for data in parts:
        part_fingerprint, modules, records = read_spirv(data)
        if part_fingerprint == fingerprint:
            sets.append((modules, records))
    modules, module_index, records, keys = [], {}, [], set()
    for set_modules, set_records in sets:
        for key, fields, index in set_records:
            if key in keys:
                continue
            keys.add(key)
            module = set_modules[index]
            if module not in module_index:
                module_index[module] = len(modules)
                modules.append(module)
            records.append(fields + struct.pack('<I', module_index[module]))
    body = struct.pack('<Q', len(modules)) + b''.join(modules) + b''.join(records)
    return b'WWVKSC04' + struct.pack('<QQQQ', fingerprint, len(body), _fnv(body), len(records)) + body


# --- Metal recipe file -----------------------------------------------------------------------------------------------
def metal_records(data, start=0):
    """[(record bytes)] of a shaders.bin from byte start on (a torn last record is left out)."""
    out, at = [], start
    while at + 12 <= len(data):
        size = struct.unpack_from('<I', data, at + 8)[0]
        if at + 12 + size > len(data):
            break
        out.append(bytes(data[at:at + 12 + size]))
        at += 12 + size
    return out


def merge_metal(base, parts):
    """base, then each part's records past the base's own length that are new (the parts are copies of base)."""
    seen = {hashlib.sha1(r).digest() for r in metal_records(base)}
    out = bytearray(base)
    for data in parts:
        for record in metal_records(data, len(base) if data[:len(base)] == base else 0):
            digest = hashlib.sha1(record).digest()
            if digest not in seen:
                seen.add(digest)
                out += record
    return bytes(out)


# --- placements and places -------------------------------------------------------------------------------------------
def _indexed(paths):
    rows = []
    for part, path in enumerate(paths):
        try:
            text = Path(path).read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue
        for seq, line in enumerate(text.splitlines()):
            order, _, rest = line.strip().partition(' ')
            if order.isdigit() and rest:
                rows.append((int(order), part, seq, rest))
    rows.sort()
    return [rest for _, _, _, rest in rows]


def merge_placements(paths):
    """The placements one process would have kept: first VARIANTS distinct ones per actor name, in reading order."""
    out, seen, per_name = [], set(), {}
    for line in _indexed(paths):
        name = line.split(' ', 1)[0]
        if line in seen or per_name.get(name, 0) >= VARIANTS:
            continue
        seen.add(line)
        per_name[name] = per_name.get(name, 0) + 1
        out.append(line)
    return out


def merge_places(paths):
    out, seen = [], set()
    for line in _indexed(paths):
        if line not in seen:
            seen.add(line)
            out.append(line)
    return out
