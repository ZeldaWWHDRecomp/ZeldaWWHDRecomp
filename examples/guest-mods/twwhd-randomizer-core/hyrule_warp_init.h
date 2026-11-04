/* CC0 d_a_warpdm20.cpp CreateInit. MIT remove_cutscenes.asm: shard gate
 * replaces the skipped-demo predicate; omit only forced Tingle autoStock. */
#pragma once
WWHD_GAME_FUNC(0x024D4C40,f32,randomizer_hyrule_water,(u32 actor,u32 position));
WWHD_GAME_FUNC(0x028E93CC,void,randomizer_hyrule_translation,(u32 matrix,f32 x,f32 y,f32 z));
WWHD_GAME_FUNC(0x024D4C94,void,randomizer_hyrule_end_animation,(u32 actor));
WWHD_GAME_FUNC(0x02543F10,s32,randomizer_hyrule_event_index,(u32 manager,u32 name,u32 hint));
WWHD_REPLACE(0x024D4D10,void,randomizer_hyrule_warp_init,(u32 actor)) {
    u32 model=*(volatile u32*)(actor+0x3B4);
    const u32 cull=model?model+0xC8:0;
    const f32 low=WWHD_GAME_DATA(0x100421A8,f32),zero=WWHD_GAME_DATA(0x1004214C,f32);
    const f32 high=WWHD_GAME_DATA(0x100421AC,f32),top=WWHD_GAME_DATA(0x100421B0,f32);
    *(volatile u32*)(actor+0x348)=cull;
    randomizer_mail_cull(actor,low,zero,low,high,top,high);
    union {unsigned int word[3];f32 value[3];} probe;
    probe.word[2]=*(volatile unsigned int*)(actor+0x31C);probe.word[0]=*(volatile unsigned int*)(actor+0x314);
    const f32 y=*(volatile f32*)(actor+0x318),one=WWHD_GAME_DATA(0x10042148,f32),rise=WWHD_GAME_DATA(0x100421B4,f32);
    probe.value[1]=y+rise;*(volatile f32*)(actor+0x364)=one;
    f32 water=randomizer_hyrule_water(actor,(u32)&probe);
    const f32 zscale=*(volatile f32*)(actor+0x338),sentinel=WWHD_GAME_DATA(0x100421B8,f32);
    const f32 yscale=*(volatile f32*)(actor+0x334),xscale=*(volatile f32*)(actor+0x330);
    model=*(volatile u32*)(actor+0x3B4);if(water==sentinel)water=zero;
    *(volatile f32*)(actor+0x318)=water;*(volatile f32*)(actor+0x400)=water;
    *(volatile f32*)(model+0xC4)=zscale;*(volatile f32*)(model+0xBC)=xscale;*(volatile f32*)(model+0xC0)=yscale;
    const f32 x=*(volatile f32*)(actor+0x314),yy=*(volatile f32*)(actor+0x318),z=*(volatile f32*)(actor+0x31C);
    const u32 matrix=(u32)&WWHD_GAME_DATA(0x1048D0CC,u8);
    randomizer_hyrule_translation(matrix,x,yy,z);
    unsigned int m[12];
    /* Native snapshots the matrix before model writes, permitting aliasing. */
    const u8 order[]={3,7,2,8,1,9,0,4,10,5,11,6};
    for(u32 i=0;i<12;i++)m[order[i]]=*(volatile unsigned int*)(matrix+order[i]*4);
    model=*(volatile u32*)(actor+0x3B4);
    const u8 stores[]={11,4,6,1,3,5,2,0,10,9,8,7};
    for(u32 i=0;i<12;i++)*(volatile unsigned int*)(model+0xC8+stores[i]*4)=m[stores[i]];
    const unsigned int py=*(volatile unsigned int*)(actor+0x318),pz=*(volatile unsigned int*)(actor+0x31C);
    *(volatile f32*)(actor+0x410)=one;*(volatile unsigned int*)(actor+0x404)=pz;
    *(volatile unsigned int*)(actor+0x400)=py;*(volatile f32*)(actor+0x408)=one;
    const unsigned int px=*(volatile unsigned int*)(actor+0x314);
    *(volatile unsigned int*)(actor+0x3E4)=0;*(volatile unsigned int*)(actor+0x3FC)=px;*(volatile f32*)(actor+0x40C)=one;
    if(wwhd_dSv_player_collect_c_getTriforceNum(randomizer_daily_save()+0xD4)>=8) {
        *(volatile u8*)(actor+0x3E8)=1;randomizer_hyrule_end_animation(actor);
    }
    const s32 event=randomizer_hyrule_event_index(wwhd_dComIfG_getGameInfo()+0x52C4,
        (u32)&WWHD_GAME_DATA(0x100421BC,u8),255);
    *(volatile u16*)(actor+0x3EE)=(u16)event;
    if(wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x2D02))
        wwhd_dSv_event_c_onEventBit(randomizer_daily_event(),0x1820);
}
