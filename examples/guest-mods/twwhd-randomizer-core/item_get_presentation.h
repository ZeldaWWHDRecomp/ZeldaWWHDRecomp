/* Source-level adapter for make_items_progressive.asm's dProcGetItem_init paths.
 * Copyright (c) 2024 SuperDude88. MIT: see LICENSE-TWWHD-Randomizer.txt. */
#pragma once
WWHD_GAME_ORIGINAL(0x024216C8,s32,randomizer_native_get_item_init,(u32 actor));
typedef struct {
    u32 actor,info,old_parameter,new_parameter;
    u8 old_event,new_event,parameter_changed,event_changed;
} randomizer_get_item_context;
static randomizer_get_item_context randomizer_get_item;

static void randomizer_sync_get_item(u32 actor) {
    randomizer_get_item_context* c=&randomizer_get_item;
    if(c->actor!=actor) return;
    volatile u32* parameter=(volatile u32*)(actor+0x428);
    const u32 raw=*parameter;
    if(raw==0x100) {
        volatile u8* event=(volatile u8*)(c->info+0x52A4);
        if(c->event_changed && *event==c->new_event) return;
        const u8 display=(u8)randomizer_display_item(*event);
        if(display!=*event) {
            c->old_event=*event;c->new_event=display;c->event_changed=1;*event=display;
        }
    } else {
        if(c->parameter_changed && raw==c->new_parameter) return;
        const u32 display=randomizer_display_item(raw);
        if(display!=raw) {
            c->old_parameter=raw;c->new_parameter=display;c->parameter_changed=1;*parameter=display;
        }
    }
}

WWHD_REPLACE(0x024216C8,s32,randomizer_get_item_init,(u32 actor)) {
    const randomizer_get_item_context previous=randomizer_get_item;
    const u32 info=wwhd_dComIfG_getGameInfo();
    randomizer_get_item=(randomizer_get_item_context){0};
    randomizer_get_item.actor=actor;randomizer_get_item.info=info;
    randomizer_sync_get_item(actor);
    const s32 result=randomizer_native_get_item_init(actor);
    volatile u32* parameter=(volatile u32*)(actor+0x428);
    volatile u8* event=(volatile u8*)(info+0x52A4);
    if(randomizer_get_item.parameter_changed && *parameter==randomizer_get_item.new_parameter)
        *parameter=randomizer_get_item.old_parameter;
    if(randomizer_get_item.event_changed && *event==randomizer_get_item.new_event)
        *event=randomizer_get_item.old_event;
    randomizer_get_item=previous;
    return result;
}
/* Native initialization can populate the demo parameter after entry. These are
 * its two final calls before loading that parameter, verified in the original RPX. */
WWHD_HOOK_RETURN(0x023E0A04,randomizer_after_item_get_animation,(u32 actor)) {
    randomizer_sync_get_item(actor);
}
WWHD_HOOK_RETURN(0x023ECEF0,randomizer_after_item_get_unequip,(u32 actor)) {
    randomizer_sync_get_item(actor);
}
