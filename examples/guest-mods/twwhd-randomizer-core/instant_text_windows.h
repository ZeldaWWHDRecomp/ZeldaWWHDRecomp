/* Complete CC0 a874e1a9 HD message window handlers.
 * Only the nine public MIT patch input-load seams select held input50.
 * Later input reads, native predicates and callbacks retain stock behavior. */
#pragma once
#include "instant_text_helpers.h"
WWHD_REPLACE(0x026FCEE0,void,randomizer_text_stop,(u32 s)) {
    if (rit_has_input(s) || (rit_selected_input(s) & 2u)) {
        unsigned int count = RIT32(s,0x84) + 1;
        RIT32(s,0x84) = count;
        RIT8(wwhd_dComIfG_getGameInfo(),0x5BD2) = count;
        WWHD_GAME_DATA(0x1047B09E,u8) = 2;
        unsigned int i = RIT32(s,0xE8) + 1;
        if (RITS32(s,0x98+4*(i < 10 ? i : 0)) >= 0)
            rit_change((s+0x18), RITDATA(0x1049E3AC));
        else {
            rit_status(WWHD_GAME_DATA(0x101F4B5C,u32), 14);
            rit_change((s+0x18), RITDATA(0x1049E2BC));
        }
    } else if (RIT8(RITPTR(s,0x118),0x90E)) {
        rit_status(WWHD_GAME_DATA(0x101F4B5C,u32), 1);
        rit_change((s+0x18), RITDATA(0x1049E40C));
    } else {
        rit_pane(RITPTR(s,0x68), RITPTR(s,0x118));
        WWHD_GAME_DATA(0x1047B09E,u8) = 1;
    }
}

WWHD_REPLACE(0x026FD1DC,void,randomizer_text_wait_input,(u32 s)) {
    u32 msg = RITPTR(s,0x118);
    s32 accepted = RITS32(msg,0x6B4) == 0;
    u8 mode = 0;
    if (!accepted) {
        if (rit_has_input(s) || (rit_selected_input(s) & 2u)) {
            u32 play = wwhd_dComIfG_getGameInfo();
            msg = RITPTR(s,0x118);
            if (!RIT8(play,0x5C20)) {
                accepted = 1;
                mode = 2;
            }
        }
    }
    if (accepted) {
        RIT8(msg,0x698) = 0;
        unsigned int count = RIT32(s,0x84) + 1;
        RIT32(s,0x84) = count;
        RIT8(wwhd_dComIfG_getGameInfo(),0x5BD2) = count;
        WWHD_GAME_DATA(0x1047B09E,u8) = mode;
        unsigned int i = RIT32(s,0xE8) + 1;
        if (RITS32(s,0x98+4*(i < 10 ? i : 0)) < 0) {
            rit_status(WWHD_GAME_DATA(0x101F4B5C,u32), 14);
            rit_change((s+0x18), RITDATA(0x1049E2BC));
        } else
            rit_change((s+0x18), RITDATA(0x1049E3AC));
    } else {
        s32 t = RITS32(msg,0x6B4);
        RITS32(msg,0x6B4) = t > 0 ? (unsigned int)t - 1 : 0;
        rit_pane(RITPTR(s,0x68), RITPTR(s,0x118));
        WWHD_GAME_DATA(0x1047B09E,u8) = 1;
    }
}

