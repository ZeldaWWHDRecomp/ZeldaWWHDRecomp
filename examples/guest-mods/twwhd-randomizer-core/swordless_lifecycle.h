/* Temporary equipment: TWWHD Randomizer swordless.asm (MIT).
 * Native phase ordering and SafeString seam: public CC0
 * ZeldaWWHDDecomp/wwhd a874e1a9 d_s_play.cpp / d_s_play_6.cpp.
 * These observers retain the native empty-callee register behavior. */
#pragma once
#ifndef R_SW
#define R_SW(type,address) (*(volatile type*)(address))
#endif
#define RANDOMIZER_PHASE_LITERAL ((u32)&WWHD_GAME_DATA(0x1005336C,u8))
#define RANDOMIZER_PHASE_STRING_VTABLE ((u32)&WWHD_GAME_DATA(0x10052EEC,u8))
typedef struct {
    u32 stage,literal_object;
    u8 enabled,ready,literal_count,finished;
} randomizer_swordless_phase_scope;
static randomizer_swordless_phase_scope randomizer_swordless_phase;

static u8 randomizer_swordless_stage_is(u32 stage,const char*name) {
    for(u32 i=0;i<9;i++) {
        const u8 value=R_SW(u8,stage+i);
        if(value!=(u8)name[i]) return 0;
        if(!value) return 1;
    }
    return 0;
}
static void randomizer_swordless_late_equipment(u32 stage) {
    if(randomizer_swordless_stage_is(stage,"GTower")) return;
    const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    if(randomizer_swordless_stage_is(stage,"Ojhous") && R_SW(u8,save+0x2E)==255) {
        R_SW(u8,save+0x2E)=0x38;
        return; // The upstream Orca grant bypasses ordinary temporary-sword cleanup.
    }
    if(!R_SW(u8,save+0xD4)) R_SW(u8,save+0x2E)=255;
}
WWHD_GAME_ORIGINAL(0x025B1FF8,s32,randomizer_native_swordless_phase,(u32 scene));
WWHD_REPLACE(0x025B1FF8,s32,randomizer_swordless_phase_load,(u32 scene)) {
    const randomizer_swordless_phase_scope previous=randomizer_swordless_phase;
    const u32 stage=wwhd_dComIfG_getGameInfo()+0x5134;
    randomizer_swordless_phase=(randomizer_swordless_phase_scope){stage,0,!!init_swordless,0,0,0};
    if(init_swordless && randomizer_swordless_stage_is(stage,"GTower")) {
        const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        if(R_SW(u8,save+0x2E)==255) R_SW(u8,save+0x2E)=0x3E;
    }
    const s32 result=randomizer_native_swordless_phase(scene);
    randomizer_swordless_phase=previous;
    return result;
}
WWHD_HOOK_RETURN(0x02526124,randomizer_swordless_phase_actors_ready,(void)) {
    if(randomizer_swordless_phase.enabled) {
        randomizer_swordless_phase.ready=1;
        randomizer_swordless_phase.literal_count=0;
        randomizer_swordless_phase.literal_object=0;
    }
}
WWHD_HOOK_RETURN(0x025B376C,randomizer_swordless_phase_string_ready,(u32 object)) {
    randomizer_swordless_phase_scope*s=&randomizer_swordless_phase;
    if(!s->enabled || !s->ready || s->finished ||
       R_SW(u32,object+4)!=RANDOMIZER_PHASE_STRING_VTABLE) return;
    const u32 text=R_SW(u32,object);
    if(text==RANDOMIZER_PHASE_LITERAL) {
        // Both assurances must belong to the same exact native GTower literal object.
        if(s->literal_object==object && s->literal_count==1) s->literal_count=2;
        else {s->literal_object=object;s->literal_count=1;}
        return;
    }
    if(text==s->stage && object!=s->literal_object && s->literal_count==2) {
        s->finished=1;
        randomizer_swordless_late_equipment(s->stage);
    }
}
