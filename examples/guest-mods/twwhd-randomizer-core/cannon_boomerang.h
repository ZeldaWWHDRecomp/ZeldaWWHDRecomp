/* Complete CC0 a874e1a9 daObj_Canon_c::checkTgHit02330A98.
 * MIT misc_rando_features changes only boomerang damage from1 to2. */
#pragma once
#define RCB(t,a) (*(volatile t*)(a))
WWHD_GAME_FUNC(0x02515E50,void,rcb_status,(u32 status));
WWHD_GAME_FUNC(0x02330178,void,rcb_mode,(u32 actor,s32 proc,s32 mode));
WWHD_GAME_FUNC(0x0211D2F8,s32,rcb_timer,(u32 timer));
WWHD_GAME_FUNC(0x02516300,u32,rcb_target,(u32 sphere));
WWHD_GAME_FUNC(0x02520540,s32,rcb_reverb,(s32 room));
WWHD_GAME_FUNC(0x025E1A40,void,rcb_sound,(u32 sound,u32 position,u32 parameter,s32 reverb));
WWHD_GAME_FUNC(0x025200D4,u32,rcb_game,(void));
WWHD_GAME_FUNC(0x025A847C,u32,rcb_particle,(u32 control,s32 group,u32 id,u32 position,u32 angle,u32 scale,s32 alpha,u32 callback,s32 setup,u32 color,u32 env,u32 scale2));
static void rcb_effect(u32 id,u32 position,u32 angle,u32 scale) {
    const u32 control=RCB(u32,rcb_game()+0x5AB0);
    (void)rcb_particle(control,0,id,position,angle,scale,255,0,-1,0,0,0);
}
static void rcb_audio(u32 actor,u32 sound,u32 parameter) {
    const s32 reverb=rcb_reverb(RCB(s8,actor+0x326));
    rcb_sound(sound,actor+0x37C,parameter,reverb);
}
WWHD_REPLACE(0x02330A98,s32,randomizer_cannon_hit,(u32 actor)) {
    rcb_status(actor+0x53C);
    if(WWHD_GAME_DATA(0x10469465,u8)) {
        RCB(u8,actor+0x3A1)=0;rcb_mode(actor,0,2);return 1;
    }
    if(rcb_timer(actor+0x55C))return 0;
    const u32 target=rcb_target(actor+0x3F4);
    unsigned int position[3];
    position[2]=RCB(unsigned int,actor+0x4C8);
    position[0]=RCB(unsigned int,actor+0x4C0);
    position[1]=RCB(unsigned int,actor+0x4C4);
    (void)rcb_target(actor+0x3F4);
    if(!target)return 0;
    const u32 type=RCB(u32,target+0x10);
    if(type==0x20) {
        const u8 health=RCB(u8,actor+0x3A1);
        RCB(s32,actor+0x55C)=5;RCB(u8,actor+0x3A1)=(u8)(health-2u);
    } else if(type==0x40) {
        RCB(u16,actor+0x586)=0x190;
        rcb_audio(actor,0x2833,0x20);
        const u8 health=RCB(u8,actor+0x3A1);
        RCB(s32,actor+0x55C)=5;RCB(u8,actor+0x3A1)=(u8)(health-2u);
    } else return 0;
    rcb_effect(0x10,(u32)position,0,0);
    if(RCB(s8,actor+0x3A1)>0) {rcb_mode(actor,0,0);return 1;}
    const u32 player=RCB(u32,rcb_game()+0x5B2C);
    const f32 two=WWHD_GAME_DATA(0x10026BA0,f32);
    f32 scale[3]={two,two,two};
    rcb_effect(0xF,(u32)position,player+0x328,(u32)scale);
    rcb_audio(actor,0x2828,0);
    rcb_mode(actor,0,2);return 1;
}
