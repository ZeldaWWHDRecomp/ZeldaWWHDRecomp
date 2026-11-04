/* Public CC0 d_a_ship.cpp/.h, checkForceMessage and setInitMessage.
 * MIT randomizer make_game_nonlinear.asm removes only each direct sail test.
 * Complete consumers keep nested callbacks on real inventory: no shared
 * ownership-query replacement or broad dialogue scope is installed. */
#pragma once
#include "ship_boarding_state.h"
WWHD_GAME_FUNC(0x025B8B94,s32,randomizer_ship_dialogue_event_native,(u32 object,u32 flag));
WWHD_GAME_FUNC(0x025B7D90,s32,randomizer_ship_dialogue_symbol_native,(u32 object,u32 symbol));
WWHD_GAME_FUNC(0x02520C0C,s32,randomizer_ship_dialogue_item_native,(u8 item));
WWHD_GAME_FUNC(0x025B7B10,s32,randomizer_ship_dialogue_tact_native,(u32 object,u32 song));
WWHD_GAME_FUNC(0x025B7E00,s32,randomizer_ship_dialogue_triforce_native,(u32 object));
WWHD_GAME_FUNC(0x0257DAA8,u32,randomizer_ship_dialogue_wind,(void));
WWHD_GAME_FUNC(0x024731BC,s32,randomizer_ship_force_dispatch,(u32 actor));
static u32 randomizer_ship_dialogue_save(void){return WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);}
static s32 randomizer_ship_dialogue_event(u32 flag){return randomizer_ship_dialogue_event_native(randomizer_ship_dialogue_save()+0x644,flag);}
static s32 randomizer_ship_dialogue_symbol(u32 symbol){return randomizer_ship_dialogue_symbol_native(randomizer_ship_dialogue_save()+0xD4,symbol);}
static s32 randomizer_ship_dialogue_item(u8 item){return randomizer_ship_dialogue_item_native(item);}
static s32 randomizer_ship_dialogue_sword(void){
    (void)wwhd_dComIfG_getGameInfo();
    const u8 sword=*(volatile u8*)(randomizer_ship_dialogue_save()+0x2E);
    return sword==0x39||sword==0x3A||sword==0x3E;
}
#ifndef RANDOMIZER_SHIP_DIALOGUE_STRING_VTABLE
#define RANDOMIZER_SHIP_DIALOGUE_STRING_VTABLE 0x1003A1EC /* ship TU, assureTermination 024832A0 */
#endif
typedef struct{u32 string,vtable;} randomizer_ship_dialogue_string;
static void randomizer_ship_dialogue_assure(randomizer_ship_dialogue_string* text){
    ((void(*)(randomizer_ship_dialogue_string*))(*(volatile u32*)(text->vtable+0x14)))(text);
}
static s32 randomizer_ship_dialogue_stage(u32 literal){
    randomizer_ship_dialogue_string a={literal,RANDOMIZER_SHIP_DIALOGUE_STRING_VTABLE};
    const u32 play=wwhd_dComIfG_getGameInfo();
    randomizer_ship_dialogue_string b={play+0x5134,RANDOMIZER_SHIP_DIALOGUE_STRING_VTABLE};
    randomizer_ship_dialogue_assure(&a);randomizer_ship_dialogue_assure(&a);
    u32 pa=a.string;
    randomizer_ship_dialogue_assure(&b);
    u32 pb=b.string;
    if(pa==pb)return 1;
    pa=a.string;pb=b.string;
    for(u32 n=0;n<0x40001;n++){
        const u8 ca=*(volatile u8*)(pa+n),cb=*(volatile u8*)(pb+n);
        if(ca!=cb)return 0;
        if(!ca)return 1;
    }
    return 0;
}
/* The original first sail call is replaced with true by upstream; all later
 * query calls retain their actual results and callback behavior. */
WWHD_REPLACE(0x024731BC,s32,randomizer_ship_force_message,(u32 actor)) {
    if (!randomizer_ship_dialogue_event(0x0908)) {
        (*(volatile u32*)(actor+0x648)) = 0x5E0;
    } else if (randomizer_ship_dialogue_symbol(1) && !randomizer_ship_dialogue_event(0x0A80)) {
        (*(volatile u32*)(actor+0x648)) = 0x5EC;
    } else if (randomizer_ship_dialogue_symbol(2) && !randomizer_ship_dialogue_event(0x0A08)) {
        (*(volatile u32*)(actor+0x648)) = 0x5F6;
    } else if (randomizer_ship_dialogue_event(0x0A02) && !randomizer_ship_dialogue_event(0x0A01)) {
        (*(volatile u32*)(actor+0x648)) = 0x607;
    } else if (randomizer_ship_dialogue_item(0x31) && !randomizer_ship_dialogue_event(0x1F02)) {
        (*(volatile u32*)(actor+0x648)) = 0x624;
    } else if (randomizer_ship_dialogue_symbol(0) && !randomizer_ship_dialogue_event(0x2F20)) {
        (*(volatile u32*)(actor+0x648)) = 0xD5A;
    } else if (randomizer_ship_dialogue_event(0x2D10) && !randomizer_ship_dialogue_sword()) {
        (*(volatile u32*)(actor+0x648)) = 0xD65;
    } else if (randomizer_ship_dialogue_event(0x3E01) && !randomizer_ship_dialogue_event(0x3F80)) {
        (*(volatile u32*)(actor+0x648)) = 0x1688;
    } else if (randomizer_ship_dialogue_event(0x2D02) && !randomizer_ship_dialogue_event(0x3201)) {
        (*(volatile u32*)(actor+0x648)) = 0x1645;
    } else {
        return 0;
    }
    return 1;
}

/* Upstream removes only the direct missing-sail branch after player status. */
static void randomizer_ship_init_message_body(u32 actor) {
    if ((*(volatile u32*)(wwhd_dComIfG_getGameInfo()+0x5CD8)) & 0x10000) {
        return;
    } else if (randomizer_ship_force_dispatch(actor)) {
        return;
    } else if (randomizer_ship_dialogue_event(0x2110) && !randomizer_ship_dialogue_item(0x31)) {
        (*(volatile u32*)(actor+0x648)) = 0x623;
    } else if ((*(volatile u32*)(actor+0x644)&0x800000)) {
        (*(volatile u32*)(actor+0x648)) = 0x636;
    } else {
        /* Upstream replaces the late milestone chain at 02473514: skip all
         * those event queries, then compare only Hyrule when seed data asks. */
        if(init_use_different_korl_hyrule_text &&
           randomizer_ship_dialogue_stage((u32)&WWHD_GAME_DATA(0x1003A460,u8)))
            *(volatile u32*)(actor+0x648)=0xD77;
        else *(volatile u32*)(actor+0x648)=0xD73;
    }
}

WWHD_REPLACE(0x02473454,void,randomizer_ship_init_message,(u32 actor)){
    const u32 previous=randomizer_ship_boarding_scope;
    randomizer_ship_boarding_scope=0;
    randomizer_ship_init_message_body(actor);
    randomizer_ship_boarding_scope=previous;
}
