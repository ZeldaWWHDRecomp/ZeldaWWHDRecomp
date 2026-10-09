"""Narrow public call-site contracts for declarations absent from generic trampolines.

Only declarations are generated. No game implementation or initialized data is copied.
"""
import re


def semantic_declarations(read_source, revision):
    source = 'd/actor/d_a_player_main_07.cpp'
    text = read_source(source)
    contracts = [
        ('tact_judge', '0x025E1F34', 's32', '(s32 index, s32 direction)',
         r's32 judge = gabi::call<s32>\(0x025E1F34 /\* mDoAud_tact_judge \*/, \(s32\)mProcVar5, \(s32\)mProcVar3\);'),
        ('tact_get_beat', '0x025E1EFC', 'u32', '(void)',
         r'm3624 = gabi::call<u32>\(0x025E1EFC /\* mDoAud_tact_getBeat \*/\);'),
        ('tact_reset', '0x025E1E94', 'void', '(void)',
         r'gabi::call\(0x025E1E94 /\* mDoAud_tact_reset \*/\);'),
    ]
    lines = [f'/* Public HD call-site contracts at {revision}; CC0-1.0.',
             f' * Source: wwhd_src/{source}.',
             ' * Reset discards the result, matching the public caller.',
             ' * These declarations do not provide arbitrary melody playback. */',
             '#pragma once', '#include "../wwhd_guest.h"', '']
    for name, address, result, parameters, pattern in contracts:
        if not re.search(pattern, text):
            raise ValueError('public song call contract changed: ' + name)
        public_name = {'tact_judge': 'mDoAud_tact_judge', 'tact_get_beat': 'mDoAud_tact_getBeat',
                       'tact_reset': 'mDoAud_tact_reset'}[name]
        lines += [f'#define WWHD_ADDR_{public_name} {address}',
                  f'WWHD_GAME_FUNC({address}, {result}, wwhd_{name}, {parameters});']
    # The chime actor has no public WWHD_SIZE. Publish its explicitly asserted
    # member offset without inventing a complete object size or a prefix view.
    source = 'd/actor/d_a_obj_gong.cpp'
    text = read_source(source)
    declaration = re.findall(r'/\*\s*(0x[0-9A-Fa-f]+)\s*\*/\s*gptr<mDoExt_McaMorf>\s+mpMorf;', text)
    assertion = re.findall(r'WWHD_OFFSET\(Act_c, mpMorf, (0x[0-9A-Fa-f]+)\);', text)
    if len(declaration) != 1 or len(assertion) != 1 or int(declaration[0], 0) != int(assertion[0], 0):
        raise ValueError('public chime actor field changed')
    lines += [f'/* wwhd_src/{source}; no complete object size asserted by the public source. */',
              f'#define WWHD_OFFSET_daObjGong_Act_c_mpMorf {assertion[0]}']
    return '\n'.join(lines) + '\n'
