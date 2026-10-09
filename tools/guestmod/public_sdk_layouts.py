"""Curated guest views of offset-annotated public HD layouts.

Unknown compound fields remain available through the byte view. Each exposed field
has its own offset assertion; no host wrapper implementation is copied.
"""
import re

CURATED = {
    'messages': ('include/d/actor/d_a_tag_msg.h', ['daTag_Msg_c']),
    'actor': ('include/f_op/f_op_actor.h', ['actor_place', 'dKy_tevstr_c', 'fopAc_ac_c']),
    'link': ('include/d/actor/d_a_player_main.h', ['daPy_actorKeep_l', 'daPy_lk_c']),
    'camera': ('include/d/d_camera.h', ['cSGlobe_l', 'camSphChkdata_l', 'dCamForcusLine', 'dCamera_c']),
    'items': ('include/d/actor/d_a_itembase.h', ['daItemBase_c']),
}
FIELD = re.compile(r'/\*\s*(0x[0-9A-Fa-f]+)\s*\*/\s*(be<\w+>|gptr<[^>]+>|\w+)\s+(\w+)(\[[0-9xXa-fA-F+*/ ()-]+\])?\s*;')
PRIMITIVES = {'u8', 's8', 'u16', 's16', 'u32', 's32', 'f32', 'f64', 'char'}


def body_of(text, name):
    match = re.search(r'\bstruct\s+' + re.escape(name) + r'(?:\s*:\s*\w+)?\s*\{', text)
    if not match:
        raise ValueError('missing public layout: ' + name)
    start, depth = match.end(), 1
    # Braces inside comments are irrelevant to layout nesting.
    masked = re.sub(r'/\*.*?\*/|//[^\n]*', lambda m: ' ' * len(m[0]), text, flags=re.S)
    for end in range(start, len(text)):
        depth += (masked[end] == '{') - (masked[end] == '}')
        if not depth:
            return text[start:end], masked[start:end]
    raise ValueError('unclosed public layout: ' + name)


def layout(text, name):
    size = re.search(r'WWHD_SIZE\(\s*' + re.escape(name) + r'\s*,\s*(0x[0-9A-Fa-f]+|[0-9]+)\s*\)', text)
    if not size:
        raise ValueError('missing public size: ' + name)
    body, masked = body_of(text, name)
    fields = []
    for match in FIELD.finditer(body):
        prefix = masked[:match.start()]
        if prefix.count('{') != prefix.count('}'):
            continue
        offset, kind, field, array = match.groups()
        if kind.startswith('gptr<'):
            kind = 'u32'  # guest address, never a host pointer
        kind = re.sub(r'be<(\w+)>', r'\1', kind)
        if kind not in PRIMITIVES:
            continue
        fields.append((int(offset, 16), kind, field, array or ''))
    known = {f[2] for f in fields}
    for offset in re.finditer(r'WWHD_OFFSET\(\s*' + re.escape(name) + r'\s*,\s*(\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\)', text):
        field = offset[1]
        if field in known:
            continue
        declaration = re.search(r'\b(be<\w+>|[us][0-9]+|f32)\s+' + re.escape(field) + r'\s*;', body)
        if declaration:
            kind = re.sub(r'be<(\w+)>', r'\1', declaration[1])
            if kind in PRIMITIVES:
                fields.append((int(offset[2], 16), kind, field, ''))
    fields.sort()
    lines = [f'typedef union {name} {{', f'    u8 bytes[{size[1]}];']
    for offset, kind, field, array in fields:
        pad = f'u8 _pad_{field}[0x{offset:X}]; ' if offset else ''
        lines.append(f'    struct __attribute__((packed)) {{ {pad}{kind} {field}{array}; }};')
    lines += [f'}} {name};', f'WWHD_SDK_ASSERT(sizeof({name}) == {size[1]}, "{name} size");']
    for offset, _, field, _ in fields:
        lines.append(f'WWHD_SDK_ASSERT(__builtin_offsetof({name}, {field}) == 0x{offset:X}, "{name}.{field}");')
    return '\n'.join(lines) + '\n'


def generate(root, output, revision):
    output.mkdir(parents=True, exist_ok=True)
    for subsystem, (source, names) in CURATED.items():
        text = (root / 'wwhd_src' / source).read_text()
        lines = [f'/* Generated from public ZeldaWWHDDecomp/wwhd {revision}.',
                 f' * Source: wwhd_src/{source}; CC0-1.0 (public-wwhd-LICENSE).',
                 ' * Partial views: named scalar fields only; unknown fields remain bytes.',
                 ' * Source offset qualifications still apply; see the public source. */',
                 '#pragma once', '#include "../wwhd_guest.h"',
                 '#ifndef WWHD_SDK_ASSERT', '#ifdef __cplusplus',
                 '#define WWHD_SDK_ASSERT(x, message) static_assert(x, message)', '#else',
                 '#define WWHD_SDK_ASSERT(x, message) _Static_assert(x, message)', '#endif', '#endif',
                 'WWHD_SDK_ASSERT(sizeof(void*) == 4, "SDK layouts require a 32-bit guest target");', '']
        for name in names:
            lines.append(layout(text, name))
        (output / (subsystem + '.h')).write_text('\n'.join(lines))
