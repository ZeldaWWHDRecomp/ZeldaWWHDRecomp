/* Complete CC0 daMo2_Execute021C9B1C, source a874e1a9.
 * MIT make_game_nonlinear skips sword collection021CA1A8..021CA210. */
#pragma once
#include "moblin_execute_effects.h"
#include "moblin_execute_damage.h"
WWHD_GAME_FUNC(0x025B9E38,void,randomizer_mo2_switch_on,(u32 save,s32 index,s32 room));
WWHD_GAME_FUNC(0x025BA5D4,void,randomizer_mo2_actor_on,(u32 save,s32 index,s32 room));
WWHD_GAME_FUNC(0x020402C8,s32,randomizer_mo2_ice,(u32 ice));
WWHD_GAME_FUNC(0x025E535C,s32,randomizer_mo2_morf_play,(u32 morf,u32 position,u32 sound,s32 reverb));
WWHD_GAME_FUNC(0x025E55A0,void,randomizer_mo2_morf_calc,(u32 morf));
WWHD_GAME_FUNC(0x02555D0C,u32,randomizer_mo2_light,(void));
WWHD_GAME_FUNC(0x025626A4,void,randomizer_mo2_tev,(u32 light,s32 type,u32 position,u32 tev));
WWHD_GAME_FUNC(0x02041570,void,randomizer_mo2_fire,(u32 fire));
WWHD_GAME_FUNC(0x0252012C,void,randomizer_mo2_next_stage,(u32 name,s32 spawn,s32 room,s32 layer,f32 speed,u32 mode,s32 wipe,s32 argument));
WWHD_GAME_FUNC(0x025E1AE0,void,randomizer_mo2_sound_stop,(u32 id,s32 frames));
WWHD_GAME_FUNC(0x020078BC,s32,randomizer_mo2_trigger_b,(s32 port));
WWHD_GAME_FUNC(0x02007738,s32,randomizer_mo2_hold_y,(s32 port));
WWHD_GAME_FUNC(0x021D0A14,void,randomizer_mo2_move,(u32 actor));
WWHD_GAME_FUNC(0x02041F94,s32,randomizer_mo2_damage_reaction,(u32 reaction));
WWHD_GAME_FUNC(0x0200FC74,void,randomizer_mo2_scale,(f32 x,f32 y,f32 z,u32 concatenate));
WWHD_GAME_FUNC(0x020182E0,void,randomizer_mo2_cylinder_center,(u32 cylinder,u32 position));
WWHD_GAME_FUNC(0x02516C14,void,randomizer_mo2_mass,(u32 manager,u32 object,u32 mass));
WWHD_GAME_FUNC(0x02008E0C,void,randomizer_mo2_ground_construct,(u32 ground));
WWHD_GAME_FUNC(0x02008DAC,void,randomizer_mo2_ground_destroy,(u32 ground,s32 flags));
/* The native caller retains the complete F1 result across later ground calls. */
WWHD_GAME_FUNC(0x02008974,f64,randomizer_mo2_ground_cross,(u32 manager,u32 ground));
WWHD_GAME_FUNC(0x0200F428,void,randomizer_mo2_angle_calculate,(u32 value,s32 target,s32 ratio,s32 maximum));
/* J3DModel::setBaseTRMtx is an inline snapshot assignment, not a native
 * PSMTXCopy callback. Load all twelve words before writing any destination. */
