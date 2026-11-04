/* Public CC0 daLodbg_c::createModelData, d_a_lod_bg.cpp/.h (a874e1a9).
 * MIT randomizer policy: only Dragon Roost's secondary model follows Gohma.
 */
#pragma once
WWHD_GAME_FUNC(0x0252447C,u32,randomizer_drc_lod_resource,(u32 name,u32 path));
WWHD_GAME_ORIGINAL(0x02520A84,s32,randomizer_drc_lod_boss,(u32 stage));
#ifndef RANDOMIZER_DRC_LOD_NAME_ACCESS
#define RANDOMIZER_DRC_LOD_NAME_ACCESS(name) \
    ((void(*)(u32))(*(volatile u32*)(*(volatile u32*)((name)+4)+0x14)))((name))
#endif
WWHD_REPLACE(0x021B50C4,s32,randomizer_drc_lod_models,(u32 actor)) {
    RANDOMIZER_DRC_LOD_NAME_ACCESS(actor+0x3AC);
    *(volatile u32*)(actor+0x3EC)=randomizer_drc_lod_resource(
        *(volatile u32*)(actor+0x3AC),(u32)&WWHD_GAME_DATA(0x10014034,u8));
    if(!*(volatile u32*)(actor+0x3EC)) return 0;
    const u32 room=*(volatile u32*)(actor+0xB0);
    if(room==11) {
        RANDOMIZER_DRC_LOD_NAME_ACCESS(actor+0x3AC);
        *(volatile u32*)(actor+0x3F0)=randomizer_drc_lod_resource(
            *(volatile u32*)(actor+0x3AC),(u32)&WWHD_GAME_DATA(0x10014028,u8));
        return *(volatile u32*)(actor+0x3F0)!=0;
    }
    if((room==1 && !wwhd_save_event_test(0x1820)) ||
       (room==13 && !randomizer_drc_lod_boss(3))) {
        RANDOMIZER_DRC_LOD_NAME_ACCESS(actor+0x3AC);
        *(volatile u32*)(actor+0x3F0)=randomizer_drc_lod_resource(
            *(volatile u32*)(actor+0x3AC),(u32)&WWHD_GAME_DATA(0x10014040,u8));
        return *(volatile u32*)(actor+0x3F0)!=0;
    }
    return 1;
}
