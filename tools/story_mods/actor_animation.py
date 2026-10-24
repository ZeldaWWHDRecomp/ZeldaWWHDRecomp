# SPDX-License-Identifier: MPL-2.0
"""Original bounded glTF animation sampler and Wii U BFRES FSKA writer.

Only original clip data is emitted. No game animation is copied. Native baked
float curves interpolate adjacent samples, including a duplicate end sentinel.
"""
import bisect
import math
import re
import struct
import os
import sys
# Windows' embeddable Python runs isolated: the script's own folder is not on sys.path, so the
# sibling modules are found through it explicitly (issue #280).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import actor_model as static  # noqa: E402

require = static.require
VERSION = 2
MAX_CLIPS = 8
MAX_FRAMES = 900
MAX_SAMPLE_BYTES = 2 * 1024 * 1024
FPS = 30


def _accessor(doc, binary, index, width):
    require(type(index) is int and 0 <= index < len(doc.get('accessors', [])), 'invalid clip accessor')
    a = doc['accessors'][index]
    require(a.get('type') == {1: 'SCALAR', 3: 'VEC3', 4: 'VEC4'}[width] and a.get('componentType') == 5126 and not a.get('normalized') and 'sparse' not in a, 'clip requires float accessor')
    count = a.get('count'); require(type(count) is int and 1 <= count <= 4096, 'clip accessor count')
    vi = a.get('bufferView'); require(type(vi) is int and 0 <= vi < len(doc.get('bufferViews', [])), 'invalid clip view')
    v = doc['bufferViews'][vi]
    require(v.get('buffer', 0) == 0, 'external clip buffer')
    start, length = v.get('byteOffset', 0), v.get('byteLength')
    offset, stride = a.get('byteOffset', 0), v.get('byteStride', width * 4)
    require(all(type(x) is int and x >= 0 for x in [start, length, offset, stride]), 'invalid clip byte span')
    declared = doc.get('buffers', [{}])[0].get('byteLength', 0)
    require(type(declared) is int and 0 <= declared <= len(binary) and start <= declared and length <= declared - start, 'clip view outside buffer')
    require(stride >= width * 4 and stride <= 252 and stride % 4 == offset % 4 == 0 and offset + (count - 1) * stride + width * 4 <= length, 'clip accessor outside view')
    values = [struct.unpack_from('<' + 'f' * width, binary, start + offset + i * stride) for i in range(count)]
    require(all(math.isfinite(x) for row in values for x in row), 'non-finite animation key')
    return values


def _quaternion(q):
    length = math.sqrt(sum(x * x for x in q))
    require(length > 1e-8 and math.isfinite(length), 'zero animation quaternion')
    return tuple(x / length for x in q)


def _slerp(a, b, t):
    dot = sum(x * y for x, y in zip(a, b))
    if dot < 0:
        b = tuple(-x for x in b); dot = -dot
    dot = min(1., max(-1., dot))
    if dot > .9995:
        return _quaternion(tuple(x + t * (y - x) for x, y in zip(a, b)))
    angle = math.acos(dot); divisor = math.sin(angle)
    left, right = math.sin((1 - t) * angle) / divisor, math.sin(t * angle) / divisor
    return _quaternion(tuple(left * x + right * y for x, y in zip(a, b)))


def _euler(q, previous=None):
    """Equivalent Rz*Ry*Rx angles, choosing a nearby continuous Euler branch."""
    x, y, z, w = _quaternion(q)
    m20 = 2 * (x * z - w * y)
    pitch = math.asin(max(-1., min(1., -m20)))
    if abs(math.cos(pitch)) > 1e-7:
        roll = math.atan2(2 * (y * z + w * x), 1 - 2 * (x * x + y * y))
        yaw = math.atan2(2 * (x * y + w * z), 1 - 2 * (y * y + z * z))
    else:
        # At gimbal lock only roll-yaw (positive pitch) or roll+yaw
        # (negative pitch) is defined. Preserve the prior yaw when available.
        relation = math.atan2(2 * (w * x - y * z), 1 - 2 * (x * x + z * z))
        yaw = previous[2] if previous else 0.
        roll = relation + (yaw if pitch > 0 else -yaw)
    if previous is None: return (roll, pitch, yaw)
    candidates = [(roll, pitch, yaw), (roll + math.pi, math.pi - pitch, yaw + math.pi)]
    def unwrap(values):
        return tuple(v + 2 * math.pi * round((p - v) / (2 * math.pi)) for v, p in zip(values, previous))
    return min((unwrap(c) for c in candidates), key=lambda c: sum((a - b) ** 2 for a, b in zip(c, previous)))


