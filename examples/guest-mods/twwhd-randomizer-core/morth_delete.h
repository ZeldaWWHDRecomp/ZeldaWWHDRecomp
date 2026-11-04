/* Complete CC0 a874e1a9 d_a_ks.cpp::daKS_Delete021A9EC8.
 * MIT fix_vanilla_bugs.asm only decrements fully-created Morths (status2). */
#pragma once
WWHD_GAME_FUNC(0x025204C8,void,rmd_resource_delete,(u32 phase,u32 name));
WWHD_GAME_FUNC(0x025A9270,void,rmd_ripple_remove,(u32 ripple));
WWHD_GAME_FUNC(0x02041C30,void,rmd_fire_remove,(u32 fire));
WWHD_REPLACE(0x021A9EC8,s32,randomizer_morth_delete,(u32 actor)) {
    rmd_resource_delete(actor+0x3C8,0x10013794);
    rmd_ripple_remove(actor+0x7A8);
    rmd_fire_remove(actor+0xCDC);
    const unsigned int count=*(volatile unsigned int*)&WWHD_GAME_DATA(0x101B883C,unsigned int);
    if(*(volatile u8*)(actor+0xD)==2) {
        const unsigned int remaining=count-1u;
        WWHD_GAME_DATA(0x101B883C,unsigned int)=remaining;
        if(remaining==0) {
            (void)wwhd_dComIfG_getGameInfo();
            WWHD_GAME_DATA(0x101B8840,unsigned int)=0;
            WWHD_GAME_DATA(0x101B8848,unsigned int)=0;
            WWHD_GAME_DATA(0x101B8844,unsigned int)=0;
        }
    }
    return 1;
}
