/* Complete CC0 a874e1a9 daPy_lk_c::setGetItemSound023DBCA0.
 * MIT flexible_item_locations: the final selection uses the actual item,
 * bypassing the original short-fanfare flag for this branch. */
#pragma once
WWHD_GAME_FUNC(0x025E1988,void,rig_system_sound,(u32 id));
WWHD_GAME_FUNC(0x025E1918,void,rig_sub_bgm,(u32 id));
WWHD_GAME_FUNC(0x025200D4,u32,rig_game,(void));
WWHD_REPLACE(0x023DBCA0,void,randomizer_item_get_music,(u32 actor,u32 item,s32 short_fanfare)) {
    (void)actor;(void)short_fanfare;
    u32 bgm=0x80000002u;
    if((item>=1&&item<=5)||item==0xB3) {
        rig_system_sound(0x8F5);
        *(volatile u8*)(rig_game()+0x5C20)=2;
        return;
    }
    if(item==8 || (item==7 &&
       ((*(volatile u16*)(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x20)&3)==3)))
        bgm+=0x22;
    else if(item==7||item==6||item==0xF||item==0x15||item==0x1F)
        bgm+=0x23;
    else if(item>=0x69&&item<=0x6B)
        bgm=0x8000004Fu;
    else if(item>=0x6D&&item<=0x72)
        bgm=0x80000027u;
    rig_sub_bgm(bgm);
    *(volatile u8*)(rig_game()+0x5C20)=1;
}
