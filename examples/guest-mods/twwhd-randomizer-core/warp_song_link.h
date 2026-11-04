/* Complete public CC0 a874e1a9 procTactPlay_init/procTactPlay,
 * d_a_player_main_07.cpp. MIT remove_cutscenes skips initial textbox and
 * first-use replay while preserving manual/partner event paths and callbacks. */
#pragma once
#ifndef R_SONG
#define R_SONG(t,a) (*(volatile t*)(a))
#endif
WWHD_GAME_FUNC(0x023E26EC,s32,song_ship_item,(u32 self,s32 item));
WWHD_GAME_FUNC(0x023DFDD8,s32,song_common_init,(u32 self,s32 procedure));
WWHD_GAME_FUNC(0x025E1EF0,void,song_volume,(f32 value));
WWHD_GAME_FUNC(0x023DC6A4,s32,song_reset_upper,(u32 self,s32 upper,f32 morph));
WWHD_GAME_FUNC(0x023E0A04,s32,song_animation,(u32 self,s32 animation,f32 speed,f32 start,s32 end,f32 morph));
WWHD_GAME_FUNC(0x023E2E18,void,song_init_ship_item,(u32 self,s32 use,s32 kind));
WWHD_GAME_FUNC(0x025D5834,u32,song_create_actor,(s32 name,u32 parameter,u32 pos,s32 room,u32 angle,u32 scale,s32 subtype,u32 callback));
WWHD_GAME_FUNC(0x023D457C,void,song_emitter,(u32 callback,u32 id,u32 matrix,u32 pos,u32 angle));
WWHD_GAME_FUNC(0x023E287C,void,song_ship_position,(u32 self,s32 mode));
WWHD_GAME_FUNC(0x025E1E94,void,song_reset,(void));
WWHD_GAME_FUNC(0x023DD768,void,song_texture,(u32 self,u32 animation,s32 change));
WWHD_GAME_FUNC(0x0243AAD4,s32,song_usable,(u32 self));
WWHD_GAME_FUNC(0x025E1988,void,song_sound,(u32 id));
WWHD_GAME_FUNC(0x02412948,s32,song_end_init,(u32 self,s32 song));
WWHD_GAME_FUNC(0x025E1F14,void,song_ambient,(void));
WWHD_GAME_FUNC(0x0241FBCC,s32,song_message_ended,(u32 self,u32 message));
WWHD_GAME_FUNC(0x024F8044,u32,song_camera,(void));
WWHD_GAME_FUNC(0x0253E860,void,song_camera_end,(u32 camera,u32 actor));
WWHD_GAME_FUNC(0x023F2048,void,song_end_demo,(u32 self));
WWHD_GAME_FUNC(0x027170AC,void,song_save_result,(u32 system,u32 song));
WWHD_GAME_FUNC(0x025E1F58,void,song_melody,(s32 song));
WWHD_GAME_FUNC(0x02543280,void,song_cut_end,(u32 manager,s32 staff));
WWHD_GAME_FUNC(0x025F795C,u32,song_message_status,(u32 manager));
WWHD_GAME_FUNC(0x025F74D0,void,song_message_set_status,(u32 manager,s32 status));
WWHD_GAME_FUNC(0x025D5218,u32,song_find_actor,(u32 predicate,u32 key));
#ifndef SONG_STAGE_INFO
#define SONG_STAGE_INFO(stage) (((u32(*)(u32))R_SONG(u32,R_SONG(u32,(stage))+0x15C))(stage))
#endif
static u8 song_mask(s32 song){const u32 shift=(u32)song&63;return shift>=32?0:(u8)(1u<<shift);}
static void song_read_learned(s32 song){
    /* Both upstream patches retain this actual system-byte load and mask;
     * only its following conditional branch changes. */
    const u32 system=WWHD_GAME_DATA(0x101F8344,u32);
    volatile u8 learned=R_SONG(u8,system+0x241)&song_mask(song);(void)learned;
}
static void song_event_reset(void){const u32 info=wwhd_dComIfG_getGameInfo();R_SONG(u16,info+0x52B8)|=8;}
static void song_end_camera(u32 self){const u32 camera=song_camera();song_camera_end(camera,R_SONG(u32,self+4));}
static void song_end_staff(u32 self){const s32 staff=R_SONG(s32,self+0x69DC);song_cut_end(wwhd_dComIfG_getGameInfo()+0x52C4,staff);}
static void song_wait_animation(u32 self){
    (void)song_animation(self,0xA8,0.8f,0,-1,10);song_texture(self,6,0);
    R_SONG(u32,self+0x6A70)=(R_SONG(u32,self+0x6A70)|0x100)&~0x400u;
}
WWHD_REPLACE(0x02439D54,s32,randomizer_song_play_init,(u32 self,s32 song,s32 warp,s32 no_message)){
    const s32 use=song_ship_item(self,0);(void)song_common_init(self,0x9B);
    const u32 info=wwhd_dComIfG_getGameInfo();R_SONG(u32,info+0x5CDC)|=1;
    R_SONG(s32,self+0x69C0)=song;
    (void)WWHD_GAME_DATA(0x101F4B5C,u32); /* retained manager capture */
    if(!no_message)song_read_learned(song);
    R_SONG(u32,self+0x6A80)=0xFFFFFFFFu;
    song_volume(0);(void)song_reset_upper(self,2,-1);(void)song_reset_upper(self,1,-1);
    R_SONG(s16,self+0x6920)=10;
    const s32 animation=song==0?0xE4:song==1?0xE5:song==5?0xE6:song==4?0xE7:song==2?0xE8:0xE9;
    (void)song_animation(self,animation,1,0,-1,3);song_init_ship_item(self,use,2);
    if(use&&R_SONG(s32,self+0x69C0)==1){
        const u32 stage=wwhd_dComIfG_getGameInfo()+0x5150;
        const u32 stage_info=SONG_STAGE_INFO(stage);
        if(((R_SONG(u32,stage_info+0xC)>>16)&7)==7&&R_SONG(u8,self+0x6911)!=1&&warp){
            const u32 ship=R_SONG(u32,wwhd_dComIfG_getGameInfo()+0x5B3C);
            const u32 id=song_create_actor(0x1BB,1,self+0x314,R_SONG(s8,self+0x326),0,0,-1,0);
            R_SONG(u32,ship+0x714)=id;
        }
    }
    const u32 model=R_SONG(u32,self+0x4440);
    song_emitter(self+0x66F8,0x51,model?model+0xC8:0,self+0x314,0);
    R_SONG(s16,self+0x691E)=(s16)no_message;R_SONG(u32,self+0x6A7C)=0;
    R_SONG(s32,self+0x69C4)=warp;R_SONG(s16,self+0x6916)=0;return 1;
}
WWHD_REPLACE(0x0243AC64,s32,randomizer_song_play,(u32 self)){
    if(R_SONG(u32,wwhd_dComIfG_getGameInfo()+0x5CD8)&0x10000){
        song_ship_position(self,1);R_SONG(s16,self+0x32A)=(s16)((u16)R_SONG(s16,self+0x32A)-0x4000u);
    }
    if(!R_SONG(s16,self+0x691E)&&R_SONG(s32,self+0x69C4)){
        const s32 song=R_SONG(s32,self+0x69C0);
        if(R_SONG(u32,self+0x6A84)==0xFFFFFFFFu||(u32)R_SONG(u8,self+0x6911)!=(u32)song){
            song_read_learned(song);song_reset();R_SONG(u8,wwhd_dComIfG_getGameInfo()+0x5BD1)=0;
            song_wait_animation(self);
            if(song_usable(self)){song_sound(0x871);return song_end_init(self,R_SONG(s32,self+0x69C0));}
        }
    }
    song_ambient();
    if(R_SONG(u32,self+0x6A7C)){
        if(song_message_ended(self,R_SONG(u32,self+0x6A7C))){
            (void)song_reset_upper(self,2,-1);song_event_reset();song_end_camera(self);song_end_demo(self);
            song_save_result(WWHD_GAME_DATA(0x101F8344,u32),(u8)R_SONG(s32,self+0x69C0));
        }return 1;
    }
    if(R_SONG(s16,self+0x6920)>0){
        R_SONG(s16,self+0x6920)=(s16)(R_SONG(s16,self+0x6920)-1);
        if(!R_SONG(s16,self+0x6920))song_melody(R_SONG(s32,self+0x69C0));
    }
    if(R_SONG(s16,self+0x6916)){
        if(R_SONG(s16,self+0x691E)){
            R_SONG(u8,wwhd_dComIfG_getGameInfo()+0x5BD1)=0;song_end_staff(self);return 1;
        }
        const u32 manager=WWHD_GAME_DATA(0x101F4B5C,u32),status=song_message_status(manager);
        if(status==0||status==0x12){
            u32 partner=0;R_SONG(u8,wwhd_dComIfG_getGameInfo()+0x5BD1)=0;
            if(status==0x12)song_message_set_status(manager,0x13);
            R_SONG(u32,self+0x6A80)=0xFFFFFFFFu;
            const u32 id=R_SONG(u32,self+0x6A84);
            if(id!=0xFFFFFFFFu&&(u32)R_SONG(u8,self+0x6911)==(u32)R_SONG(s32,self+0x69C0)){
                const u32 key=id;partner=song_find_actor(0x025E1234,(u32)&key);
            }
            if(!R_SONG(s32,self+0x69C4)){song_end_staff(self);return 1;}
            if(partner){
                if(!R_SONG(u32,self+0x68AC)){
                    (void)song_reset_upper(self,2,-1);song_event_reset();song_end_camera(self);song_end_demo(self);return 1;
                }return song_end_init(self,-1);
            }
            if(song_usable(self)){
                song_save_result(WWHD_GAME_DATA(0x101F8344,u32),(u8)R_SONG(s32,self+0x69C0));
                return song_end_init(self,R_SONG(s32,self+0x69C0));
            }
        }
    }else if(!(R_SONG(f32,self+0x5898)>=0.01f)){
        /* Native BGE at0243B0F0 falls through for unordered as well as LT. */
        song_sound(0x871);R_SONG(u8,WWHD_GAME_DATA(0x101F4B5C,u32)+0x921)=1;
        R_SONG(s16,self+0x6916)=1;song_reset();song_wait_animation(self);
    }
    return 1;
}
