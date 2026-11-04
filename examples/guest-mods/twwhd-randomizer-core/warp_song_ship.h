/* Full public CC0 a874e1a9 d_a_ship.cpp procTactWarp_init/procTactWarp.
 * MIT remove_cutscenes removes the warp sound call and altitude branch only;
 * its preceding room-reverb query and all other callbacks remain in order. */
#pragma once
#ifndef R_WARP_SHIP
#define R_WARP_SHIP(t,a) (*(volatile t*)(a))
#endif
WWHD_GAME_FUNC(0x0247BA38,void,warp_ship_part_off,(u32 self));
WWHD_GAME_FUNC(0x02520540,s32,warp_ship_reverb,(s32 room));
WWHD_GAME_FUNC(0x024F8044,u32,warp_ship_camera,(void));
WWHD_GAME_FUNC(0x0253E70C,void,warp_ship_camera_start,(u32 camera,u32 mode,u32 actor,u32 flags));
WWHD_GAME_FUNC(0x025602F0,void,warp_ship_colors,(u32 ambient,u32 diffuse));
WWHD_GAME_FUNC(0x025A847C,u32,warp_ship_particle,(u32 control,u32 group,u32 id,u32 pos,u32 angle,u32 scale,u32 alpha,u32 callback,s32 setup,u32 primary,u32 environment,u32 scale2d));
WWHD_GAME_FUNC(0x025D5218,u32,warp_ship_judge,(u32 judge,u32 data));
WWHD_GAME_FUNC(0x025DD868,s32,warp_ship_creating,(u32 id));
WWHD_GAME_FUNC(0x025A5AC8,void,warp_ship_follow_end,(u32 callback));
WWHD_GAME_FUNC(0x0247BEFC,s32,warp_ship_paddle,(u32 self));
WWHD_GAME_FUNC(0x0253E860,void,warp_ship_camera_end,(u32 camera,u32 actor));
WWHD_GAME_FUNC(0x0200F564,s32,warp_ship_chase_angle,(u32 angle,s32 target,s32 step));
WWHD_GAME_FUNC(0x0200F5C8,s32,warp_ship_chase_float,(u32 value,f32 target,f32 step));
WWHD_GAME_FUNC(0x025C3748,s32,warp_ship_scene,(s32 exit,f32 speed,u32 mode,s32 room));
WWHD_GAME_FUNC(0x025E1E88,void,warp_ship_mute_off,(void));
static f32 warp_ship_joint_position(u32 tornado,s32 joint,u32 axis){
    const u32 model=R_WARP_SHIP(u32,tornado+0x3B4);
    if(model){
        const u32 block=R_WARP_SHIP(u32,model+0x2C);
        R_WARP_SHIP(u16,block+4)|=0x10;
        return R_WARP_SHIP(f32,R_WARP_SHIP(u32,block+0x10)+(u32)joint*0x30+axis*16+12);
    }
    return R_WARP_SHIP(f32,tornado+0x314+axis*4);
}
static s32 warp_ship_joint_above(u32 tornado,u32 y){
    /* The native modeled path exits on unordered (BGE falls through), but the
     * null-model fallback exits only on ordered LT (02482670/02482684). */
    u32 model=R_WARP_SHIP(u32,tornado+0x3B4);
    s32 i=0;
    for(;i<11;i++){
        if(model){
            const u32 block=R_WARP_SHIP(u32,model+0x2C);
            R_WARP_SHIP(u16,block+4)|=0x10;
            const f32 height=R_WARP_SHIP(f32,y);
            const f32 joint_y=R_WARP_SHIP(f32,R_WARP_SHIP(u32,block+0x10)+(u32)i*0x30+0x1C);
            model=R_WARP_SHIP(u32,tornado+0x3B4);
            if(!(height>=joint_y))break;
        }else if(R_WARP_SHIP(f32,y)<R_WARP_SHIP(f32,tornado+0x318))break;
    }
    return i==11?10:i;
}
WWHD_REPLACE(0x0247DBF8,s32,randomizer_ship_warp_init,(u32 self)){
    R_WARP_SHIP(u8,self+0x635)=14;
    R_WARP_SHIP(s16,self+0xD190)=0;R_WARP_SHIP(s16,self+0xD192)=-1;R_WARP_SHIP(u32,self+0xD194)=0x02482438;
    if(R_WARP_SHIP(u16,self+0x67A)!=10)warp_ship_part_off(self);
    R_WARP_SHIP(s16,self+0x68E)=0;R_WARP_SHIP(f32,self+0x374)=0;R_WARP_SHIP(f32,self+0x340)=0;
    R_WARP_SHIP(u32,self+0x644)|=1;R_WARP_SHIP(s16,self+0x662)=0;
    if(self&&(u32)(self+0x314)){
        (void)warp_ship_reverb(R_WARP_SHIP(s8,self+0x326));
        /* Upstream nops only025E1A40, leaving the native reverb query. */
    }
    const u32 camera=warp_ship_camera(),actor=self?R_WARP_SHIP(u32,self+4):0xFFFFFFFFu;
    warp_ship_camera_start(camera,14,actor,0);
    u32 ambient,diffuse;warp_ship_colors((u32)&ambient,(u32)&diffuse);
    R_WARP_SHIP(s16,self+0x6A6)=0;R_WARP_SHIP(s16,self+0x6A2)=0;
    R_WARP_SHIP(s16,self+0x6A4)=(s16)((u16)R_WARP_SHIP(s16,self+0x32A)+0x8000u);
    u32 info=wwhd_dComIfG_getGameInfo();
    (void)warp_ship_particle(R_WARP_SHIP(u32,info+0x5AB0),0,0x285,self+0x314,self+0x328,0,255,self+0xD140,-1,(u32)&ambient,0,0);
    info=wwhd_dComIfG_getGameInfo();
    (void)warp_ship_particle(R_WARP_SHIP(u32,info+0x5AB0),0,0x285,self+0x314,self+0x6A2,0,255,self+0xD154,-1,(u32)&ambient,0,0);
    return 1;
}
WWHD_REPLACE(0x02482438,s32,randomizer_ship_warp,(u32 self)){
    R_WARP_SHIP(s16,self+0x32A)=(s16)((u16)R_WARP_SHIP(s16,self+0x32A)+(u16)R_WARP_SHIP(s16,self+0x68E));
    R_WARP_SHIP(s16,self+0x322)=R_WARP_SHIP(s16,self+0x32A);
    u32 id=R_WARP_SHIP(u32,self+0x714),key=id;
    const u32 tornado=id==0xFFFFFFFFu?0:warp_ship_judge(0x025E1234,(u32)&key);
    if(!tornado||warp_ship_creating(R_WARP_SHIP(u32,self+0x714))){
        if(R_WARP_SHIP(u32,self+0x714)==0xFFFFFFFFu){
            const u32 info=wwhd_dComIfG_getGameInfo();R_WARP_SHIP(u16,info+0x52B8)|=8;
            warp_ship_follow_end(self+0xD140);warp_ship_follow_end(self+0xD154);(void)warp_ship_paddle(self);
            const u32 camera=warp_ship_camera(),actor=self?R_WARP_SHIP(u32,self+4):0xFFFFFFFFu;
            warp_ship_camera_end(camera,actor);
        }
        return 0;
    }
    if(warp_ship_chase_angle(self+0x68E,0x1C25,0x40)&&!(R_WARP_SHIP(f32,tornado+0x744)<=0.8f)){
        f32 vy=R_WARP_SHIP(f32,self+0x340)+1.0f;if(!(vy<=50))vy=50;
        R_WARP_SHIP(f32,self+0x340)=vy;R_WARP_SHIP(f32,self+0x318)+=vy;
        R_WARP_SHIP(f32,tornado+0x318)=R_WARP_SHIP(f32,self+0x318)-700;
        warp_ship_follow_end(self+0xD140);warp_ship_follow_end(self+0xD154);
    }else if(!R_WARP_SHIP(s16,self+0x662)&&R_WARP_SHIP(s16,self+0x68E)>0x1000){
        R_WARP_SHIP(s16,self+0x662)=1;R_WARP_SHIP(s16,tornado+0x6B0)=1;
    }
    if(R_WARP_SHIP(s16,self+0x662)==1){
        (void)warp_ship_chase_float(tornado+0x318,R_WARP_SHIP(f32,self+0x318)-700,20);
        const s32 joint=warp_ship_joint_above(tornado,self+0x318);
        (void)warp_ship_chase_float(self+0x314,warp_ship_joint_position(tornado,joint,0),50);
        (void)warp_ship_chase_float(self+0x31C,warp_ship_joint_position(tornado,joint,2),50);
    }
    if(R_WARP_SHIP(s16,self+0x662)!=2){
        /* The original arithmetic/comparison still executes with its branch
         * nopped; retain its loads and FP comparison without gating the warp. */
        volatile s32 above=R_WARP_SHIP(f32,self+0x318)>R_WARP_SHIP(f32,self+0x6D8)+5000.0f;(void)above;
        R_WARP_SHIP(s16,self+0x662)=2;
        (void)warp_ship_scene((s32)((u32)R_WARP_SHIP(s32,self+0x6AC)+0xC5u),0,0,-1);warp_ship_mute_off();
    }
    R_WARP_SHIP(s16,self+0x6A4)=(s16)((u16)R_WARP_SHIP(s16,self+0x32A)+0x8000u);
    return 1;
}
