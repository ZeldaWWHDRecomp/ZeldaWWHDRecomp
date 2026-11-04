/* MIT b_button_skips_text.asm; complete public CC0 a874e1a9 consumers.
 * Seed data chooses the coherent enabled or disabled behavior; predicates
 * retained by the upstream patch still run, even when their branch is omitted. */
#pragma once
#include "instant_text_windows.h"
WWHD_REPLACE(0x026BA628,void,randomizer_instant_scroll_input,(u32 window)) {
    const unsigned int input=rit_selected_input(window);
    if(input&3){
        if(rit_can_down(window)){
            WWHD_GAME_DATA(0x1047B09E,u8)=2;
            rit_sound(0x803);
            rit_change(window+0x18,RITDATA(0x10498C48));
        }else{
            WWHD_GAME_DATA(0x1047B09E,u8)=0;
            rit_change(window+0x18,RITDATA(0x10498D08));
        }
        WWHD_GAME_DATA(0x1047B09E,u8)=2;
        return;
    }
    if(input&0x110000){
        if(rit_can_up(window)){
            WWHD_GAME_DATA(0x1047B09E,u8)=rit_can_down(window)?2:0;
            rit_sound(0x803);
            rit_change(window+0x18,RITDATA(0x10498C78));
        }
        return;
    }
    if(input&0x220000){
        if(rit_can_down(window)){
            WWHD_GAME_DATA(0x1047B09E,u8)=2;
            rit_sound(0x803);
            rit_change(window+0x18,RITDATA(0x10498C48));
        }else{
            WWHD_GAME_DATA(0x1047B09E,u8)=0;
            rit_change(window+0x18,RITDATA(0x10498D08));
        }
        return;
    }
    WWHD_GAME_DATA(0x1047B09E,u8)=rit_can_down(window)?1:4;
    const unsigned int line=RIT32(window,0x118);
    rit_lines(window,RITPTR(window,0xC8+4*(line<20?line:0)));
}
WWHD_REPLACE(0x026F7E78,void,randomizer_instant_icon_in,(u32 icon)) {
    const u32 animation=RITPTR(RITPTR(icon,0x44),0xD4);
    const u32 ended=rit_animation_end(animation,0);
    if(init_instant_text_boxes||ended)
        rit_change(icon+0x18,RITDATA(0x1049DCAC));
}
WWHD_REPLACE(0x026F7FB0,void,randomizer_instant_icon_decide,(u32 icon)) {
    /* Native captures the animator before reading the decision-mode branch. */
    const u32 owner=RITPTR(icon,0x44);
    const u8 to_wait=RIT8(icon,0x54);
    const u32 animation=RITPTR(owner,0xD4);
    if(to_wait){
        if(rit_animation_end(animation,3))
            rit_change(icon+0x18,RITDATA(0x1049DCAC));
        else if(RITS32(icon,0x50)==3){
            RIT32(icon,0x50)=0;
            rit_change(icon+0x18,RITDATA(0x1049DCDC));
        }
    }else{
        const u32 ended=rit_animation_end(animation,3);
        if(init_instant_text_boxes||ended)
            rit_change(icon+0x18,RITDATA(0x1049DD0C));
    }
}
