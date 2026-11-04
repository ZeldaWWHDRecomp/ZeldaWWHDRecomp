/* Normal Beedle inventory switching from flexible_item_locations.asm (MIT,
 * SuperDude88). Native tail/layout verified in public CC0 d_a_npc_bs1.cpp.
 * Special Rock Spire refill selection remains in the original function. */
#pragma once
WWHD_GAME_ORIGINAL(0x02212BA0,void,randomizer_native_beedle_create_list,(u32 actor));
static u8 randomizer_beedle_set_data_item(u32 data) {
    return *(volatile u8*)(*(volatile u32*)data+3);
}
WWHD_HOOK(0x028EA120,randomizer_beedle_alternate_bait,(void)) {
    /* The original alternate second slot was null. Switching the whole set
     * needs a real FoodAll descriptor there, even for a randomized bait gift. */
    WWHD_GAME_DATA(0x101BD558,u32)=0x101EB850;
}
WWHD_REPLACE(0x02212BA0,void,randomizer_beedle_create_list,(u32 actor)) {
    if(*(volatile s8*)(actor+0x99E)!=0) {
        randomizer_native_beedle_create_list(actor);return;
    }
    const s32 shop=*(volatile s8*)(actor+0x99F);
    const u32 table=shop==4?0x101BD560:shop==5?0x101BD56C:0x101BD548;
    const u32 positions=*(volatile u32*)((u32)&WWHD_GAME_DATA(0x101EB974,u8)+shop*4);
    const s16 angle[3]={0,0,0};
    for(u32 i=0;i<3;i++) {
        const u32 alternate=shop!=4 && shop!=5 && wwhd_save_event_test(0x6902)==1;
        const u32 descriptor=*(volatile u32*)(table+(i+(alternate?3:0))*4);
        *(volatile u32*)(actor+0x944+i*4)=wwhd_fopAcM_createShopItem(
            positions+i*12,randomizer_beedle_set_data_item(descriptor),(u32)angle,
            (u32)(s32)*(volatile s8*)(actor+0x326),0,0);
        *(volatile u32*)(actor+0x988+i*4)=descriptor;
    }
    *(volatile s16*)(actor+0x980)=3;
    wwhd_setItemListExplicit((void*)(actor+0x940),actor+0x988);
    for(s16 i=0;i<3;i++) {
        *(volatile s16*)(actor+0x940)=i;
        const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        if((!wwhd_dComIfGs_checkGetItem(0x31) &&
              wwhd_isBomb(randomizer_beedle_selected(actor+0x940))) ||
           (*(volatile u8*)(save+0x68)==0xFF &&
              wwhd_isArrow(randomizer_beedle_selected(actor+0x940)))) {
            randomizer_beedle_sold(actor+0x940,i);
            *(volatile u8*)(actor+0x8E4+i)=1;
        } else *(volatile u8*)(actor+0x8E4+i)=0;
        for(u32 n=0;n<3;n++)
            *(volatile f32*)(actor+0x8C0+i*12+n*4)=*(volatile f32*)(positions+i*12+n*4);
    }
    *(volatile s16*)(actor+0x940)=-1;
}
