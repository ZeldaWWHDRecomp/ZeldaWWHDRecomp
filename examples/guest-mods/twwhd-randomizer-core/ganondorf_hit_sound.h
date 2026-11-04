/* Complete CC0 d_cc_uty.cpp::cc_at_check (a874e1a9).
 * MIT fix_vanilla_bugs: dead-health Ganondorf uses the normal hit path. */
#pragma once
#ifndef R_HIT
#define R_HIT(t,a) (*(volatile t*)(a))
#endif
WWHD_GAME_FUNC(0x02518DB0,u32,randomizer_hit_power,(u32 info));
WWHD_GAME_FUNC(0x025E1FD8,void,randomizer_hit_enemy_damage,(u32 actor));
WWHD_GAME_FUNC(0x025D9000,void,randomizer_hit_steal,(u32 pos,u32 table,s32 room,u32 zero,u32 bit));
WWHD_GAME_FUNC(0x025157AC,u32,randomizer_hit_object_info,(u32 object));
WWHD_GAME_FUNC(0x02518BE8,u32,randomizer_hit_sound_id,(u32 object));
WWHD_GAME_FUNC(0x025D6894,s32,randomizer_hit_player_angle,(u32 actor,u32 player));
WWHD_GAME_FUNC(0x025E1D30,void,randomizer_hit_music,(u32 id));
static void randomizer_hit_sound(u32 actor,u32 id,u32 arg) {
    if(actor+0x37C) {
        const s32 reverb=randomizer_boko_reverb(R_HIT(s8,actor+0x326));
        randomizer_boko_sound(id,actor+0x37C,arg,reverb);
    }
}
static u32 randomizer_hit_critical_sound(u32 object) {
    if(R_HIT(u32,object+0x10)&0x01010000)return 0x2855;
    switch(R_HIT(u8,object+0x6C)) {
        case 4:return 0x2835;case 5:return 0x2836;case 7:return 0x2879;case 8:return 0x286F;default:return 0x2806;
    }
}
WWHD_REPLACE(0x025192A8,u32,randomizer_ganondorf_hit_check,(u32 target,u32 info)) {
    const u32 player=R_HIT(u32,wwhd_dComIfG_getGameInfo()+0x5B2C);
    R_HIT(u32,info+0x18)=0;
    u32 actor=randomizer_hit_power(info);
    R_HIT(u32,info+4)=actor;
    if(!actor)return actor;
    randomizer_hit_enemy_damage(actor);
    actor=R_HIT(u32,info+4);
    const f64 magnitude=randomizer_hyrule_sqrt(randomizer_hyrule_square_magnitude((const f32*)(actor+0x33C)));
    const f32 limit=WWHD_GAME_DATA(0x1004B460,f32);
    actor=R_HIT(u32,info+4);
    f32 x,z;
    if(magnitude>limit) {z=R_HIT(f32,actor+0x344);x=R_HIT(f32,actor+0x33C);}
    else {
        const f32 tx=R_HIT(f32,target+0x314),ax=R_HIT(f32,actor+0x314),az=R_HIT(f32,actor+0x31C);
        x=tx-ax;z=R_HIT(f32,target+0x31C)-az;
    }
    const s32 angle=randomizer_arrow_atan(-x,-z);
    const u32 object=R_HIT(u32,info);
    R_HIT(u16,info+0xE)=(u16)angle;
    const u32 flags=R_HIT(u32,object+0x10);
    actor=R_HIT(u32,info+4);
    if((flags&0x8000) && (R_HIT(u32,target+0x2E0)&0x380000))R_HIT(u8,info+8)=0;
    if(actor && R_HIT(s16,actor+8)==446 && R_HIT(s8,target+0x3A9)!=0) {
        const s8 left=R_HIT(s8,target+0x3A9);
        const s32 room=R_HIT(s8,target+0x326);
        const u32 bit=R_HIT(u8,target+0x3A8),table=R_HIT(u32,target+0x3A4);
        R_HIT(u8,target+0x3A9)=(u8)(left-1);
        randomizer_hit_steal(target+0x314,table,room,0,bit);
        R_HIT(u8,target+0x3A8)++;
        R_HIT(u8,info+8)=0;
    } else {
        const u8 damage=R_HIT(u8,info+8);
        s8 health=R_HIT(s8,target+0x3A1);
        if((s8)damage>0) {
            health=(s8)(health-damage);R_HIT(s8,target+0x3A1)=health;
            const u8 cut=R_HIT(u8,player+0x3AC);
            if((cut==8 || cut==9) && R_HIT(u8,player+0x69E8)) {
                health=(s8)(health-R_HIT(u8,info+8));R_HIT(s8,target+0x3A1)=health;
            }
        }
    }
    s8 pause=6;
    s32 update_pause=1,normal_ganondorf=0;
    const s32 dead_health=R_HIT(s8,target+0x3A1)<=0;
    if(dead_health) {
        R_HIT(u8,info+9)=1;
        /* The original dead marker stays set before upstream's inserted gate. */
        if(target+0x37C && R_HIT(u16,target+8)==0xF6)normal_ganondorf=1;
        else {
            randomizer_hit_sound(target,0x2828,0);
            pause=(s8)(WWHD_GAME_DATA(0x1047B696,s16)+6);
            const u32 pos=R_HIT(u32,info+0x14);
            if(pos) {
                randomizer_arrow_particle(R_HIT(u32,wwhd_dComIfG_getGameInfo()+0x5AB0),0,0x10,pos,0,0,255,0,-1,0,0,0);
                s16 rotation[3];f32 scale[3];const f32 value=WWHD_GAME_DATA(0x1004B468,f32);
                rotation[2]=0;scale[0]=value;scale[2]=value;rotation[0]=0;scale[1]=value;
                const u32 current_player=R_HIT(u32,wwhd_dComIfG_getGameInfo()+0x5B2C);
                rotation[1]=(s16)randomizer_hit_player_angle(target,current_player);
                const u32 current_pos=R_HIT(u32,info+0x14);
                randomizer_arrow_particle(R_HIT(u32,wwhd_dComIfG_getGameInfo()+0x5AB0),0,0xD,current_pos,(u32)rotation,(u32)scale,255,0,-1,0,0,0);
            }
        }
    } else normal_ganondorf=0;
    if(normal_ganondorf || !dead_health) {
        u32 arg=normal_ganondorf?0x20:(target && R_HIT(s16,target+8)==190?0x33:0x20);
        if(!normal_ganondorf && R_HIT(u8,info+9)) {
            const u32 object_info=randomizer_hit_object_info(R_HIT(u32,info));
            randomizer_hit_sound(target,randomizer_hit_critical_sound(object_info),arg);
            if(R_HIT(u8,info+0xA)==9)update_pause=0;
            else pause=(s8)(WWHD_GAME_DATA(0x1047B694,s16)+4);
        } else {
            u32 id;
            if(!normal_ganondorf && target && R_HIT(s16,target+8)==216 && R_HIT(u8,target+0x570)==2) {id=0x693A;arg=0;}
            else id=randomizer_hit_sound_id(R_HIT(u32,info));
            randomizer_hit_sound(target,id,arg);
            pause=(s8)(WWHD_GAME_DATA(0x1047B692,s16)+1);
        }
    }
    if(update_pause && R_HIT(u8,info+0xA)==1)WWHD_GAME_DATA(0x101EACB7,u8)=(u8)pause;
    if(R_HIT(u8,info+8))randomizer_hit_music(R_HIT(u32,info+0x18));
    return R_HIT(u32,info+4);
}
