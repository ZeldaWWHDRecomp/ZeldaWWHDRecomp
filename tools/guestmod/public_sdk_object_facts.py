"""Extract public object field/data facts without inventing complete object layouts."""
import re

# Each pattern captures the numeric operand in a specific public declaration/use.
FACTS = [
    ('WWHD_OFFSET_Link_equipped_item_model', 'd/actor/d_a_player_main_04.cpp', r'LK_FIELD\(u32, (0x[0-9A-Fa-f]+)\) == 0 /\* mpEquipItemModel \*/'),
    ('WWHD_OFFSET_actor_attention_flags', 'd/actor/d_a_npc_aj1.cpp', r'gabi::store<u32>\(gabi::ea\(this\) \+ (0x[0-9A-Fa-f]+), 0xA\); /\* attention_info.flags \*/'),
    ('WWHD_ALLOC_SIZE_line_mat0', 'm_Do/m_Do_ext_line.cpp', r'ext_lineMat0Ctor\(void\* self\)[^\n]*?call<u32>\(0x0273AD10,(0x[0-9A-Fa-f]+)\)'),
    ('WWHD_OFFSET_line_mat0_points_table', 'm_Do/m_Do_ext_line.cpp', r'ext_lineMat0Ctor\(void\* self\)[^\n]*?store<u32>\(o\+(0x[0-9A-Fa-f]+),0\);store<u16>\(o\+0x13E'),
    ('WWHD_OFFSET_J3DModel_mpMtxBlock', 'include/d/actor/d_a_pz.h', r'/\*\s*(0x[0-9A-Fa-f]+)\s*\*/ gptr<J3DMtxBlock_l> mpMtxBlock;'),
    ('WWHD_OFFSET_J3DMtxBlock_mpMtx', 'include/d/actor/d_a_pz.h', r'/\*\s*(0x[0-9A-Fa-f]+)\s*\*/ gptr<Mtx34> mpMtx;'),
    ('WWHD_OFFSET_J3DModel_base_matrix', 'include/m_Do/m_Do_ext.h', r'inline Mtx34\* J3DModel_getBaseTRMtx\(J3DModel\* m\) \{ return m \? gabi::at<Mtx34>\(gabi::ea\(m\) \+ (0x[0-9A-Fa-f]+)\) : nullptr; \}'),
    ('WWHD_OFFSET_J3DModel_base_scale', 'include/bindings.h', r'gabi::store<f32>\(gabi::ea\(m\) \+ (0x[0-9A-Fa-f]+), x\);'),
    ('WWHD_OFFSET_line_material_vtable', 'include/d/actor/d_a_ship.h', r'gabi::load<u32>\(gabi::load<u32>\(gabi::ea\(l\) \+ (0x[0-9A-Fa-f]+)\) \+ 0x14\)'),
    ('WWHD_OFFSET_line_material_id_vslot', 'include/d/actor/d_a_ship.h', r'gabi::load<u32>\(gabi::load<u32>\(gabi::ea\(l\) \+ 0x130\) \+ (0x[0-9A-Fa-f]+)\)'),
    ('WWHD_PLAY_LINE_PACKETS_OFFSET', 'include/d/actor/d_a_ship.h', r'u32 pkt = dComIfGp_ea\(\) \+ (0x[0-9A-Fa-f]+);'),
    ('WWHD_STRIDE_line_packet', 'include/d/actor/d_a_ship.h', r'gabi::call\(0x025EDD04, pkt \+ id \* (0x[0-9A-Fa-f]+), l\);'),
    ('WWHD_ADDR_Medli_flying', 'include/d/actor/d_a_npc_md.h', r'#define MD_M_FLYING (0x[0-9A-Fa-f]+)'),
    ('WWHD_ADDR_Medli_player_room', 'include/d/actor/d_a_npc_md.h', r'#define MD_M_PLAYERROOM (0x[0-9A-Fa-f]+)'),
    ('WWHD_ADDR_Medli_mirror', 'd/actor/d_a_npc_md_exec.cpp', r'#define MD_M_MIRROR (0x[0-9A-Fa-f]+)'),
    ('WWHD_ADDR_Medli_sea_talk', 'd/actor/d_a_npc_md_heap.cpp', r'#define MD_M_SEATALK (0x[0-9A-Fa-f]+)'),
    ('WWHD_ADDR_Medli_instance_pointer', 'd/actor/d_a_npc_md_exec.cpp', r'#define MD_INSTANCE (0x[0-9A-Fa-f]+)'),
    ('WWHD_ADDR_room_control_stay_no', 'include/d/actor/d_a_pt.h', r'static inline s8 dComIfGp_roomControl_getStayNo\(\) \{ return gabi::load<s8>\((0x[0-9A-Fa-f]+)\); \}'),
    ('WWHD_ADDR_Link_debug_position', 'd/actor/d_a_player_main_04.cpp', r'gabi::store<u32>\(b \+ 0x314 \+ k \* 4, gabi::load<u32>\((0x[0-9A-Fa-f]+) \+ k \* 4\)\);'),
    ('WWHD_ADDR_Link_debug_shape_angle', 'd/actor/d_a_player_main_04.cpp', r'gabi::store<u16>\(b \+ 0x328 \+ k \* 2, gabi::load<u16>\((0x[0-9A-Fa-f]+) \+ k \* 2\)\);'),
    ('WWHD_ADDR_Link_debug_current_angle', 'd/actor/d_a_player_main_04.cpp', r'gabi::store<u16>\(b \+ 0x320 \+ k \* 2, gabi::load<u16>\((0x[0-9A-Fa-f]+) \+ k \* 2\)\);'),
    ('WWHD_ADDR_Himo2_SafeString_vtable', 'include/d/actor/d_a_himo2.h', r'#define HIMO2_SAFESTRING_VTBL (0x[0-9A-Fa-f]+)'),
    ('WWHD_PLAY_PLAYER_STATUS1_OFFSET', 'd/actor/d_a_player_main_07.cpp', r'static inline u32 dComIfGp_checkPlayerStatus1_l\(u32 flag\) \{ return gabi::load<u32>\(dComIfGp_ea\(\) \+ (0x[0-9A-Fa-f]+)\) & flag; \}'),
    ('WWHD_PLAY_METRONOME_OFFSET', 'd/actor/d_a_player_main_07.cpp', r'gabi::store<u8>\(dComIfGp_ea\(\) \+ (0x[0-9A-Fa-f]+), 0\); /\* dComIfGp_setMetronomeOff\(\) \*/'),
    ('WWHD_PLAY_EVENT_FLAGS_OFFSET', 'include/d/actor/d_a_npc_mn.h', r'gabi::store<u16>\(play \+ (0x[0-9A-Fa-f]+), gabi::load<u16>\(play \+ 0x52B8\) \| f\);'),
    ('WWHD_PLAY_BG_COLLISION_OFFSET', 'include/d/d_com_inf_game.h', r'PLAY_BGS\s*=\s*(0x[0-9A-Fa-f]+),'),
    ('WWHD_PLAY_PLAYER_STATUS0_OFFSET', 'include/d/d_com_inf_game.h', r'PLAY_PLAYER_STATUS0\s*=\s*(0x[0-9A-Fa-f]+),'),
]


