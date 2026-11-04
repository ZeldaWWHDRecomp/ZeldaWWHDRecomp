/* Complete CC0 a874e1a9 heart-alarm and resting-animation consumers.
 * MIT remove_low_health_effects is enabled only for seeds starting below8
 * quarter hearts. Keep the alarm's existing urgent <=2-health branch. */
#pragma once
#ifndef RLH_GUARD
#define RLH_GUARD(actor) (((s32 (*)(u32))(*(volatile u32*)(*(volatile u32*)((actor)+0xB4)+0x3C)))(actor))
#endif
WWHD_GAME_FUNC(0x025200D4,u32,rlh_game,(void));
WWHD_GAME_FUNC(0x027200D0,u32,rlh_options,(u32 save_options));
WWHD_GAME_FUNC(0x0271FC5C,s32,rlh_alarm_disabled,(u32 options));
WWHD_GAME_FUNC(0x0201EBA0,u32,rlh_sound,(u32 audio,u32 id,u32 position,u32 category,s32 flags,f32 volume,f32 pitch,f32 pan,f32 fx,u32 extra));
static u32 rlh_save(void){return WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);}
WWHD_REPLACE(0x02024334,void,randomizer_heart_alarm,(u32 audio)) {
    if(*(volatile u8*)(rlh_game()+0x5292))return;
    u8 counter=*(volatile u8*)(audio+0x50);
    if(!counter)return;
    if(*(volatile s32*)(audio+0x44)!=0 && !*(volatile u8*)(audio+0x276)) {
        if(!rlh_alarm_disabled(rlh_options(rlh_save()+0x12C0))) {
            const s32 health=*(volatile s32*)(audio+0x44);
            u32 id=0;
            if(health<=2)id=0xD2;
            else if(health<=4) {
                if(init_quarter_hearts>=8 || *(volatile u16*)(rlh_save()+0x20)>=8)id=0xD1;
            } else if(health<=6)id=0xD0;
            if(id) {
                const f32 minus_one=WWHD_GAME_DATA(0x100038D0,f32);
                const f32 one=WWHD_GAME_DATA(0x10003894,f32);
                (void)rlh_sound(audio,id,0,0,0,one,one,minus_one,minus_one,0);
            }
        }
        counter=*(volatile u8*)(audio+0x50);
    }
    *(volatile u8*)(audio+0x50)=(u8)(counter-1);
}
WWHD_REPLACE(0x023DD12C,s32,randomizer_rest_health_animation,(u32 actor)) {
    if(RLH_GUARD(actor) || *(volatile u16*)(actor+0x5888)!=0xFFFF ||
       *(volatile u32*)(actor+0x65B0)!=0)return 0;
    s32 eligible=0;
    if(!*(volatile u16*)(actor+0x420) && !(*(volatile u32*)(actor+0x6A70)&0x2000))
        eligible=*(volatile u16*)(rlh_save()+0x22)<=6;
    if(!eligible && *(volatile s32*)(actor+0x430)!=0x12)return 0;
    if(init_quarter_hearts>=8)return 1;
    /* Read the actual save allocation for either eligibility branch; a demo
     * branch need not have executed the earlier current-health load. */
    const u32 save=rlh_save();
    const u32 maximum=*(volatile u16*)(save+0x20)&~3u;
    const u32 health=*(volatile u16*)(save+0x22);
    return (s32)(maximum-health)>=2;
}
