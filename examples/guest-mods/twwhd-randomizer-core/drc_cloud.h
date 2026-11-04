/* Public CC0 daObjRcloud_c::_create, d_a_obj_rcloud.cpp/.h (a874e1a9).
 * Only the Dragon Roost (name zero) cloud uses Gohma completion. */
#pragma once
WWHD_GAME_FUNC(0x025D4ED0,void,randomizer_drc_cloud_ctor,(u32 actor));
WWHD_GAME_FUNC(0x025E7C6C,u32,randomizer_drc_cloud_btk,(u32 animation));
WWHD_GAME_FUNC(0x02387A58,u32,randomizer_drc_cloud_param,(u32 actor,u32 width,u32 shift));
WWHD_GAME_FUNC(0x025B7D90,s32,randomizer_drc_cloud_symbol,(u32 collect,u32 symbol));
WWHD_GAME_ORIGINAL(0x02520A84,s32,randomizer_drc_cloud_boss,(u32 stage));
WWHD_GAME_FUNC(0x02520460,s32,randomizer_drc_cloud_load,(u32 phase,u32 archive));
WWHD_GAME_FUNC(0x025D63E8,s32,randomizer_drc_cloud_heap,(u32 actor,u32 callback,u32 size));
WWHD_GAME_FUNC(0x0238735C,void,randomizer_drc_cloud_matrix,(u32 actor));
WWHD_GAME_FUNC(0x02387428,void,randomizer_drc_cloud_action,(u32 actor,s32 action));
WWHD_REPLACE(0x02387448,s32,randomizer_drc_cloud_create,(u32 actor)) {
    if(!(*(volatile u32*)(actor+0x2E4)&8)) {
        if(actor) {
            randomizer_drc_cloud_ctor(actor);
            *(volatile u32*)(actor+0xB4)=0x1002EC74;
            randomizer_drc_cloud_btk(actor+0x3B8);
        }
        *(volatile u32*)(actor+0x2E4)|=8;
    }
    if(*(volatile s8*)(actor+0xC)==0)
        *(volatile u32*)(actor+0x438)=randomizer_drc_cloud_param(actor,1,0);
    const u32 name=*(volatile u32*)(actor+0x438);
    if(name>1)return 5;
    const s32 done=name?randomizer_drc_cloud_symbol(
        WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0xD4,0):randomizer_drc_cloud_boss(3);
    if(done)return 5;
    s32 phase=randomizer_drc_cloud_load(actor+0x3AC,(u32)&WWHD_GAME_DATA(0x1002EC44,u8));
    *(volatile u8*)(actor+0x43C)=1;
    if(phase==4) {
        if(randomizer_drc_cloud_heap(actor,0x02387358,0x5A0)) {
            const u32 model=*(volatile u32*)(actor+0x3B4);
            *(volatile u32*)(actor+0x348)=model?model+0xC8:0;
            randomizer_drc_cloud_matrix(actor);
            randomizer_drc_cloud_action(actor,0);
        } else phase=5;
    }
    return phase;
}
