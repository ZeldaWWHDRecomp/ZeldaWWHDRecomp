/* CC0 a874e1a9 daNpcPeople_c::eventGetItemInit022C0FC8.
 * MIT flexible_item_locations.asm: packed high-half event is set after the
 * factory even on failure; only low item byte is passed to presentation. */
#pragma once
WWHD_GAME_FUNC(0x0254487C,u32,rwt_integer,(u32 manager,s32 staff,u32 name,s32 count));
WWHD_GAME_FUNC(0x025D7DEC,u32,rwt_present,(u32 position,u32 item,u32 flag,s32 subtype,s32 room,u32 rotation,u32 scale));
WWHD_GAME_FUNC(0x025B8B68,void,rwt_event_on,(u32 save,u32 flag));
static u32 rwt_create(u32 position,unsigned int packed,s8 room) {
    const u32 result=rwt_present(position,packed&255,0,-1,room,0,0);
    const u32 event=packed>>16;
    if(event)rwt_event_on(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,event);
    return result;
}
WWHD_REPLACE(0x022C0FC8,void,randomizer_windfall_grant,(u32 actor,s32 staff)) {
    const u32 value=rwt_integer(wwhd_dComIfG_getGameInfo()+0x52C4,staff,0x10020668,3);
    const s8 room=*(volatile s8*)(actor+0x326);
    unsigned int packed;
    if(value) {
        const unsigned int index=*(volatile unsigned int*)value;
        packed=*(volatile unsigned int*)((u32)&WWHD_GAME_DATA(0x101C3998,u8)+index*4u);
    } else packed=*(volatile unsigned int*)(actor+0x8D0);
    const u32 result=rwt_create(actor+0x314,packed,room);
    if((unsigned int)result!=0xFFFFFFFFu)
        *(volatile unsigned int*)(wwhd_dComIfG_getGameInfo()+0x52A0)=(unsigned int)result;
}
WWHD_HOOK(0x028EA120,randomizer_initialize_windfall_reward_flags,(void)) {
    WWHD_GAME_DATA(0x101C399C,u16)=0x6904;
    WWHD_GAME_DATA(0x101C39A0,u16)=0x6908;
    WWHD_GAME_DATA(0x101C39AC,u16)=0x6910;
}
