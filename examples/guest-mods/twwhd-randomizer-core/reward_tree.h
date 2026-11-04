/* Withered-tree async reward lifecycle from TWWHD Randomizer 1.2.0
 * flexible_item_locations.asm (MIT; see LICENSE-TWWHD-Randomizer.txt).
 * Native placement conditions verified in public CC0 d_a_obj_ftree.cpp. */
#pragma once
WWHD_GAME_ORIGINAL(0x02346D34,u32,randomizer_native_tree_create,(u32 actor));
WWHD_GAME_FUNC(0x028E93CC,void,randomizer_tree_translate,(u32 matrix,f32 x,f32 y,f32 z));
WWHD_GAME_FUNC(0x028E8F64,void,randomizer_tree_transform,(u32 matrix,u32 source,u32 dest));
WWHD_REPLACE(0x02346D34,u32,randomizer_tree_create,(u32 actor)) {
    const u32 phase=randomizer_native_tree_create(actor);
    if(phase==4) {
        *(volatile u32*)(actor+0x3A4)=0xFFFFFFFF;
        *(volatile u8*)(actor+0x32E)=0;
    }
    return phase;
}
static void randomizer_tree_pending(u32 actor) {
    const u32 id=*(volatile u32*)(actor+0x3A4);
    if(id==0xFFFFFFFF)return;
    u32 item=0;
    if(!wwhd_fopAcM_SearchByID(id,(u32)&item)) {
        wwhd_save_event_set(0x2E20,1);
        *(volatile u32*)(actor+0x3A4)=0xFFFFFFFF;
        return;
    }
    if(!item)return; /* Creation request exists but model loading is not finished. */
    *(volatile u32*)(item+0x2E8)=*(volatile u32*)(actor+4);
    if(*(volatile u8*)(actor+0x32E))return;
    *(volatile f32*)(item+0x370)=1.75f;
    *(volatile f32*)(item+0x340)=30.0f;
    *(volatile f32*)(item+0x374)=-2.1f;
    *(volatile u32*)(item+0x2E0)|=0x40;
    *(volatile u8*)(actor+0x32E)=1;
}
WWHD_HOOK(0x02347CF0,randomizer_tree_execute,(u32 actor)) {
    if(*(volatile u32*)(actor+0x7F4))randomizer_tree_pending(actor);
}
static u32 randomizer_tree_spawn(u32 actor,u32 position,u32 angles,u32 stationary) {
    const f32 scale[3]={1.0f,1.0f,1.0f};
    const u32 id=init_reward_tree==0xFF?0xFFFFFFFF:wwhd_fopAcM_createItem(position,init_reward_tree,0xFFFFFFFF,
        (u32)(s32)*(volatile s8*)(actor+0x326),3,angles,5,(u32)scale);
    *(volatile u32*)(actor+0x3A4)=id;
    *(volatile u8*)(actor+0x32E)=(u8)stationary;
    if(id==0xFFFFFFFF)return 0;
    wwhd_dSv_event_c_setEventReg((u32)wwhd_save_flags_detail_event_object(),0x9B07,
        *(volatile u32*)(actor+0x7BC));
    return 1;
}
WWHD_REPLACE(0x02347B7C,u32,randomizer_tree_launch,(u32 actor)) {
    f32 position[3];s16 angles[3];
    for(u32 n=0;n<3;n++) {
        position[n]=*(volatile f32*)(actor+0x314+n*4);
        angles[n]=*(volatile s16*)(actor+0x328+n*2);
    }
    position[1]+=860.0f;angles[1]=(s16)((u16)angles[1]+9000);
    if(!randomizer_tree_spawn(actor,(u32)position,(u32)angles,0))return 0;
    *(volatile s16*)(actor+0x7C0)=1;
    return 1;
}
WWHD_REPLACE(0x02347924,u32,randomizer_tree_place,(u32 actor)) {
    if(*(volatile s16*)(actor+0x4CE)!=-1 || wwhd_save_event_test(0x0102)!=1 ||
       wwhd_save_event_test(0x2E20))return 0;
    if(wwhd_dSv_event_c_getEventReg((u32)wwhd_save_flags_detail_event_object(),0x9B07)!=
       *(volatile u32*)(actor+0x7BC))return 0;
    f32 matrix[12],position[3];const f32 source[3]={0.0f,1.0f,310.0f};
    const s16 angles[3]={0,0,0};
    randomizer_tree_translate((u32)matrix,*(volatile f32*)(actor+0x314),
        *(volatile f32*)(actor+0x318),*(volatile f32*)(actor+0x31C));
    wwhd_mDoMtx_YrotM((u32)matrix,(s16)((u16)*(volatile s16*)(actor+0x32A)+9000));
    randomizer_tree_transform((u32)matrix,(u32)source,(u32)position);
    return randomizer_tree_spawn(actor,(u32)position,(u32)angles,1);
}
