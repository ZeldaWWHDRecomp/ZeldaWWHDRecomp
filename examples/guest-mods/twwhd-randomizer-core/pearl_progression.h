/* Pearl/statue/Hyrule progression from TWWHD Randomizer 1.2.0
 * remove_cutscenes.asm and make_game_nonlinear.asm (MIT; see
 * LICENSE-TWWHD-Randomizer.txt). Native call order/state: public CC0
 * d_item_2.cpp and d_a_obj_doguu.cpp. */
#pragma once
#include "ship_boarding_state.h"
#include "jabun_wall_state.h"

static void randomizer_grant_pearl(u32 index) {
    wwhd_dSv_player_collect_c_onSymbol(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0xD4,index);
    const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    const u16 placed[3]={0x1410,0x1480,0x1440}; // Nayru, Din, Farore.
    wwhd_dSv_event_c_onEventBit(save+0x644,placed[index]);
    // Match upstream's whole-byte test, not just a count of placed story flags.
    if(*(volatile u8*)(save+0xDF)==7) {
        wwhd_dSv_event_c_onEventBit(save+0x644,0x1E40);
        wwhd_dSv_event_c_onEventBit(save+0x644,0x2E80);
    }
}
WWHD_REPLACE(0x0254F51C,void,randomizer_nayru_pearl,(void)){randomizer_grant_pearl(0);}
WWHD_REPLACE(0x0254F530,void,randomizer_din_pearl,(void)){randomizer_grant_pearl(1);}
WWHD_REPLACE(0x0254F544,void,randomizer_farore_pearl,(void)){randomizer_grant_pearl(2);}

// Hyrule initialization/execution use complete source consumers in their own
// headers. This shared query wrapper retains only the other reviewed adapters.
WWHD_GAME_ORIGINAL(0x025B8B94,s32,randomizer_native_pearl_event_query,(u32 events,u32 bit));
WWHD_REPLACE(0x025B8B94,s32,randomizer_pearl_event_query,(u32 events,u32 bit)) {
    if(randomizer_jabun_wall_query(events,bit)) {
        (void)randomizer_native_pearl_event_query(events,bit);
        return 1;
    }
    if(randomizer_ship_boarding_query(events,bit)) {
        (void)randomizer_native_pearl_event_query(events,bit);
        return 1;
    }
    return randomizer_native_pearl_event_query(events,bit);
}
typedef struct {u32 actor;u8 finish;} randomizer_goddess_scope;
static randomizer_goddess_scope randomizer_goddess;
WWHD_GAME_ORIGINAL(0x023368F0,s32,randomizer_native_goddess_execute,(u32 actor));
WWHD_REPLACE(0x023368F0,s32,randomizer_goddess_execute,(u32 actor)) {
    const randomizer_goddess_scope previous=randomizer_goddess;
    randomizer_goddess=(randomizer_goddess_scope){actor,*(volatile s32*)(actor+0xB98)==9};
    const s32 result=randomizer_native_goddess_execute(actor);
    randomizer_goddess=previous;
    return result;
}
WWHD_GAME_ORIGINAL(0x02336824,s32,randomizer_native_goddess_count,(u32 actor));
WWHD_REPLACE(0x02336824,s32,randomizer_goddess_count,(u32 actor)) {
    const s32 count=randomizer_native_goddess_count(actor);
    if(randomizer_goddess.finish && randomizer_goddess.actor==actor &&
       *(volatile s32*)(actor+0xB98)==9) { // STATE_FINISH_DEMO3, after setFinishMyEvent.
        randomizer_goddess.finish=0;
        if(count>=3) {
            wwhd_dSv_event_c_onEventBit(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x1E40);
            // Take the exact existing target of upstream's02336D68->02336D94:
            // native code writes STATE_WAIT(10), ends the event with flags|8,
            // then runs its unchanged animation/collision/demo tail.
            return 0;
        }
    }
    return count;
}
