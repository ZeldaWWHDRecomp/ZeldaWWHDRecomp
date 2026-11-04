/* Complete public CC0 d_a_npc_so_mode.cpp::modeEventFirstWait.
 * MIT make_game_nonlinear.asm 022E0A5C: keep normal-sail query, skip its gate. */
#pragma once
WWHD_GAME_FUNC(0x025200D4,u32,randomizer_fish_dialogue_info,(void));
WWHD_GAME_FUNC(0x0201ADE0,void,randomizer_fish_dialogue_subtract,(u32 a,u32 result,u32 b));
WWHD_GAME_FUNC(0x028E8DD0,f64,randomizer_fish_dialogue_square,(u32 vector));
WWHD_GAME_FUNC(0x028F4384,f64,randomizer_fish_dialogue_sqrt,(f64 square));
WWHD_GAME_FUNC(0x02520C0C,s32,randomizer_fish_dialogue_item,(u32 item));
WWHD_GAME_FUNC(0x02520A84,s32,randomizer_fish_dialogue_boss,(u32 stage));
WWHD_GAME_FUNC(0x022DE7D8,void,randomizer_fish_dialogue_mode,(u32 actor,s32 command,s32 mode));
WWHD_REPLACE(0x022E09CC,void,randomizer_fish_dialogue_wait,(u32 actor)) {
    const u32 ship=*(volatile u32*)(randomizer_fish_dialogue_info()+0x5B3C);
    if(!ship)return;
    *(volatile u32*)(actor+0x314)=*(volatile u32*)(ship+0x314);
    *(volatile u32*)(actor+0x318)=*(volatile u32*)(ship+0x318);
    *(volatile u32*)(actor+0x31C)=*(volatile u32*)(ship+0x31C);
    union { f32 values[3]; unsigned int words[3]; } difference;
    unsigned int flat[3];
    randomizer_fish_dialogue_subtract(ship+0x314,(u32)difference.values,actor+0xC24);
    flat[0]=difference.words[0];flat[1]=0;flat[2]=difference.words[2];
    const f64 distance=randomizer_fish_dialogue_sqrt(randomizer_fish_dialogue_square((u32)flat));
    if(distance<WWHD_GAME_DATA(0x10468720,f32))return;
    (void)randomizer_fish_dialogue_item(0x78);
    if(!randomizer_fish_dialogue_boss(3))return;
    if(*(volatile u32*)(randomizer_fish_dialogue_info()+0x5CD8)&0x10000)
        randomizer_fish_dialogue_mode(actor,0,6);
}
