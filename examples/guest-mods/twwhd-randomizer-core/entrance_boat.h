/* Missing-boat spawn5 fallback: upstream flexible_entrances.asm (MIT).
 * Source fallback/common tail: public CC0 d_a_player_main_04b.cpp a874e1a9,
 * with native field/call contracts checked against the canonical USA build. */
#pragma once
#ifndef R_BOAT_U8
#define R_BOAT_U8(a,o) (*(volatile u8*)((a)+(o)))
#endif
#ifndef R_BOAT_U16
#define R_BOAT_U16(a,o) (*(volatile u16*)((a)+(o)))
#endif
#ifndef R_BOAT_U32
#define R_BOAT_U32(a,o) (*(volatile u32*)((a)+(o)))
#endif
#ifndef R_BOAT_F32
#define R_BOAT_F32(a,o) (*(volatile f32*)((a)+(o)))
#endif
static u32 randomizer_bgwait_actor,randomizer_bgwait_late,randomizer_bgwait_mode,randomizer_bgwait_spawn;
WWHD_GAME_ORIGINAL(0x024131D0,s32,randomizer_native_bgwait,(u32 actor));
WWHD_GAME_FUNC(0x023F8F80,s32,randomizer_boat_change_swim,(u32 actor));
WWHD_GAME_FUNC(0x023F8B00,s32,randomizer_boat_swim_wait,(u32 actor,s32 keep));
WWHD_GAME_FUNC(0x02412E58,s32,randomizer_boat_swim_move,(u32 actor,s32 keep));
WWHD_GAME_FUNC(0x023E2FF4,s32,randomizer_boat_wait,(u32 actor));
WWHD_GAME_FUNC(0x023F1154,s32,randomizer_boat_move,(u32 actor));
WWHD_GAME_FUNC(0x023F6020,s32,randomizer_boat_damage_up,(u32 actor,s32 type,s32 large,s16 root_x,s16 root_z));
WWHD_GAME_FUNC(0x025E1928,void,randomizer_boat_sub_bgm_stop,(void));
WWHD_GAME_FUNC(0x025E3F3C,void,randomizer_boat_old_frame,(u32 data,f32 morph,u32 first,u32 last));
WWHD_GAME_FUNC(0x023FF6A0,void,randomizer_boat_world_matrix,(u32 actor));
WWHD_GAME_FUNC(0x02402FFC,void,randomizer_boat_hat,(u32 actor));
WWHD_GAME_FUNC(0x023DD054,void,randomizer_boat_anime,(u32 actor));
WWHD_GAME_FUNC(0x023FC06C,void,randomizer_boat_ratio,(u32 actor));
WWHD_GAME_FUNC(0x027F4D5C,void,randomizer_boat_model_calc,(u32 model));
WWHD_GAME_FUNC(0x02403C24,s32,randomizer_boat_item_model,(u32 actor));
WWHD_GAME_FUNC(0x020182E0,void,randomizer_boat_cylinder_center,(u32 cylinder,u32 position));
WWHD_GAME_FUNC(0x0240A0A4,void,randomizer_boat_attention,(u32 actor));
WWHD_GAME_FUNC(0x025200D4,u32,randomizer_boat_play,(void));
WWHD_GAME_FUNC(0x024EF0BC,u32,randomizer_boat_ground_code,(u32 background,u32 polygon));
WWHD_GAME_FUNC(0x023E9D50,s32,randomizer_boat_dragon_shield,(u32 actor,s32 enabled));
/* All four zero-return ground/collision failures precede this final helper.
 * The only native zero return after it is the absent-ship branch. No native
 * prelude is retried and no ownership, stage or scene-mode query is overridden. */
