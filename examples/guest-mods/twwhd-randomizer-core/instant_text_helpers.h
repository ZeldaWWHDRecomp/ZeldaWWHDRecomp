/* Canonical HD message scalar ABI and CC0 a874e1a9 layouts. */
#pragma once
#define RIT8(a,o) (*(volatile u8*)((a)+(o)))
#define RIT32(a,o) (*(volatile unsigned int*)((a)+(o)))
#define RITS32(a,o) (*(volatile s32*)((a)+(o)))
#ifndef RITPTR
#define RITPTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
#define RITDATA(address) ((u32)&WWHD_GAME_DATA(address,u8))
WWHD_GAME_FUNC(0x026FCEC8,u32,rit_has_input,(u32 window));
WWHD_GAME_FUNC(0x020063C0,void,rit_change,(u32 machine,u32 state));
WWHD_GAME_FUNC(0x025F74D0,void,rit_status,(u32 core,u32 status));
WWHD_GAME_FUNC(0x0270477C,void,rit_pane,(u32 pane,u32 message));
WWHD_GAME_FUNC(0x0265233C,u32,rit_selection_ready,(u32 selector));
WWHD_GAME_FUNC(0x025E1988,u32,rit_sound,(u32 sound));
WWHD_GAME_FUNC(0x026BA3A0,s32,rit_can_down,(u32 window));
WWHD_GAME_FUNC(0x026BA470,s32,rit_can_up,(u32 window));
WWHD_GAME_FUNC(0x026B9BD8,void,rit_lines,(u32 window,u32 text));
WWHD_GAME_FUNC(0x02005840,u32,rit_animation_end,(u32 animator,u32 animation));
static unsigned int rit_selected_input(u32 window){return RIT32(window,init_instant_text_boxes?0x50:0x54);}
static void rit_close_virtual(u32 window){const u32 table=RITPTR(window,4);((void(*)(u32))RITPTR(table,0x4A4))(window);}