WWHD_REPLACE(0x026FD980,void,randomizer_text_select_two,(u32 s)) {
    u32 core = WWHD_GAME_DATA(0x101F4B5C,u32);
    s32 input = rit_has_input(s) != 0;
    if (!input && (rit_selected_input(s) & 2u))
        input = RIT8(wwhd_dComIfG_getGameInfo(),0x5BB6) == 39;
    if (!input)
        input = RIT8(RITPTR(s,0x118),0x90D) != 0;
    if (input && rit_selection_ready(RITPTR(core,0x964))) {
        if (RIT32(s,0x54) & 2u)
            RIT8(wwhd_dComIfG_getGameInfo(),0x5BD3) = 1;
        RIT32(core,0x948) = RIT32(s,0xF4);
        rit_status(core, 14);
        unsigned int count = RIT32(s,0x84) + 1;
        RIT32(s,0x84) = count;
        RIT8(wwhd_dComIfG_getGameInfo(),0x5BD2) = count;
        rit_sound(RIT32(s,0xF4) == 0 ? 0x80D : 0x880);
        WWHD_GAME_DATA(0x1047B09E,u8) = 2;
        rit_change((s+0x18), RITDATA(0x1049E10C));
        return;
    }
    unsigned int flags = RIT32(s,0x54);
    unsigned int old = RIT32(s,0xF4);
    unsigned int value = old;
    if (flags & 0x100000u) {
        value = 0;
        RIT32(s,0xF4) = value;
    } else if (flags & 0x200000u) {
        value = 1;
        RIT32(s,0xF4) = value;
    } else if (flags & 0x10000u) {
        value = 0;
        RIT32(s,0xF4) = value;
    } else if (flags & 0x20000u) {
        value = 1;
        RIT32(s,0xF4) = value;
    }
    if (value != old)
        rit_sound(0x83D);
    WWHD_GAME_DATA(0x1047B09E,u8) = 1;
}

WWHD_REPLACE(0x026FDB08,void,randomizer_text_select_three,(u32 s)) {
    u32 core = WWHD_GAME_DATA(0x101F4B5C,u32);
    s32 input = rit_has_input(s) != 0;
    if (!input && (rit_selected_input(s) & 2u))
        input = RIT8(wwhd_dComIfG_getGameInfo(),0x5BB6) == 39;
    if (!input)
        input = RIT8(RITPTR(s,0x118),0x90D) != 0;
    if (input && rit_selection_ready(RITPTR(core,0x964))) {
        if (RIT32(s,0x54) & 2u)
            RIT8(wwhd_dComIfG_getGameInfo(),0x5BD3) = 1;
        RIT32(core,0x948) = RIT32(s,0xF4);
        rit_status(core, 14);
        unsigned int count = RIT32(s,0x84) + 1;
        RIT32(s,0x84) = count;
        RIT8(wwhd_dComIfG_getGameInfo(),0x5BD2) = count;
        rit_sound(RIT32(s,0xF4) == 2 ? 0x880 : 0x80D);
        WWHD_GAME_DATA(0x1047B09E,u8) = 2;
        rit_change((s+0x18), RITDATA(0x1049E10C));
        return;
    }
    unsigned int flags = RIT32(s,0x54);
    unsigned int old = RIT32(s,0xF4);
    unsigned int value = old;
    if (flags & 0x100000u) {
        if ((s32)value > 0) {
            --value;
            RIT32(s,0xF4) = value;
        }
    } else if (flags & 0x200000u) {
        if ((s32)value < 2) {
            ++value;
            RIT32(s,0xF4) = value;
        }
    } else if (flags & 0x10000u) {
        if ((s32)value > 0) {
            --value;
            RIT32(s,0xF4) = value;
        }
    } else if (flags & 0x20000u) {
        if ((s32)value < 2) {
            ++value;
            RIT32(s,0xF4) = value;
        }
    }
    if (value != old)
        rit_sound(0x83D);
    WWHD_GAME_DATA(0x1047B09E,u8) = 1;
}

