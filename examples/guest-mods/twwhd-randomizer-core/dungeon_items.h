/* Dungeon-specific grants and statue ownership from TWWHD Randomizer 1.2.0.
 * Copyright (c) 2024 SuperDude88. MIT: LICENSE-TWWHD-Randomizer.txt. */
#pragma once

/* Key is 3; the other values are native onDungeonItem indices. */
static int randomizer_dungeon_item(u32 item,u32* stage,u32* kind) {
    switch(item) {
#define DUNGEON_ITEM(id,s,k) case id:*stage=s;*kind=k;return 1
    DUNGEON_ITEM(0x40,3,3); DUNGEON_ITEM(0x41,3,2);
    DUNGEON_ITEM(0x5A,3,0); DUNGEON_ITEM(0x5B,3,1);
    DUNGEON_ITEM(0x5C,4,3); DUNGEON_ITEM(0x5D,4,2);
    DUNGEON_ITEM(0x5E,4,0); DUNGEON_ITEM(0x5F,4,1);
    DUNGEON_ITEM(0x60,5,3); DUNGEON_ITEM(0x73,5,2);
    DUNGEON_ITEM(0x74,5,0); DUNGEON_ITEM(0x75,5,1);
    DUNGEON_ITEM(0x76,2,0); DUNGEON_ITEM(0x81,2,1);
    DUNGEON_ITEM(0x84,6,3); DUNGEON_ITEM(0x85,6,2);
    DUNGEON_ITEM(0x86,6,0); DUNGEON_ITEM(0x87,6,1);
    DUNGEON_ITEM(0x88,7,3); DUNGEON_ITEM(0x89,7,2);
    DUNGEON_ITEM(0x8A,7,0); DUNGEON_ITEM(0x8B,7,1);
#undef DUNGEON_ITEM
    default:return 0;
    }
}
static u32 randomizer_statue_event(u32 stage,u32 chest) {
    return chest==15 && stage>=3 && stage<=7 ? 0x6A00u|(4u<<(stage-3)) : 0;
}
static u32 randomizer_statue_owned(u32 stage,u32 chest) {
    const u32 event=randomizer_statue_event(stage,chest);
    return event ? (wwhd_save_event_test(event)==1) : 0;
}
static int randomizer_grant_dungeon_item(u32 item) {
    if(item>=0xA3 && item<=0xA7) {
        wwhd_save_event_set(randomizer_statue_event(item-0xA3+3,15),1);
        return 1;
    }
    u32 stage,kind;
    if(!randomizer_dungeon_item(item,&stage,&kind))return 0;
    const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    const int active=*(volatile u8*)(save+0x7BC)==stage;
    const u32 memory=active?save+0x798:save+0x3A0+stage*0x24;
    if(kind!=3)wwhd_dSv_memBit_c_onDungeonItem(memory,kind);
    else {
        if(active) {
            /* Preserve the virtual accessor used by upstream, including derived
             * stage-data implementations, instead of assuming a field offset. */
            const u32 data=wwhd_dComIfG_getGameInfo()+0x5150;
            const u32 table=*(volatile u32*)data;
            const u32 stag=((u32 (*)(u32))(*(volatile u32*)(table+0x15C)))(data);
            if(*(volatile u8*)(stag+9)&1) {
                wwhd_item_func_small_key();
                return 1;
            }
        }
        /* Upstream byte increment wraps at 255; do not clamp or refill pending keys. */
        volatile u8* keys=(volatile u8*)(memory+0x20);
        *keys=(u8)(*keys+1);
    }
    return 1;
}

/* These are the exact enclosing native functions of upstream's 23 ownership
 * callsites. Scope the query, never replace chest-source persistence globally.
 * Observer pairs preserve native arguments, return values and nested scopes. */
static u32 randomizer_statue_query_depth,randomizer_statue_count_depth;
#define STATUE_QUERY_SCOPE(addr,name) \
    WWHD_HOOK(addr,name##_enter,(void)){++randomizer_statue_query_depth;} \
    WWHD_HOOK_RETURN(addr,name##_leave,(void)){--randomizer_statue_query_depth;}
STATUE_QUERY_SCOPE(0x022E7B4C,randomizer_statue_is_create)
STATUE_QUERY_SCOPE(0x022E9508,randomizer_statue_monument_pay)
STATUE_QUERY_SCOPE(0x022E972C,randomizer_statue_monument_fee)
STATUE_QUERY_SCOPE(0x022EAA84,randomizer_statue_exist)
STATUE_QUERY_SCOPE(0x022EADA4,randomizer_statue_blue_message)
STATUE_QUERY_SCOPE(0x022EB5F4,randomizer_statue_red_message)
STATUE_QUERY_SCOPE(0x023B6310,randomizer_statue_model_create)
#undef STATUE_QUERY_SCOPE
WWHD_GAME_ORIGINAL(0x02520864,u32,randomizer_native_statue_chest,(u32 stage,u32 chest));
WWHD_REPLACE(0x02520864,u32,randomizer_statue_chest,(u32 stage,u32 chest)) {
    if(randomizer_statue_query_depth && chest==15)
        return randomizer_statue_owned(stage,chest);
    return randomizer_native_statue_chest(stage,chest);
}
WWHD_HOOK(0x0263B230,randomizer_quest_statue_count_enter,(void)){++randomizer_statue_count_depth;}
WWHD_HOOK_RETURN(0x0263B230,randomizer_quest_statue_count_leave,(void)){--randomizer_statue_count_depth;}
WWHD_GAME_ORIGINAL(0x025B8378,s32,randomizer_native_collect_map_count,(u32 self));
WWHD_REPLACE(0x025B8378,s32,randomizer_collect_statue_count,(u32 self)) {
    if(!randomizer_statue_count_depth)return randomizer_native_collect_map_count(self);
    s32 count=0;
    for(u32 stage=3;stage<=7;++stage)count+=(s32)randomizer_statue_owned(stage,15);
    return count;
}
