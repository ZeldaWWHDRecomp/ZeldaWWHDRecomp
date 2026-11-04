/* Public CC0 daObjEayogn_c::_create and constructor, d_a_obj_eayogn.cpp/.h.
 * MIT randomizer make_game_nonlinear.asm replaces the direct Din's Pearl
 * query with false. The cooled-lava cover therefore never proceeds to resource
 * creation, retaining the bomb-flower puzzle regardless of pearl ownership.
 */
#pragma once
WWHD_GAME_FUNC(0x025D4ED0,void,randomizer_drc_lava_ctor,(u32 actor));
WWHD_REPLACE(0x023394F4,s32,randomizer_drc_lava_create,(u32 actor)) {
    if(!(*(volatile u32*)(actor+0x2E4)&8)) {
        randomizer_drc_lava_ctor(actor);
        *(volatile u32*)(actor+0xB4)=0x100278F4;
        *(volatile u32*)(actor+0x2E4)|=8;
    }
    return 5;
}