def _sample(channel, time):
    times, values, interpolation, path = channel
    if time <= times[0]: return values[0]
    if time >= times[-1]: return values[-1]
    index = bisect.bisect_right(times, time) - 1
    if interpolation == 'STEP': return values[index]
    t = (time - times[index]) / (times[index + 1] - times[index])
    if path == 'rotation': return _slerp(values[index], values[index + 1], t)
    return tuple(a + t * (b - a) for a, b in zip(values[index], values[index + 1]))


def clips(model):
    """Validate and sample original document; output per-bone EulerXYZ/TR curves."""
    doc, binary = model['document'], model['binary']
    animations = doc.get('animations', [])
    require(isinstance(animations, list) and len(animations) <= MAX_CLIPS, 'animation clip limit')
    joint_nodes = doc['skins'][0]['joints']; lookup = {node: index for index, node in enumerate(joint_nodes)}
    result, names, total_bytes = [], set(), 0
    for animation in animations:
        name = animation.get('name', '')
        require(isinstance(name, str) and re.fullmatch(r'[a-z0-9_.-]{1,32}', name) and name not in names, 'invalid or duplicate clip name')
        names.add(name)
        require(not animation.get('extensions'), 'animation extensions unsupported')
        channels, duration = {}, 0.
        inputs = animation.get('samplers', [])
        authored = animation.get('channels', [])
        require(isinstance(authored, list) and 1 <= len(authored) <= 2 * len(joint_nodes), 'animation channel limit')
        for channel in authored:
            target = channel.get('target', {}); node, path = target.get('node'), target.get('path')
            require(type(node) is int and node in lookup and path in ('rotation', 'translation') and not target.get('extensions') and not channel.get('extensions'), 'unsupported animation target')
            key = (lookup[node], path); require(key not in channels, 'duplicate animation target')
            si = channel.get('sampler'); require(type(si) is int and 0 <= si < len(inputs), 'invalid animation sampler')
            sampler = inputs[si]; interpolation = sampler.get('interpolation', 'LINEAR')
            require(interpolation in ('LINEAR', 'STEP') and not sampler.get('extensions'), 'only LINEAR/STEP animation supported')
            times = [v[0] for v in _accessor(doc, binary, sampler.get('input'), 1)]
            require(times[0] >= 0 and all(a < b for a, b in zip(times, times[1:])), 'animation times must increase')
            values = _accessor(doc, binary, sampler.get('output'), 4 if path == 'rotation' else 3)
            require(len(values) == len(times), 'animation key counts differ')
            if path == 'rotation': values = [_quaternion(q) for q in values]
            else:
                require(all(abs(x) <= 100 for row in values for x in row), 'animation translation range')
                values = [tuple(x * 100 for x in row) for row in values]
            duration = max(duration, times[-1]); channels[key] = (times, values, interpolation, path)
        require(duration > 0, 'animation requires positive duration')
        # Tolerate float32 export error at an exact game tick, not a new duration.
        ticks = duration * FPS
        end = math.ceil(ticks - 1e-5)
        require(1 <= end <= MAX_FRAMES, 'animation duration exceeds 900 frames')
        rows = []
        for index, node in enumerate(joint_nodes):
            bind = doc['nodes'][node]
            rotation = _euler(bind.get('rotation', (0., 0., 0., 1.)))
            translation = tuple(x * 100 for x in bind.get('translation', (0., 0., 0.)))
            sampled = {}
            for path in ('rotation', 'translation'):
                if (index, path) not in channels: continue
                values = [_sample(channels[(index, path)], min(frame / FPS, duration)) for frame in range(end + 1)]
                # Quaternion signs must remain adjacent even across STEP boundaries.
                if path == 'rotation':
                    for i in range(1, len(values)):
                        if sum(a * b for a, b in zip(values[i - 1], values[i])) < 0:
                            values[i] = tuple(-x for x in values[i])
                if path == 'rotation':
                    angles = []
                    for q in values: angles.append(_euler(q, angles[-1] if angles else rotation))
                    values = angles
                values.append(values[-1]); sampled[path] = values
                total_bytes += len(values) * 3 * 4
            rows.append(dict(name=model['bones'][index]['name'], rotation=rotation, translation=translation, samples=sampled))
        require(total_bytes <= MAX_SAMPLE_BYTES, 'animation sample budget exceeds 2 MiB')
        result.append(dict(name=name, frames=end + 1, end=end, bones=rows))
    return result


