/* Complete CC0 daObjBarrier_c::break_start_wait_proc, a874e1a9.
 * MIT fix_vanilla_bugs: require eight shards before the barrier cutscene. */
#pragma once
#ifndef R_BARRIER
#define R_BARRIER(t,address) (*(volatile t*)(address))
#endif
WWHD_GAME_FUNC(0x025B8B94,s32,randomizer_barrier_event,(u32 event,u32 flag));
WWHD_GAME_FUNC(0x025B8B68,void,randomizer_barrier_event_on,(u32 event,u32 flag));
WWHD_GAME_FUNC(0x02543F10,s32,randomizer_barrier_event_index,(u32 manager,u32 name,u32 map));
WWHD_REPLACE(0x0232384C,void,randomizer_hyrule_barrier_wait,(u32 actor)) {
    if(randomizer_barrier_event(randomizer_daily_save()+0x644,0x3980)!=1)return;
    const u32 player=R_BARRIER(u32,wwhd_dComIfG_getGameInfo()+0x5B2C);
    union {unsigned int words[3];f32 values[3];} delta,horizontal;
    randomizer_hyrule_subtract(player+0x314,delta.values,actor+0x314);
    horizontal.words[0]=delta.words[0];
    horizontal.words[1]=WWHD_GAME_DATA(0x1002617C,u32);
    horizontal.words[2]=delta.words[2];
    const f64 square=randomizer_hyrule_square_magnitude(horizontal.values);
    const f64 distance=randomizer_hyrule_sqrt(square);
    if(distance<(f64)WWHD_GAME_DATA(0x10026228,f32))return;
    if(R_BARRIER(u8,randomizer_daily_save()+0x2E)!=0x3E)return;
    if(wwhd_dSv_player_collect_c_getTriforceNum(randomizer_daily_save()+0xD4)<8)return;
    if(!R_BARRIER(u16,wwhd_dComIfG_getGameInfo()+0x5BAC))return;
    const u8 cut=R_BARRIER(u8,player+0x3AC);
    if(cut<1 || cut>10)return;
    randomizer_barrier_event_on(randomizer_daily_save()+0x644,0x2C02);
    const s32 event=randomizer_barrier_event_index(wwhd_dComIfG_getGameInfo()+0x52C4,0x1002622C,255);
    R_BARRIER(s16,actor+0xDF8)=(s16)event;
    R_BARRIER(s32,actor+0xDFC)=1;
}
