/* Public CC0 d_door_hkyo.cpp/proc; MIT randomizer west/north gate branches. */
#pragma once
WWHD_GAME_FUNC(0x025B8B94,s32,randomizer_tower_door_event_query,(u32 event,u32 flag));
WWHD_GAME_FUNC(0x025B7B10,s32,randomizer_tower_door_song_query,(u32 collect,u32 song));
WWHD_GAME_FUNC(0x0252C49C,void,randomizer_tower_door_animation,(u32 glow,u32 index));
WWHD_GAME_FUNC(0x025E742C,s32,randomizer_tower_door_play,(u32 animation));
WWHD_GAME_FUNC(0x02520540,s32,randomizer_tower_door_reverb,(s32 room));
WWHD_GAME_FUNC(0x025E1A40,void,randomizer_tower_door_sound,(u32 sound,u32 position,u32 flags,s32 reverb));
static s32 randomizer_tower_door_event(u32 flag,u32 temporary) {
    return randomizer_tower_door_event_query(
        WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+(temporary?0x1178:0x644),flag);
}
WWHD_REPLACE(0x0252C574,void,randomizer_tower_door_proc,(u32 glow,u32 door)) {
    const u8 mode=*(volatile u8*)(glow+0x11);
    if(!mode || !*(volatile u32*)(glow+8))return;
    u32 index=0;
    if(mode==1) {
        if(randomizer_tower_door_event(0x0108,1))index=1;
        else if(randomizer_tower_door_event(0x0110,1))index=2;
    } else if(mode==4) {
        if(!randomizer_tower_door_event(0x1710,0))index=3;
    } else if(mode==3) {
        (void)randomizer_tower_door_song_query(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0xD4,2);
        if(!randomizer_tower_door_event(0x1704,0))index=3;
    } else if(mode==2) {
        (void)randomizer_tower_door_event(0x1704,0);
        if(!randomizer_tower_door_event(0x1B01,0))index=3;
    }
    randomizer_tower_door_animation(glow,index);
    if(!*(volatile u8*)(glow+0x10))return;
    (void)randomizer_tower_door_play(*(volatile u32*)(glow+0xC));
    if(door && (unsigned int)(door+0x37C)!=0) {
        const s32 reverb=randomizer_tower_door_reverb((s8)*(volatile u8*)(door+0x326));
        randomizer_tower_door_sound(0x61D4,door+0x37C,0,reverb);
    }
}