WWHD_REPLACE(0x026FE78C,void,randomizer_text_auto_wait_input,(u32 s)) {
    u32 msg = RITPTR(s,0x118);
    if (RITS32(msg,0x6B4) == 0) {
        RIT8(msg,0x698) = 0;
        WWHD_GAME_DATA(0x1047B09E,u8) = 0;
        unsigned int n = RIT32(s,0x84) + 1;
        RIT32(s,0x84) = n;
        RIT8(wwhd_dComIfG_getGameInfo(),0x5BD2) = (u8)n;
        rit_change((s+0x18), RITDATA(0x1049E34C));
        return;
    }
    s32 input = rit_has_input(s) != 0;
    if (!input)
        input = (rit_selected_input(s) & 2u) != 0;
    if (!input)
        input = RIT8(msg,0x90D) != 0;
    if (input) {
        u32 play = wwhd_dComIfG_getGameInfo();
        u8 blocked = RIT8(play,0x5C20);
        msg = RITPTR(s,0x118);
        if (!blocked) {
            RIT8(msg,0x698) = 0;
            WWHD_GAME_DATA(0x1047B09E,u8) = 0;
            rit_change((s+0x18), RITDATA(0x1049E43C));
            return;
        }
    }
    s32 t = RITS32(msg,0x6B4);
    RITS32(msg,0x6B4) = t > 0 ? (unsigned int)t - 1 : 0;
    u32 pane = RITPTR(s,0x68);
    u32 message = RITPTR(s,0x118);
    rit_pane(pane, message);
    WWHD_GAME_DATA(0x1047B09E,u8) = 4;
}

WWHD_REPLACE(0x026FEAF4,void,randomizer_text_hand_stop,(u32 s)) {
    s32 input = rit_has_input(s) != 0;
    if (!input)
        input = (rit_selected_input(s) & 2u) != 0;
    if (!input && RIT32(s,0x5C) == 0x5AC)
        input = RIT8(wwhd_dComIfG_getGameInfo(),0x5BD3) != 0;
    if (input && !RIT8(wwhd_dComIfG_getGameInfo(),0x5C20)) {
        WWHD_GAME_DATA(0x1047B09E,u8) = 2;
        u32 play = wwhd_dComIfG_getGameInfo();
        rit_sound(RIT8(play,0x5BBA) == 39 ? 0x880 : 0x80B);
        unsigned int n = RIT32(s,0x84) + 1;
        RIT32(s,0x84) = n;
        RIT8(wwhd_dComIfG_getGameInfo(),0x5BD2) = (u8)n;
        RIT8(wwhd_dComIfG_getGameInfo(),0x5BD3) = 0;
        rit_change((s+0x18), RITDATA(0x1049E3DC));
        return;
    }
    u32 msg = RITPTR(s,0x118);
    s32 close = RIT8(msg,0x90E) != 0;
    if (close) {
        u32 play = wwhd_dComIfG_getGameInfo();
        u8 blocked = RIT8(play,0x5C20);
        msg = RITPTR(s,0x118);
        close = !blocked && !RIT8(msg,0x90F) && RIT32(msg,0x910) != RIT32(msg,0x11C);
    }
    if (close) {
        WWHD_GAME_DATA(0x1047B09E,u8) = 2;
        rit_sound(0x80B);
        rit_close_virtual(s);
        rit_change((s+0x18), RITDATA(0x1049DE9C));
    } else {
        rit_pane(RITPTR(s,0x68), msg);
        WWHD_GAME_DATA(0x1047B09E,u8) = 1;
    }
}

WWHD_REPLACE(0x026FD3C0,void,randomizer_text_stop_input,(u32 s)) {
    if (rit_has_input(s) || (rit_selected_input(s) & 2u))
        rit_change((s+0x18), RITDATA(0x1049E07C));
}

WWHD_REPLACE(0x026FE600,void,randomizer_text_auto_stop_input,(u32 s)) {
    s32 confirm = rit_has_input(s) != 0;
    if (!confirm) confirm = (rit_selected_input(s) & 2u) != 0;
    if (!confirm) confirm = RIT8(RITPTR(s,0x118),0x90D) != 0;
    if (confirm) {
        WWHD_GAME_DATA(0x1047B09E,u8) = 0;
        rit_change((s+0x18), RITDATA(0x1049E43C));
    } else {
        rit_pane(RITPTR(s,0x68), RITPTR(s,0x118));
        WWHD_GAME_DATA(0x1047B09E,u8) = 4;
    }
}