WWHD_HOOK_RETURN(0x023FE224,randomizer_bgwait_water_ready,(u32 actor)) {
    if(actor && actor==randomizer_bgwait_actor)randomizer_bgwait_late=1;
}
WWHD_HOOK_RETURN(0x023FE960,randomizer_bgwait_room_ready,(u32 actor)) {
    if(actor && actor==randomizer_bgwait_actor && randomizer_bgwait_late==1) {
        randomizer_bgwait_late=2;
        randomizer_bgwait_spawn=(R_BOAT_U32(actor,0xB0)>>12)&15;
        const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        randomizer_bgwait_mode=R_BOAT_U8(save,0x1177);
    }
}
static void randomizer_boat_swim_or_wait(u32 actor) {
    if(randomizer_boat_change_swim(actor)) {
        (void)randomizer_boat_swim_wait(actor,0);R_BOAT_U8(actor,0x68DE)=0;
    } else (void)randomizer_boat_wait(actor);
}
static void randomizer_boat_copy_head_matrix(u32 root,u32 model) {
    const u32 block=R_BOAT_U32(root,0x2C);
    R_BOAT_U16(block,4)|=0x10;
    const u32 matrix=R_BOAT_U32(block,0x10)+15*0x30;
    /* Native lfs/stfs pairs contain no arithmetic and preserve all payload
     * bits. Snapshot every word before stores, including overlapping matrices. */
    u32 values[12];for(u32 i=0;i<12;i++)values[i]=R_BOAT_U32(matrix,4*i);
    for(u32 i=0;i<12;i++)R_BOAT_U32(model,0xC8+4*i)=values[i];
}
static s32 randomizer_boat_fallback(u32 actor,u32 mode) {
    if(mode==5) {
        if(randomizer_boat_change_swim(actor)) {
            (void)randomizer_boat_swim_wait(actor,0);R_BOAT_U8(actor,0x68DE)=0;
        } else {
            R_BOAT_F32(actor,0x318)=R_BOAT_F32(actor,0x8A0);
            R_BOAT_U16(actor,0x420)=4;
            randomizer_boat_sub_bgm_stop();
            (void)randomizer_boat_damage_up(actor,-2,1,0,0);
        }
    } else if(R_BOAT_U32(actor,0x69E0)==0xFF) {
        const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        R_BOAT_U16(actor,0x422)=R_BOAT_U16(actor,0x322);
        R_BOAT_U16(actor,0x424)=0x23;R_BOAT_U16(actor,0x420)=4;
        R_BOAT_U32(actor,0x430)=14;
        R_BOAT_F32(actor,0x6A14)=R_BOAT_F32(save,0x1170);
        if(randomizer_boat_change_swim(actor)) {
            R_BOAT_F32(actor,0x370)=R_BOAT_F32(actor,0x6A14);
            (void)randomizer_boat_swim_move(actor,0);
        } else {
            f32 speed=R_BOAT_F32(actor,0x6A14);
            if(speed>17.0f){speed=17.0f;R_BOAT_F32(actor,0x6A14)=speed;}
            R_BOAT_F32(actor,0x370)=speed;
            (void)randomizer_boat_move(actor);
        }
    } else randomizer_boat_swim_or_wait(actor);
    randomizer_boat_old_frame(R_BOAT_U32(actor,0x65CC),0.0f,0,0x2A);
    randomizer_boat_world_matrix(actor);randomizer_boat_hat(actor);
    randomizer_boat_anime(actor);randomizer_boat_ratio(actor);
    randomizer_boat_model_calc(R_BOAT_U32(actor,0x448));
    randomizer_boat_copy_head_matrix(R_BOAT_U32(actor,0x448),R_BOAT_U32(actor,0x44C));
    randomizer_boat_model_calc(R_BOAT_U32(actor,0x44C));
    randomizer_boat_copy_head_matrix(R_BOAT_U32(actor,0x448),R_BOAT_U32(actor,0x450));
    randomizer_boat_model_calc(R_BOAT_U32(actor,0x450));
    (void)randomizer_boat_item_model(actor);
    randomizer_boat_cylinder_center(actor+0x7774,actor+0x314);
    randomizer_boat_attention(actor);
    const u32 code=randomizer_boat_ground_code(randomizer_boat_play()+0x12A0,actor+0x8F4);
    R_BOAT_U32(actor,0x69D0)=code;R_BOAT_U32(actor,0x69CC)=code;
    /* A missing boat for start5 reaches this tail only in modes1,5,6; the
     * native mode4 particle branch is therefore unreachable here. */
    u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    u32 flags=R_BOAT_U32(save,0x1174);
    if(flags&0x8000)(void)randomizer_boat_dragon_shield(actor,0);
    else R_BOAT_U16(actor,0x699E)=(u16)(flags>>16);
    save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    if(R_BOAT_U32(save,0x1174)&0x4000)R_BOAT_U32(actor,0x3BC)|=0x8000;
    for(u32 i=0;i<3;i++)R_BOAT_U32(0x1046CD48,4*i)=R_BOAT_U32(actor,0x314+4*i);
    for(u32 i=0;i<3;i++)R_BOAT_U16(0x1046CD10,2*i)=R_BOAT_U16(actor,0x328+2*i);
    for(u32 i=0;i<3;i++)R_BOAT_U16(0x1046CD08,2*i)=R_BOAT_U16(actor,0x320+2*i);
    return 2;
}
WWHD_REPLACE(0x024131D0,s32,randomizer_bgwait,(u32 actor)) {
    const u32 previous_actor=randomizer_bgwait_actor,previous_late=randomizer_bgwait_late;
    const u32 previous_mode=randomizer_bgwait_mode,previous_spawn=randomizer_bgwait_spawn;
    randomizer_bgwait_actor=actor;randomizer_bgwait_late=0;
    const s32 result=randomizer_native_bgwait(actor);
    const u32 fallback=result==0 && randomizer_bgwait_late==2 && randomizer_bgwait_spawn==5 &&
        (randomizer_bgwait_mode==1||randomizer_bgwait_mode==5||randomizer_bgwait_mode==6);
    const u32 mode=randomizer_bgwait_mode;
    s32 final_result=result;
    if(fallback){
        randomizer_bgwait_actor=0;randomizer_bgwait_late=0;
        final_result=randomizer_boat_fallback(actor,mode);
    }
    randomizer_bgwait_actor=previous_actor;randomizer_bgwait_late=previous_late;
    randomizer_bgwait_mode=previous_mode;randomizer_bgwait_spawn=previous_spawn;
    return final_result;
}
#undef R_BOAT_U8
#undef R_BOAT_U16
#undef R_BOAT_U32
#undef R_BOAT_F32
