/* Hero damage scaling: MIT TWWHD Randomizer misc_rando_features.asm.
 * Native complete consumer: public CC0 d_a_player_main_03.cpp a874e1a9. */
#pragma once
WWHD_GAME_FUNC(0x027200D0,u32,randomizer_damage_mode_manager,(u32 info));
WWHD_GAME_FUNC(0x0271FC5C,s32,randomizer_damage_hero_mode,(u32 manager));
WWHD_GAME_FUNC(0x023F4CB0,void,randomizer_damage_rupees,(u32 actor,f32 amount));
WWHD_REPLACE(0x023F51D0,s32,randomizer_damage_point,(u32 actor,f32 amount)) {
    const u32 manager=randomizer_damage_mode_manager(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x12C0);
    if(randomizer_damage_hero_mode(manager)) amount=amount*init_damage_multiplier;
    if(!(*(volatile u32*)(actor+0x3BC)&1) && !*(volatile s16*)(actor+0x699E)) {
        const u32 play=wwhd_dComIfG_getGameInfo();
        *(volatile f32*)(play+0x5B44)=*(volatile f32*)(play+0x5B44)+amount;
        if(amount<0.0f) {
            *(volatile u32*)(actor+0x3BC)&=~0x8000u;
            const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
            if(*(volatile u8*)(save+0x2E)!=0x3E) *(volatile u32*)(actor+0x3BC)&=~0x200000u;
        }
        return 1;
    }
    randomizer_damage_rupees(actor,amount);
    return 0;
}
