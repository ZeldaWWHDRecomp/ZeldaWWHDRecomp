/* Public CC0 daDai_c::_create and inline constructors, d_a_dai.cpp,
 * d_a_dai.h, d_cc_d.h. Randomizer policy from flexible_item_locations.asm
 * (MIT, upstream license alongside): keep pedestals present without the bag.
 */
#pragma once
WWHD_GAME_FUNC(0x025A1458,void,randomizer_pedestal_npc_ctor,(u32 actor));
WWHD_GAME_FUNC(0x0200BD2C,void,randomizer_pedestal_status_ctor,(u32 status));
WWHD_GAME_FUNC(0x02515DA0,void,randomizer_pedestal_gstatus_ctor,(u32 status));
WWHD_GAME_FUNC(0x02515FB8,void,randomizer_pedestal_collision_ctor,(u32 collision));
WWHD_GAME_FUNC(0x02018590,void,randomizer_pedestal_cylinder_ctor,(u32 cylinder));
WWHD_GAME_FUNC(0x0254DA50,s32,randomizer_pedestal_bag_query,(u8 item,s32 flag));
WWHD_GAME_FUNC(0x02520460,s32,randomizer_pedestal_load,(u32 phase,u32 name));
WWHD_GAME_FUNC(0x025D63E8,s32,randomizer_pedestal_heap,(u32 actor,u32 callback,u32 size));
WWHD_GAME_FUNC(0x02116054,void,randomizer_pedestal_init,(u32 actor));

WWHD_REPLACE(0x02116258,s32,randomizer_pedestal_create,(u32 actor)) {
    if(!(*(volatile u32*)(actor+0x2E4)&8)) {
        randomizer_pedestal_npc_ctor(actor);
        *(volatile u32*)(actor+0xB4)=0x1000CC70;
        randomizer_pedestal_status_ctor(actor+0x7F0);
        randomizer_pedestal_gstatus_ctor(actor+0x80C);
        *(volatile u32*)(actor+0x808)=0x1004AE88;
        *(volatile u32*)(actor+0x80C)=0x1004AEC0;
        randomizer_pedestal_collision_ctor(actor+0x82C);
        *(volatile u32*)(actor+0x940)=0x100015A8;
        *(volatile u32*)(actor+0x93C)=0x1000CBCC;
        randomizer_pedestal_cylinder_ctor(actor+0x944);
        *(volatile u32*)(actor+0x868)=0x1004B108;
        *(volatile u32*)(actor+0x940)=0x1004B160;
        *(volatile u32*)(actor+0x958)=0x1004B150;
        *(volatile u32*)(actor+0x2E4)|=8;
    }
    /* The upstream patch removes the branch, retaining this real query. */
    (void)randomizer_pedestal_bag_query(0x30,1);
    const s32 phase=randomizer_pedestal_load(actor+0x7DC,0x1000CC68); /* Fdai */
    if(phase!=4)return phase;
    const s32 cloth=randomizer_pedestal_load(actor+0x7E4,0x1000CC60); /* Cloth */
    if(cloth!=4)return cloth;
    if(!randomizer_pedestal_heap(actor,0x02115E78,0x4C0))return 5;
    randomizer_pedestal_init(actor);
    return phase;
}
