/* Public CC0 daKytag05_Create, d_a_kytag05.cpp and d_a_kytag05.h.
 * MIT randomizer policy: pond wind follows Gohma's defeat, not Din's Pearl.
 */
#pragma once
WWHD_GAME_FUNC(0x02555D0C,u32,randomizer_drc_wind_environment,(void));
WWHD_GAME_FUNC(0x025D4ED0,void,randomizer_drc_wind_ctor,(u32 actor));
/* Read actual completion, bypassing this module's scoped Fishman override. */
WWHD_GAME_ORIGINAL(0x02520A84,s32,randomizer_drc_wind_boss,(u32 stage));
WWHD_GAME_FUNC(0x0257E6C4,void,randomizer_drc_wind_go,(void));
WWHD_GAME_FUNC(0x0257E684,void,randomizer_drc_wind_set,(s16 first,s16 second));
WWHD_REPLACE(0x021B0B5C,s32,randomizer_drc_wind_create,(u32 actor)) {
    (void)randomizer_drc_wind_environment();
    if(!(*(volatile u32*)(actor+0x2E4)&8)) {
        randomizer_drc_wind_ctor(actor);
        *(volatile u32*)(actor+0xB4)=0x10013B94;
        *(volatile u32*)(actor+0x2E4)|=8;
    }
    if(randomizer_drc_wind_boss(3))return 3;
    *(volatile u8*)(actor+0x3AC)=0;
    *(volatile s32*)(actor+0x3B0)=0;
    *(volatile s32*)(actor+0x3B4)=*(volatile u8*)(actor+0xB3);
    randomizer_drc_wind_go();
    randomizer_drc_wind_set(0,0);
    *(volatile s32*)(randomizer_drc_wind_environment()+0xA4C)=200;
    *(volatile u8*)(randomizer_drc_wind_environment()+0xA7D)=0;
    *(volatile s32*)(randomizer_drc_wind_environment()+0xA80)=30;
    return 4;
}
