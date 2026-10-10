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


# Public offsets are extracted from declarations at the pinned revision. The
# transition byte is deliberately not called a layer: it enables the next stage.
PLAY_FIELDS = [
    ('PLAYER', 'include/d/d_com_inf_game.h', r'PLAY_PLAYER\s*=\s*(0x[0-9A-Fa-f]+),'),
    ('START_STAGE_NAME', 'include/d/actor/d_a_mo2.h',
     r'inline const char\* dComIfGp_getStartStageName\(\) \{ return gabi::at<const char>\(dComIfGp_ea\(\) \+ (0x[0-9A-Fa-f]+)\); \}'),
    ('EVENT_RUNNING', 'include/d/actor/d_a_fm_local.h',
     r'static inline bool dComIfGp_event_runCheck\(\) \{ return gabi::load<u8>\(dComIfGp_ea\(\) \+ (0x[0-9A-Fa-f]+)\) != 0; \}'),
    ('ENABLE_NEXT_STAGE', 'd/d_s_play_4.cpp',
     r'st8\(dComIfGp_ea\(\) \+ (0x[0-9A-Fa-f]+), 0\);\s*/\* offEnableNextStage \*/'),
]


def play_declarations(read_source):
    source = 'include/d/d_com_inf_game.h'
    pattern = r'inline u8\* dComIfGp_get\(\) \{ return gabi::call<u8\*>\((0x[0-9A-Fa-f]+)\); \}'
    matches = list(re.finditer(pattern, read_source(source)))
    if len(matches) != 1:
        raise ValueError('public play accessor changed')
    lines = [f'/* wwhd_src/{source}; call the accessor rather than a regional static address. */',
             f'WWHD_GAME_FUNC({matches[0][1]}, u8*, wwhd_play_get, (void));']
    lines += ['/* Player slot is [v kamome] in the public source; preserve its 8-byte stride.',
              ' * ENABLE_NEXT_STAGE is the transition flag, not a stage layer. */']
    for name, source, pattern in PLAY_FIELDS:
        matches = list(re.finditer(pattern, read_source(source)))
        if len(matches) != 1:
            raise ValueError('public play declaration changed: ' + name)
        lines += [f'/* wwhd_src/{source} */',
                  f'#define WWHD_PLAY_{name}_OFFSET {matches[0][1]}']
    return lines


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
    lines += play_declarations(read_source)
    return '\n'.join(lines) + '\n'
