"""Named data addresses from explicit public bindings, never initialized data."""
import re

# Each pattern names a public declaration, not an address supplied by this SDK.
BINDINGS = [
    ('dComIfG_save_info_pointer', 'include/d/d_com_inf_game.h',
     r'inline dSv_info_c\* dComIfGs_info\(\) \{ return gabi::at<dSv_info_c>\(gabi::load<u32>\((0x[0-9A-Fa-f]+)\) \+ 0x20\); \}'),
    ('dComIfG_resControl_pointer', 'include/d/d_com_inf_game.h',
     r'inline dRes_control_c\* dComIfG_resControl\(\) \{ return gabi::at<dRes_control_c>\(gabi::load<u32>\((0x[0-9A-Fa-f]+)\)\); \}'),
    ('mDoMtx_stack_now', 'include/bindings.h',
     r'static Mtx34\* get\(\) \{ return gabi::at<Mtx34>\((0x[0-9A-Fa-f]+)\); \}'),
    ('cXyz_Zero', 'include/bindings.h',
     r'#define cXyz_Zero gabi::at<cXyz>\((0x[0-9A-Fa-f]+)\)'),
]
TABLES = ['item_resource', 'field_item_res', 'item_info']


def declarations(read_source, revision):
    lines = ['/* Generated named data addresses from public ZeldaWWHDDecomp/wwhd.',
             ' * Revision: ' + revision,
             ' * CC0-1.0; see public-wwhd-LICENSE. USA version 0.',
             ' * Pointer slots contain guest addresses; tables contain no copied game bytes. */',
             '#pragma once', '#include "../wwhd_guest.h"', '']
    for name, source, pattern in BINDINGS:
        matches = list(re.finditer(pattern, read_source(source)))
        if len(matches) != 1:
            raise ValueError('public data declaration changed: ' + name)
        lines += [f'/* wwhd_src/{source} */', f'#define WWHD_ADDR_{name} {matches[0][1]}']
    source = 'include/d/actor/d_a_itembase.h'
    text = read_source(source)
    for name in TABLES:
        pattern = r'inline u32 ' + name + r'\(u32 no\) \{ return (0x[0-9A-Fa-f]+) \+ no \* (0x[0-9A-Fa-f]+|[0-9]+); \}'
        match = re.search(pattern, text)
        if not match:
            raise ValueError('public item table declaration changed: ' + name)
        lines += [f'/* wwhd_src/{source} */',
                  f'#define WWHD_ADDR_dItem_data_{name} {match[1]}',
                  f'#define WWHD_STRIDE_dItem_data_{name} {match[2]}',
                  f'#define WWHD_DATA_dItem_data_{name}(index) ((u32)&WWHD_GAME_DATA(WWHD_ADDR_dItem_data_{name}, u8) + (u32)(index) * WWHD_STRIDE_dItem_data_{name})']
    return '\n'.join(lines) + '\n'