def inspect_clips(model):
    return [dict(name=c['name'], frames=c['frames']) for c in clips(model)]


def append_clips(bfres, model):
    """Append only authored native FSKA resources, replacing the template dictionary."""
    from skinned_actor_model import append_string, append_dictionary
    parsed = clips(model)
    f = static.base.Bfres(bfres)
    if not parsed:
        f.put('i', 0x28, 0); f.put('H', 0x54, 0)
        return bytes(f.b)
    fm = f.dict(f.rel(0x20))[0][1]; skeleton = f.rel(fm + 12)
    entries = []
    for clip in parsed:
        header = f.append(b'\0' * 0x30, 4); f.b[header:header + 4] = b'FSKA'
        f.set_rel(header + 4, append_string(f, clip['name']))
        f.set_rel(header + 8, append_string(f, ''))
        # Standard scale, EulerXYZ rotation, looping. Curves are native baked
        # floats; FSKA BakedCurve flag is set with exact allocated baked size.
        f.put('IIHHII', header + 12, 0x1105, clip['frames'], len(clip['bones']), 0, 0, 0)
        bone_array = f.append(b'\0' * (0x18 * len(clip['bones'])), 4)
        f.set_rel(header + 0x20, bone_array); f.set_rel(header + 0x24, skeleton)
        bindings = f.append(struct.pack('>' + 'H' * len(clip['bones']), *range(len(clip['bones']))), 4)
        f.set_rel(header + 0x28, bindings)
        curve_count, baked_bytes = 0, 0
        for i, bone in enumerate(clip['bones']):
            at = bone_array + i * 0x18
            descriptions = []
            for path, offsets, bit in [('rotation', (0x20, 0x24, 0x28), 9), ('translation', (0x10, 0x14, 0x18), 13)]:
                if path in bone['samples']:
                    for component, offset in enumerate(offsets):
                        descriptions.append((offset, [r[component] for r in bone['samples'][path]], 1 << (bit + component)))
            flags = 0x38 | sum(row[2] for row in descriptions)
            f.put('I', at, flags); f.set_rel(at + 4, append_string(f, bone['name']))
            # Native Euler group boundaries match the player-local FSKA template.
            f.put('BBBBB', at + 8, 3, 6, len(descriptions), 0, curve_count)
            base_data = f.append(struct.pack('>10f', 1., 1., 1., *bone['rotation'], 1., *bone['translation']), 4)
            f.set_rel(at + 0x14, base_data)
            if descriptions:
                curves_at = f.append(b'\0' * (0x24 * len(descriptions)), 4); f.set_rel(at + 0x10, curves_at)
                for j, (offset, keys, _) in enumerate(descriptions):
                    curve = curves_at + j * 0x24
                    f.put('HHI5f', curve, 0x20, len(keys), offset, 0., float(clip['end']), 1., 0., keys[-1] - keys[0])
                    # Native baked evaluator ignores frame array; null is intentional.
                    data = f.append(struct.pack('>' + 'f' * len(keys), *keys), 4); f.set_rel(curve + 0x20, data)
                    baked_bytes += len(keys) * 4
            curve_count += len(descriptions)
        f.put('II', header + 0x18, curve_count, baked_bytes)
        entries.append((clip['name'], header))
    f.set_rel(0x28, append_dictionary(f, entries)); f.put('H', 0x54, len(entries))
    f.append(b'', 0x1000); f.put('I', 12, len(f.b))
    return bytes(f.b)