def unique_number(text, pattern, name):
    values = re.findall(pattern, text)
    if len(values) != 1:
        raise ValueError('public object fact changed: ' + name)
    return values[0]


def function_body(text, name):
    match = re.search(r'\b' + re.escape(name) + r'\([^)]*\)\s*\{', text)
    if not match:
        raise ValueError('public function declaration changed: ' + name)
    masked = re.sub(r'/\*.*?\*/|//[^\n]*', lambda m: ' ' * len(m[0]), text, flags=re.S)
    start, depth = match.end(), 1
    for end in range(start, len(text)):
        depth += (masked[end] == '{') - (masked[end] == '}')
        if depth == 0:
            return text[start:end]
    raise ValueError('unclosed public function: ' + name)


def object_declarations(read_source, revision):
    lines = [f'/* Public HD object facts at {revision}; CC0-1.0.',
             ' * Offsets do not assert a complete model, rope, or controller layout.',
             ' * Source qualifications ([v]/[g]) continue to apply. */',
             '#pragma once', '#include "../wwhd_guest.h"', '']
    for name, source, pattern in FACTS:
        text = read_source(source)
        if name == 'WWHD_PLAY_METRONOME_OFFSET':
            values = re.findall(pattern, text)
            if len(values) != 4 or len(set(values)) != 1:
                raise ValueError('public metronome uses changed')
            value = values[0]
        else:
            value = unique_number(text, pattern, name)
        lines += [f'/* wwhd_src/{source} */', f'#define {name} {value}']
    source = 'include/d/actor/d_a_player.h'
    callback = r'struct daPy_mtxFollowEcallBack_c\s*\{\s*be<u32> mVtable;\s*gptr<void> mpEmitter;\s*gptr<void> mpMatrix;\s*\};\s*WWHD_SIZE\(daPy_mtxFollowEcallBack_c, 0xC\);'
    if len(re.findall(callback, read_source(source))) != 1:
        raise ValueError('public matrix callback declaration changed')
    lines += [f'/* wwhd_src/{source}; declared fields in public order, size 0xC. */',
              'typedef struct { u32 mVtable, mpEmitter, mpMatrix; } daPy_mtxFollowEcallBack_c;']
    source = 'include/d/d_com_inf_game.h'
    safe = re.findall(r'struct SafeString\s*\{\s*be<u32> mStringTop;\s*be<u32> __vtbl;\s*\};', read_source(source))
    if len(safe) != 1:
        raise ValueError('public SafeString declaration changed')
    lines += [f'/* wwhd_src/{source}; GHS places the vtable after the string pointer. */',
              'typedef struct { u32 mStringTop, __vtbl; } wwhd_safe_string;']
    # Controller declarations expose only proven operands, not a fabricated
    # complete class or inferred meanings for unknown trailing words.
    source = 'SSystem/SComponent/c_API_controller.cpp'
    text = read_source(source)
    buttons = function_body(text, 'api_020075C0')
    pointer = unique_number(buttons, r'u32 buttons=load<u32>\(load<u32>\((0x[0-9A-Fa-f]+)\)\+0x124\);', 'pad pointer')
    buttons_offset = unique_number(buttons, r'u32 buttons=load<u32>\(load<u32>\(0x101F5088\)\+(0x[0-9A-Fa-f]+)\);', 'pad buttons')
    lines += [f'/* wwhd_src/{source} */', f'#define WWHD_ADDR_pad_pointer {pointer}',
              f'#define WWHD_OFFSET_pad_buttons_124 {buttons_offset}']
    for function, field in [('api_0200796C', '130'), ('api_02007990', '134'),
                            ('api_02007AFC', '138'), ('api_02007B20', '13C')]:
        pattern = r'return load<f32>\(load<u32>\(0x101F5088\)\+(0x[0-9A-Fa-f]+)\);'
        value = unique_number(function_body(text, function), pattern, function)
        lines += [f'#define WWHD_OFFSET_pad_axis_{field} {value}']
    # The public collision header declares this storage explicitly, without WWHD_SIZE.
    source = 'include/d/d_bg_s.h'
    text = read_source(source)
    size = unique_number(text, r'struct dBgS_LinChk \{ u8 _\[(0x[0-9A-Fa-f]+)\]; \};', 'dBgS_LinChk storage')
    lines += [f'/* wwhd_src/{source}; [v kamome] storage only. */',
              f'typedef struct {{ u8 bytes[{size}]; }} wwhd_line_check_storage;']
    body = function_body(text, 'dBgS_LinChk_ct')
    collision_fields = [
        ('pass_flags', r'gabi::store<u8>\(b \+ (0x[0-9A-Fa-f]+) \+ i,'),
        ('group', r'gabi::store<u32>\(b \+ (0x[0-9A-Fa-f]+), 1\);'),
        ('pass_pointer0', r'gabi::store<u32>\(b \+ (0x[0-9A-Fa-f]+), b \+ 0x58\);'),
        ('pass_pointer1', r'gabi::store<u32>\(b \+ (0x[0-9A-Fa-f]+), b \+ 0x64\);'),
    ]
    for name, pattern in collision_fields:
        value = unique_number(body, pattern, name)
        lines += [f'#define WWHD_OFFSET_line_check_{name} {value}']
    for name in ('10', '20', '58', '64'):
        value = unique_number(body, r'gabi::store<u32>\(b \+ (0x[0-9A-Fa-f]+), vt\.v' + name + r'\);', 'vtable ' + name)
        lines += [f'#define WWHD_OFFSET_line_check_vtable_{name} {value}']
    # Per-TU virtual table declarations; addresses are remapped with WWHD_GAME_DATA.
    source = 'd/actor/d_a_obj_hole.cpp'
    match = re.findall(r'static const dBgS_LinChk_vt LINCHK_VT = \{(0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+)\};', read_source(source))
    if len(match) != 1:
        raise ValueError('public collision virtual tables changed')
    lines += [f'/* wwhd_src/{source} */']
    for field, value in zip(('10', '20', '58', '64'), match[0]):
        lines += [f'#define WWHD_ADDR_ObjHole_line_check_vtable_{field} {value}']
    return '\n'.join(lines) + '\n'
