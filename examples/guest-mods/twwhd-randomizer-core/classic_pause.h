/* Full CC0 a874e1a9 hd_scene_ingame_ui_part2 consumers. MIT classic_features
 * removes only the later menu-allowed branch and available-song branch.
 * Initial game-over, demo and mode gates and their callbacks remain native. */
#pragma once
#ifndef WWHD_RANDOMIZER_CLASSIC_MODE
#error "Regenerate seed data: missing validated classic mode"
#elif WWHD_RANDOMIZER_CLASSIC_MODE != 0 && WWHD_RANDOMIZER_CLASSIC_MODE != 1
#error "Invalid validated classic mode"
#elif WWHD_RANDOMIZER_CLASSIC_MODE
#ifndef R_CLASSIC_UI
#define R_CLASSIC_UI(t,a) (*(volatile t*)(a))
#endif
#ifndef CLASSIC_UI_DATA
#define CLASSIC_UI_DATA(a) ((u32)&WWHD_GAME_DATA(a,u8))
#endif
#ifndef CLASSIC_UI_SONG
#define CLASSIC_UI_SONG(target,self) (((u32(*)(u32))(target))(self))
#endif
WWHD_GAME_FUNC(0x020013C0,void,classic_ui_event,(u32 out,u32 queue));
WWHD_GAME_FUNC(0x020063C0,void,classic_ui_state,(u32 self,u32 state));
WWHD_GAME_FUNC(0x025200D4,u32,classic_ui_play,(void));
WWHD_GAME_FUNC(0x0270D4E8,u32,classic_ui_gameover,(u32 self));
WWHD_GAME_FUNC(0x0270D528,u32,classic_ui_password,(u32 self));
WWHD_GAME_FUNC(0x0270D594,u32,classic_ui_fishman,(u32 self));
WWHD_GAME_FUNC(0x0270D664,u32,classic_ui_drc,(u32 self));
WWHD_GAME_FUNC(0x0270D668,u32,classic_ui_mode,(u32 self));
WWHD_GAME_FUNC(0x02622720,void,classic_ui_show_guide,(u32 guide));
WWHD_GAME_FUNC(0x02622730,void,classic_ui_hide_guide,(u32 guide));
WWHD_GAME_FUNC(0x02621C2C,void,classic_ui_set_guide,(u32 guide,u32 value));
WWHD_GAME_FUNC(0x026A35D4,void,classic_ui_photo,(u32 window));
WWHD_GAME_FUNC(0x026184AC,u32,classic_ui_tv,(u32 controller));
WWHD_GAME_FUNC(0x02617AE4,u32,classic_ui_pro,(u32 controller));
WWHD_GAME_FUNC(0x020399DC,u32,classic_ui_busy,(u32 framework));
WWHD_GAME_FUNC(0x0257E6EC,u32,classic_ui_wind_ready,(void));
WWHD_GAME_FUNC(0x02030B38,void,classic_ui_sound,(u32 name));
WWHD_GAME_FUNC(0x02030C34,void,classic_ui_sound_begin,(void));
WWHD_REPLACE(0x0270D70C,void,randomizer_classic_play_execute,(u32 self)){
    u32 event[10];classic_ui_event((u32)event,R_CLASSIC_UI(u32,CLASSIC_UI_DATA(0x101F4FF0))+0x30);
    if(classic_ui_gameover(self)){classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049F46C));return;}
    if(R_CLASSIC_UI(u8,classic_ui_play()+0x5BB0)&&!R_CLASSIC_UI(u32,classic_ui_play()+0x5B4C)){
        classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EC2C));return;
    }
    if(R_CLASSIC_UI(u8,CLASSIC_UI_DATA(0x1047B097))){
        classic_ui_show_guide(R_CLASSIC_UI(u32,self+0x1E0));classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EDB8));return;
    }
    if(R_CLASSIC_UI(u8,classic_ui_play()+0x5BE8)==2){classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049F288));return;}
    if(R_CLASSIC_UI(u32,classic_ui_play()+0x5CD8)&0x200000){classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049F128));return;}
    if(R_CLASSIC_UI(u32,classic_ui_play()+0x5CDC)&8){classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049F180));return;}
    if(classic_ui_password(self)){classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049F338));return;}
    if(classic_ui_fishman(self)){classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049F390));return;}
    if(R_CLASSIC_UI(u8,self+0x243)){classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049F0D0));return;}
    if(classic_ui_drc(self)){
        if(event[0]==0x68)classic_ui_photo(R_CLASSIC_UI(u32,self+0x180));
        classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EC2C));return;
    }
    if(classic_ui_mode(self)){R_CLASSIC_UI(u8,self+0x23C)=0;return;}
    if(classic_ui_tv(R_CLASSIC_UI(u32,CLASSIC_UI_DATA(0x101F5088)))){
        if(event[0]==0x62)classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EC84));return;
    }
    /* Upstream NOP retains this getter and halfword load, not its branch. */
    (void)R_CLASSIC_UI(u16,classic_ui_play()+0x5BAC);
    switch(event[0]){
    case 0x2A:classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049ED08));break;
    case 0x2B:classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049ED60));break;
    case 0x62:classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EC84));break;
    case 0x66:if(!classic_ui_busy(R_CLASSIC_UI(u32,CLASSIC_UI_DATA(0x1018F4AC))))classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EFF4));break;
    case 0x67:if(!classic_ui_busy(R_CLASSIC_UI(u32,CLASSIC_UI_DATA(0x1018F4AC))))classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EF70));break;
    case 0x68:if(!classic_ui_busy(R_CLASSIC_UI(u32,CLASSIC_UI_DATA(0x1018F4AC))))classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EF44));break;
    case 0x69:classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049F04C));break;
    case 0x6A:classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EF9C));break;
    case 0x6D:classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049F2E0));break;
    }
}
WWHD_REPLACE(0x0270F01C,void,randomizer_classic_takt_execute,(u32 self)){
    u32 event[10];classic_ui_event((u32)event,R_CLASSIC_UI(u32,CLASSIC_UI_DATA(0x101F4FF0))+0x30);
    if(event[0]==0x62){
        (void)R_CLASSIC_UI(u8,self+0x240);
        classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EE10));classic_ui_sound(CLASSIC_UI_DATA(0x1013F068));classic_ui_sound_begin();R_CLASSIC_UI(u8,self+0x23C)=0;return;
    }
    const u32 demo=R_CLASSIC_UI(u32,classic_ui_play()+0x5B2C);
    const u32 vtable=R_CLASSIC_UI(u32,demo+0xB4);
    const u32 song=CLASSIC_UI_SONG(R_CLASSIC_UI(u32,vtable+0x2C),demo);
    if(song==0&&R_CLASSIC_UI(u8,self+0x242)){
        if(classic_ui_wind_ready()&&!classic_ui_pro(R_CLASSIC_UI(u32,CLASSIC_UI_DATA(0x101F5088))))classic_ui_hide_guide(R_CLASSIC_UI(u32,self+0x1E0));
        else classic_ui_set_guide(R_CLASSIC_UI(u32,self+0x1E0),0);
        classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EEC0));return;
    }
    if(song==1&&R_CLASSIC_UI(u8,self+0x242)){classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EEEC));return;}
    if(song<=5){classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EE94));return;}
    if(R_CLASSIC_UI(u8,CLASSIC_UI_DATA(0x1047B097)))return;
    classic_ui_hide_guide(R_CLASSIC_UI(u32,self+0x1E0));
    if(R_CLASSIC_UI(u8,classic_ui_play()+0x5292))classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EC2C));
    else classic_ui_state(self+0x10,CLASSIC_UI_DATA(0x1049EBD4));
}
#endif
