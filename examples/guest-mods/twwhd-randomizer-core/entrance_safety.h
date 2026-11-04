/* Flexible entrances: upstream flexible_entrances.asm (MIT, SuperDude88).
 * Source consumers: public CC0 d_a_auction.cpp and
 * hd_screen_dungeon_map_part1.cpp, a874e1a9. */
#pragma once
#ifndef R_ENT_U8
#define R_ENT_U8(a,o) (*(volatile u8*)((a)+(o)))
#endif
#ifndef R_ENT_U16
#define R_ENT_U16(a,o) (*(volatile u16*)((a)+(o)))
#endif
#ifndef R_ENT_U32
#define R_ENT_U32(a,o) (*(volatile u32*)((a)+(o)))
#endif
WWHD_GAME_ORIGINAL(0x0205C55C,u16,randomizer_native_auction_next_message,(u32 actor,u32 message));
WWHD_GAME_FUNC(0x025C3748,s32,randomizer_auction_exit,(s32 exit,f32 speed,u32 mode,s32 room));
WWHD_GAME_FUNC(0x025200D4,u32,randomizer_entrance_play,(void));

/* Keep every other dialogue branch native. The auction's leave message follows
 * SCLS1 in room0, then replaces its wipe with the auction's original fade. */
WWHD_REPLACE(0x0205C55C,u16,randomizer_auction_next_message,(u32 actor,u32 message)) {
    if(R_ENT_U32(message,0)!=0x1D1F)
        return randomizer_native_auction_next_message(actor,message);
    (void)randomizer_auction_exit(1,0.0f,0,0);
    R_ENT_U8(randomizer_entrance_play(),0x514D)=0;
    u16 bid=R_ENT_U16(actor,0x916);
    R_ENT_U16(randomizer_entrance_play(),0x5CEC)=bid;
    bid=R_ENT_U16(actor,0x916);
    R_ENT_U16(randomizer_entrance_play(),0x5BA6)=bid;
    R_ENT_U8(actor,0x943)=1;
    R_ENT_U32(actor,0x904)=0;
    return 16;
}

/* The six map objects already belong to this native menu. A different valid
 * dungeon selection must make the UI reload even for dungeon-to-dungeon travel.
 * Native invalid boss-index selection still clears mpMap without this reload. */
WWHD_REPLACE(0x02657EAC,void,randomizer_dungeon_select_map,(u32 menu)) {
    u32 maps[6];
    for(u32 i=0;i<6;i++)maps[i]=R_ENT_U32(menu,0xA0+4*i);
    const u32 owner=WWHD_GAME_DATA(0x101F8344,u32);
    const u32 ui=R_ENT_U32(owner,0x218);
    const u32 mode=R_ENT_U32(ui,0x50);
    u32 map=0;
    if(mode<6) {
        map=maps[mode];
        if(map!=R_ENT_U32(menu,0x920))R_ENT_U32(ui,0x64)=0;
    }
    R_ENT_U32(menu,0x920)=map;
}
#undef R_ENT_U8
#undef R_ENT_U16
#undef R_ENT_U32
