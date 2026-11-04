/* Flexible spawn posture: flexible_entrances.asm (MIT, SuperDude88).
 * Native late resource sequence: public CC0 d_a_player_main_04b.cpp a874e1a9. */
#pragma once
#ifndef R_SPAWN_U8
#define R_SPAWN_U8(a,o) (*(volatile u8*)((a)+(o)))
#endif
#ifndef R_SPAWN_U16
#define R_SPAWN_U16(a,o) (*(volatile u16*)((a)+(o)))
#endif
#ifndef R_SPAWN_U32
#define R_SPAWN_U32(a,o) (*(volatile u32*)((a)+(o)))
#endif
static u32 randomizer_spawn_actor,randomizer_spawn_resource_phase;
static s32 randomizer_spawn_name(u32 safe,const char* name) {
    u32 text=R_SPAWN_U32(safe,0);
    if(!text)return 0;
    for(u32 i=0;;i++) {
        const u8 actual=R_SPAWN_U8(text,i),wanted=(u8)name[i];
        if(actual!=wanted)return 0;
        if(!wanted)return 1;
    }
}
#ifndef RANDOMIZER_PLAYER_INIT_BODY
WWHD_GAME_ORIGINAL(0x02410BE8,void,randomizer_native_player_init,(u32 actor));
#define RANDOMIZER_PLAYER_INIT_BODY(actor) randomizer_native_player_init(actor)
#endif
WWHD_REPLACE(0x02410BE8,void,randomizer_player_init,(u32 actor)) {
    const u32 previous_actor=randomizer_spawn_actor;
    const u32 previous_phase=randomizer_spawn_resource_phase;
    randomizer_spawn_actor=actor;randomizer_spawn_resource_phase=0;
    RANDOMIZER_PLAYER_INIT_BODY(actor);
    randomizer_spawn_actor=previous_actor;randomizer_spawn_resource_phase=previous_phase;
}
/* This exact helper occurs once in playerInit, after model/heap setup and just
 * before the final Always71/Link72 sight-texture requests. No mode changes occur
 * during those earlier callbacks. Other resource calls do not arm the seam. */
WWHD_HOOK_RETURN(0x023E07C0,randomizer_spawn_sound_ready,(u32 actor)) {
    if(actor==randomizer_spawn_actor && actor)randomizer_spawn_resource_phase=1;
}
WWHD_HOOK_RETURN(0x026066C4,randomizer_spawn_resource_ready,(u32 system,u32 safe,u32 index)) {
    if(!randomizer_spawn_actor || !randomizer_spawn_resource_phase ||
       system!=WWHD_GAME_DATA(0x101F4F28,u32))return;
    if(randomizer_spawn_resource_phase==1 && index==0x71 && randomizer_spawn_name(safe,"Always")) {
        randomizer_spawn_resource_phase=2;return;
    }
    if(randomizer_spawn_resource_phase!=2 || index!=0x72 || !randomizer_spawn_name(safe,"Link"))return;
    randomizer_spawn_resource_phase=0;
    const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    const u8 mode=R_SPAWN_U8(save,0x1177);
    if((R_SPAWN_U16(randomizer_spawn_actor,0x2F8)&1) && mode!=2 && mode!=3)
        R_SPAWN_U8(save,0x1177)=2;
}
#undef R_SPAWN_U8
#undef R_SPAWN_U16
#undef R_SPAWN_U32