static void randomizer_mo2_model_matrix(u32 destination,u32 source) {
    unsigned int snapshot[12];
    for(u32 i=0;i<12;i++)snapshot[i]=R_MO2(u32,source+4*i);
    for(u32 i=0;i<12;i++)R_MO2(u32,destination+4*i)=snapshot[i];
}
WWHD_REPLACE(0x021C9B1C,s32,randomizer_moblin_execute,(u32 actor)) {
    const u32 player=R_MO2(u32,randomizer_mo2_game()+0x5B2C);
    if(R_MO2(u8,actor+0x3DC)!=0 && R_MO2(f32,actor+0x318)<R_MO2(f32,actor+0x2F0)-5000.0f) {
        randomizer_mo2_switch_on(randomizer_daily_save(),R_MO2(u8,actor+0x3DC),R_MO2(s8,actor+0x326));
        randomizer_mo2_actor_on(randomizer_daily_save(),R_MO2(u16,actor+0x2D8),R_MO2(s8,actor+0x2FE));
        randomizer_mo2_delete(actor);
        if(R_MO2(s8,actor+0x10B0)!=0) {
            const u32 key=R_MO2(u32,actor+0x2D60);
            const u32 weapon=key==0xFFFFFFFFu?0:randomizer_mo2_judge(0x025E1234,(u32)&key);
            if(weapon)randomizer_mo2_delete(weapon);
        }
        return 1;
    }
    if(randomizer_mo2_ice(actor+0x2E54)) {
        R_MO2(u8,R_MO2(u32,actor+0x3E0)+0xA6)=0;
        R_MO2(f32,R_MO2(u32,actor+0x3E0)+0x98)=3.0f;
        (void)randomizer_mo2_morf_play(R_MO2(u32,actor+0x3E0),actor+0x37C,0,0);
        randomizer_mo2_model_matrix(R_MO2(u32,R_MO2(u32,actor+0x3E0)+0x90)+0xC8,0x1048D0CC);
        randomizer_mo2_morf_calc(R_MO2(u32,actor+0x3E0));
        return 1;
    }
    randomizer_mo2_tev(randomizer_mo2_light(),0,actor+0x314,actor+0x110);
    randomizer_mo2_fire(actor+0x320C);
    if(R_MO2(u8,actor+0x2E4F)!=0 || R_MO2(s16,actor+0x2E50)==1) {
        randomizer_mo2_next_stage(0x10015118,0,0,-1,0.0f,0,1,0);return 1;
    }
    if(R_MO2(s16,actor+0x2E50)!=0) {
        R_MO2(s16,actor+0x2E50)--;
        if(R_MO2(s16,actor+0x2E50)==30)randomizer_mo2_sound_stop(0x834,30);
    }
    if(R_MO2(u8,actor+0x2E4E)!=0) {
        if(R_MO2(u16,actor+0xF8)==2)R_MO2(u8,actor+0x2E4E)=0;
        else {randomizer_mo2_order_event(actor,2,0xFFFF,0);R_MO2(u16,actor+0xFA)|=2;}
    }
    if(R_MO2(s16,actor+0x2E4C)!=0) {
        R_MO2(s16,actor+0x2E4C)--;
        if(R_MO2(s16,actor+0x2E4C)==0) {
            if(WWHD_GAME_DATA(0x101BA693,s8)==0)R_MO2(s8,actor+0x2E21)=1;
            R_MO2(s16,actor+0x8C2)=40;R_MO2(s16,actor+0x8C0)=0;
        }
    }
    if(R_MO2(s16,actor+0x8C2)==0 && R_MO2(f32,player+0x3CC)<0.0f) {
        WWHD_GAME_DATA(0x10465508,f32)=R_MO2(s16,actor+0x8C0)<2?
            WWHD_GAME_DATA(0x1047B638,f32)+800.0f:WWHD_GAME_DATA(0x1047B63C,f32)+1200.0f;
        WWHD_GAME_DATA(0x10465514,s16)=0x1932;
        WWHD_GAME_DATA(0x1046551C,f32)=340.0f;
        WWHD_GAME_DATA(0x10465524,f32)=340.0f;
        WWHD_GAME_DATA(0x10465528,f32)=-200.0f;
    }
    u8 damage=0;
    if(WWHD_GAME_DATA(0x104654E2,u8)==0 || randomizer_mo2_trigger_b(0) || randomizer_mo2_hold_y(0)) {
        R_MO2(u32,actor+0x840)++;
        for(u32 i=0;i<5;i++)if(R_MO2(s16,actor+0x848+i*2)!=0)R_MO2(s16,actor+0x848+i*2)--;
        if(R_MO2(s16,actor+0x85E)!=0)R_MO2(s16,actor+0x85E)--;
        if(R_MO2(s16,actor+0x860)!=0)R_MO2(s16,actor+0x860)--;
        if(R_MO2(s16,actor+0x852)!=0)R_MO2(s16,actor+0x852)--;
        if(R_MO2(s16,actor+0x45E)!=0)R_MO2(s16,actor+0x45E)--;
        if(R_MO2(s16,actor+0x854)!=0) {
            R_MO2(s16,actor+0x854)--;
            R_MO2(s16,actor+0x898)=(s16)(WWHD_GAME_DATA(0x10465500,s16)+4);
            R_MO2(u8,actor+0x89A)=4;
        }
        if(R_MO2(s16,actor+0xD7A)!=0)R_MO2(s16,actor+0xD7A)--;
        if(R_MO2(s16,actor+0x858)!=0)R_MO2(s16,actor+0x858)--;
        if(R_MO2(s16,actor+0x85A)!=0)R_MO2(s16,actor+0x85A)--;
        WWHD_GAME_DATA(0x101BA692,u8)=0;
        if(R_MO2(u8,actor+0x3DD)!=0) {
            if(randomizer_mo2_switch(randomizer_daily_save(),R_MO2(u8,actor+0x3DD)-1,R_MO2(s8,actor+0x326))) {
                R_MO2(u8,actor+0x3DD)=0;R_MO2(u32,actor+0x39C)=4;
            } else {R_MO2(u32,actor+0x39C)=0;return 1;}
        } else R_MO2(u32,actor+0x39C)&=~0x10u;
        R_MO2(u32,actor+0x2E0)|=0x20;
        R_MO2(s32,actor+0x838)=0;R_MO2(s32,actor+0x83C)=0;
        randomizer_mo2_eye(actor);
        R_MO2(s32,actor+0xD28)=0;
        if(R_MO2(s16,actor+0x856)==0)
            (void)randomizer_mo2_morf_play(R_MO2(u32,actor+0x3E0),actor+0x37C,0,0);
        if(R_MO2(s16,actor+0x8C2)<30)randomizer_mo2_morf_calc(R_MO2(u32,actor+0x3E0));
        damage=randomizer_mo2_damage(actor);
        randomizer_mo2_move(actor);
        randomizer_mo2_demo_camera(actor);
        randomizer_mo2_ground_smoke(actor);
        if(R_MO2(s16,actor+0x856)!=0)R_MO2(s16,actor+0x856)--;
        R_MO2(u32,actor+0x8BC)=actor;R_MO2(s16,actor+0x8C4)=1;
    }
    if(R_MO2(s16,actor+0x8C2)!=31) {
        R_MO2(s16,actor+0x328)=R_MO2(s16,actor+0x320);
        R_MO2(s16,actor+0x32A)=R_MO2(s16,actor+0x322);
        R_MO2(s16,actor+0x32C)=R_MO2(s16,actor+0x324);
    }
    const s32 result=randomizer_mo2_damage_reaction(actor+0x8BC);
    if(result!=0) {
        R_MO2(s16,actor+0x898)=1;R_MO2(u32,actor+0x2D5C)=0xFFFFFFFFu;
        switch((u32)result) {
        case 1:
            randomizer_mo2_animation(actor,0x6A,2.0f,0,1.0f,0x31);
            if(R_MO2(s8,actor+0x10B0)!=0 && (R_MO2(s8,actor+0x3A1)<=0 || damage>=4 || randomizer_mo2_random(1.0f)<0.5f))
                R_MO2(s8,actor+0x10B1)=1;
            R_MO2(s16,actor+0x8C2)=0;break;
        case 2:randomizer_mo2_animation(actor,0x6B,2.0f,0,1.0f,0x32);break;
        case 5:
            if((R_MO2(u8,actor+0x3D8)!=1 || WWHD_GAME_DATA(0x101BA692,u8)==0) &&
               R_MO2(s16,actor+0x8C2)!=4 && R_MO2(s16,actor+0x8C2)!=11 && R_MO2(s16,actor+0x8C2)!=8 && R_MO2(s16,actor+0x84A)==0) {
                randomizer_mo2_wait(actor);R_MO2(s16,actor+0x8C2)=4;R_MO2(s16,actor+0x8C0)=0;R_MO2(s16,actor+0x84A)=30;
            }
            R_MO2(s16,actor+0x882)=5;R_MO2(s16,actor+0x890)=-0x4000;break;
        case 10:
            randomizer_mo2_wait(actor);R_MO2(s16,actor+0x8C0)=2;R_MO2(s16,actor+0x8C2)=0;randomizer_mo2_path_check(actor);break;
        case 20:
            R_MO2(s16,actor+0x882)=1;R_MO2(s16,actor+0x890)=-0x4000;R_MO2(u8,actor+0x89A)=1;
            R_MO2(s16,actor+0x898)=(s16)(WWHD_GAME_DATA(0x10465500,s16)+16);
            R_MO2(s16,actor+0xDBC)=WWHD_GAME_DATA(0x10465564,s16);break;
        case 21:
            R_MO2(s16,actor+0x882)=1;R_MO2(s16,actor+0x890)=0;R_MO2(s16,actor+0xDBC)=WWHD_GAME_DATA(0x10465564,s16);break;
        case 30:
            randomizer_mo2_animation(actor,0x6A,2.0f,0,1.0f,0x31);
            if(R_MO2(s8,actor+0x10B0)!=0)R_MO2(s8,actor+0x10B1)=1;
            R_MO2(s16,actor+0x8C2)=0;
            if(actor)randomizer_mo2_voice(actor,0x480E,0);break;
        }
    }
    const f32 scale=WWHD_GAME_DATA(0x104654EC,f32);
    const u32 model=R_MO2(u32,R_MO2(u32,actor+0x3E0)+0x90);
    randomizer_mo2_scale(scale,scale,scale,1);
    randomizer_mo2_model_matrix(model+0xC8,randomizer_mo2_matrix());
    if(R_MO2(s16,actor+0x8C2)>=30)randomizer_mo2_morf_calc(R_MO2(u32,actor+0x3E0));
    randomizer_mo2_fire(actor+0x320C);
    randomizer_mo2_throw_weapon(actor);
    f32 offset[3];offset[1]=0.0f;offset[0]=0.0f;
    randomizer_mo2_rotate_y(randomizer_mo2_matrix(),R_MO2(s16,actor+0x322));
    if(R_MO2(s16,actor+0x8C2)==23) {
        randomizer_mo2_translate(R_MO2(f32,actor+0x37C),R_MO2(f32,actor+0x318),R_MO2(f32,actor+0x384),0);
        offset[2]=WWHD_GAME_DATA(0x1047C05C,f32);
    } else {
        randomizer_mo2_translate(R_MO2(f32,actor+0x314),R_MO2(f32,actor+0x318),R_MO2(f32,actor+0x31C),0);
        offset[2]=R_MO2(s16,actor+0x8C2)==18?WWHD_GAME_DATA(0x1047BBC0,f32)-50.0f:35.0f;
    }
    f32 center[3];randomizer_mo2_transform((u32)offset,(u32)center);
    randomizer_mo2_cylinder_center(actor+0x25AC,(u32)center);
    randomizer_mo2_collision_add(randomizer_mo2_game()+0x26A4,actor+0x2494);
    randomizer_mo2_mass(randomizer_mo2_game()+0x4EF8,actor+0x2494,3);
    f32 head[3],body[3];
    f32 hx=R_MO2(f32,actor+0x2CD0),hy=R_MO2(f32,actor+0x2CD4),hz=R_MO2(f32,actor+0x2CD8);
    f32 bx=R_MO2(f32,actor+0x314),by=R_MO2(f32,actor+0x318),bz=R_MO2(f32,actor+0x31C);
    if(R_MO2(s16,actor+0x858)!=0){hy-=20000.0f;by-=20000.0f;}
    if(R_MO2(s16,actor+0x8C2)==10){by-=100.0f;hy-=20000.0f;}
    head[0]=hx;head[1]=hy;head[2]=hz;body[0]=bx;body[1]=by;body[2]=bz;
    randomizer_mo2_cylinder_center(actor+0x26DC,(u32)body);
    randomizer_mo2_collision_add(randomizer_mo2_game()+0x26A4,actor+0x25C4);
    randomizer_mo2_sphere_center(actor+0x280C,(u32)head);
    randomizer_mo2_collision_add(randomizer_mo2_game()+0x26A4,actor+0x26F4);
    if(R_MO2(s16,actor+0x882)!=0) {
        R_MO2(s16,actor+0x882)--;
        if(R_MO2(s16,actor+0x882)==0) {
            R_MO2(s16,actor+0x892)=R_MO2(s16,actor+0x322);
            randomizer_mo2_particle(R_MO2(u32,randomizer_mo2_game()+0x5AB0),0,0xE,actor+0x2CD0,actor+0x890,0,255,0,-1,0,0,0);
        }
    }
    if(R_MO2(u32,actor+0xE34)&0x20) {
        unsigned int ground[21];
        randomizer_mo2_ground_construct((u32)ground);
        u8* bytes=(u8*)ground;
        for(u32 i=0;i<7;i++)bytes[0x44+i]=0;
        ground[0x10/4]=0x10014DDC;ground[0x4C/4]=0x10014DFC;
        ground[0x20/4]=0x10014DEC;ground[0x40/4]=0x10014E0C;
        ground[1]=(unsigned int)((u32)ground+0x4C);ground[0]=(unsigned int)((u32)ground+0x40);ground[0x50/4]=1;
        s16 pitch=0x7FFF,roll=0x7FFF;
        const f32 distance=R_MO2(s16,actor+0xD70)!=0?100.0f:10.0f;
        f32 px=R_MO2(f32,actor+0x314),py=R_MO2(f32,actor+0x318)+(50.0f-R_MO2(f32,actor+0xD3C)),pz=R_MO2(f32,actor+0x31C);
        f32* position=(f32*)(bytes+0x24);position[0]=px;position[1]=py;position[2]=pz;
        const f64 base_height=randomizer_mo2_ground_cross(randomizer_mo2_game()+0x12A0,(u32)ground);
        if(base_height!=-1000000000.0) {
            const f32 tz=pz+distance,ty=(f32)(base_height+50.0);
            position[0]=px;position[2]=tz;position[1]=ty;
            f64 height=randomizer_mo2_ground_cross(randomizer_mo2_game()+0x12A0,(u32)ground);
            if(height!=-1000000000.0f) {
                pitch=(s16)-randomizer_mo2_atan((f32)(height-base_height),tz-pz);
                if(pitch>0x2000||pitch<-0x2000)pitch=0;
            }
            const f32 tx=px+distance;
            position[1]=ty;position[2]=pz;position[0]=tx;
            height=randomizer_mo2_ground_cross(randomizer_mo2_game()+0x12A0,(u32)ground);
            if(height!=-1000000000.0f) {
                roll=(s16)randomizer_mo2_atan((f32)(height-base_height),tx-px);
                if(roll>0x2000||roll<-0x2000)roll=0;
            }
        }
        const s32 maximum=R_MO2(s16,actor+0xD70)!=0?0x400:0x100;
        const s32 ratio=R_MO2(s16,actor+0xD70)!=0?1:8;
        if(pitch!=0x7FFF)randomizer_mo2_angle_calculate(actor+0xD7C,pitch,ratio,maximum);
        if(roll!=0x7FFF)randomizer_mo2_angle_calculate(actor+0xD80,roll,ratio,maximum);
        ground[0x20/4]=0x10014DEC;ground[0x40/4]=0x10014E0C;ground[0x4C/4]=0x10014DCC;
        randomizer_mo2_ground_destroy((u32)ground,0);
    }
    if(R_MO2(s16,actor+0x2D4C)>=0) {
        R_MO2(s16,actor+0x2D4C)++;
        if(R_MO2(s16,actor+0x2D4C)==R_MO2(s16,actor+0x2D4E))R_MO2(s16,actor+0x2D56)=R_MO2(s16,actor+0x2D50);
        if(R_MO2(s16,actor+0x2D4C)==R_MO2(s16,actor+0x2D52))R_MO2(s16,actor+0x2D56)=R_MO2(s16,actor+0x2D54);
        if(R_MO2(s16,actor+0x2D4C)>200)R_MO2(s16,actor+0x2D4C)=-1;
    }
    R_MO2(f32,actor+0x3B8)=0.0f;R_MO2(f32,actor+0x3BC)=10.0f;
    R_MO2(f32,actor+0x3B4)=10000.0f;R_MO2(u8,actor+0x3C4)=R_MO2(u8,actor+0x2D58);
    R_MO2(f32,actor+0x3B4)=WWHD_GAME_DATA(0x1046561C,f32);
    if(R_MO2(s16,actor+0x2D56)!=0) {
        R_MO2(s16,actor+0x2D56)--;R_MO2(f32,actor+0x3C0)=5.0f;
    } else R_MO2(f32,actor+0x3C0)=100.0f;
    return 1;
}
