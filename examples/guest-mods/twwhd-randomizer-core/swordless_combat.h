/* Swordless behavior: TWWHD Randomizer swordless.asm (MIT).
 * Native damage tail independently adapted from public CC0
 * ZeldaWWHDDecomp/wwhd a874e1a9 d_a_fganon.cpp damage_check.
 * No ownership flags or shared attack objects are forged. */
#pragma once
#ifndef R_SW
#define R_SW(type,address) (*(volatile type*)(address))
#endif

WWHD_GAME_ORIGINAL(0x0213A6FC,void,randomizer_native_phantom_damage,(u32 actor));
WWHD_GAME_ORIGINAL(0x02518DB0,u32,randomizer_native_phantom_power,(u32 info));
WWHD_GAME_FUNC(0x025A847C,u32,randomizer_swordless_particle,(u32 control,u32 group,u32 id,u32 pos,u32 angle,u32 scale,u32 alpha,u32 callback,s32 room,u32 color,u32 primary,u32 environment));
WWHD_GAME_FUNC(0x025E1AA4,void,randomizer_swordless_monster_sound,(u32 id,u32 position,u32 process,u32 argument,s32 reverb));
WWHD_GAME_FUNC(0x025E1928,void,randomizer_swordless_stop_bgm,(void));
static u32 randomizer_phantom_actor;

static void randomizer_phantom_hammer_tail(u32 actor,u32 info) {
    const u32 hit=R_SW(u32,info);
    if(!hit || !(R_SW(u32,hit+0x10)&0x10000)) return;
    R_SW(u32,info+4)=wwhd_attackCheck(actor,info);
    u32 play=wwhd_dComIfG_getGameInfo();
    const u32 player=R_SW(u32,play+0x5B2C);
    if(R_SW(u8,info+0xA)==1 && R_SW(u8,player+0x69E8))
        R_SW(s16,actor+0x5E2)=2;
    const u32 particles=R_SW(u32,play+0x5AB0);
    const u8 dead=R_SW(u8,info+9)!=0;
    if(dead) randomizer_swordless_particle(particles,0,0x10,actor+0x37C,0,0,255,0,-1,0,0,0);
    const f32 size=dead?2.0f:1.0f;
    f32 scale[3]={size,size,size};
    randomizer_swordless_monster_sound(R_SW(s8,actor+0x3A1)<=0?0x4943:0x4942,
        actor+0x37C,R_SW(u32,actor+4),0,
        (s32)wwhd_dComIfGp_getReverb(R_SW(s8,actor+0x326)));
    s16 angle[3]={0,wwhd_fopAcM_searchActorAngleY(actor,
        R_SW(u32,wwhd_dComIfG_getGameInfo()+0x5B2C)),0};
    play=wwhd_dComIfG_getGameInfo();
    randomizer_swordless_particle(R_SW(u32,play+0x5AB0),0,0xD,
        R_SW(u32,info+0x14),(u32)angle,(u32)scale,255,0,-1,0,0,0);
    const s16 action=R_SW(s16,actor+0x5BA);
    if(action==7 || action==10) {
        R_SW(s16,actor+0x5BC)=0;
        R_SW(s16,actor+0x5BA)=8;
        return;
    }
    const u8 variant=R_SW(u8,actor+0x3D8);
    if(variant==0) {
        if(R_SW(s8,actor+0x3A1)<=0) {
            R_SW(s16,actor+0x5BC)=0;
            R_SW(s16,actor+0x5E2)=1000;
            R_SW(s16,actor+0x5BA)=21;
            randomizer_swordless_stop_bgm();
        } else {
            const s8 remaining=(s8)(R_SW(u8,actor+0x8C4)-R_SW(u8,info+8));
            if(remaining<=0) {
                R_SW(s16,actor+0x5BC)=0;
                R_SW(s16,actor+0x5BA)=2;
                R_SW(s8,actor+0x8C4)=10;
            } else {
                R_SW(s8,actor+0x8C4)=remaining;
                R_SW(s16,actor+0x5BC)=10;
            }
        }
    } else if(variant==1) {
        R_SW(s8,actor+0x3A1)=0;
        R_SW(s16,actor+0x5BC)=0;
        R_SW(s16,actor+0x5BA)=3;
        randomizer_swordless_stop_bgm();
    }
}
WWHD_REPLACE(0x0213A6FC,void,randomizer_phantom_damage,(u32 actor)) {
    const u32 previous=randomizer_phantom_actor;
    randomizer_phantom_actor=init_swordless?actor:0;
    randomizer_native_phantom_damage(actor);
    randomizer_phantom_actor=previous;
}
WWHD_REPLACE(0x02518DB0,u32,randomizer_phantom_power,(u32 info)) {
    const u32 source=randomizer_native_phantom_power(info);
    const u32 actor=randomizer_phantom_actor;
    const s16 action=actor?R_SW(s16,actor+0x5BA):0;
    // The targeted native call passes its local CcAtInfo with this actor's
    // cylinder hit-position, after setting the six-frame damage timer.
    if(actor && R_SW(u32,info+0x14)==actor+0xBF8 &&
       R_SW(s16,actor+0x5E2)==6 &&
       (action==7 || action==8 || action==10)) {
        randomizer_phantom_actor=0; // Tail callbacks must not recursively adapt another power check.
        R_SW(u32,info+4)=source;
        randomizer_phantom_hammer_tail(actor,info);
        // This belongs to damage_check's private stack, not the collision object.
        // The native caller's existing null guard returns instead of executing its sword tail.
        R_SW(u32,info)=0;
    }
    return source;
}
