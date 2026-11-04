/* Public CC0 daTagPhoto_c::eventMove (d_a_tag_photo.cpp).
 * Additional prerequisite: MIT randomizer make_game_nonlinear.asm.
 * Keep event completion/reset and cut callbacks; only flag1601 waits for1701.
 */
#pragma once
WWHD_GAME_FUNC(0x025440C8,s32,randomizer_gallery_end,(u32 manager,s32 event));
WWHD_GAME_FUNC(0x025B8B94,s32,randomizer_gallery_prerequisite,(u32 events,u32 flag));
WWHD_GAME_FUNC(0x025B8B68,void,randomizer_gallery_set,(u32 events,u32 flag));
WWHD_GAME_FUNC(0x0259F858,s32,randomizer_gallery_cut,(u32 cut));
WWHD_GAME_FUNC(0x024B219C,void,randomizer_gallery_private_cut,(u32 actor));
WWHD_REPLACE(0x024B228C,void,randomizer_gallery_event_move,(u32 actor)) {
    s32 event=*(volatile s16*)(actor+0x430);
    s32 done=randomizer_gallery_end(wwhd_dComIfG_getGameInfo()+0x52C4,event);
    if(!done) {
        event=*(volatile s16*)(actor+0x432);
        done=randomizer_gallery_end(wwhd_dComIfG_getGameInfo()+0x52C4,event);
    }
    if(done) {
        const u32 play=wwhd_dComIfG_getGameInfo();
        *(volatile u16*)(play+0x52B8)|=8;
        if(randomizer_gallery_prerequisite(
                WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x1701))
            randomizer_gallery_set(
                WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x1601);
    } else {
        const u8 flag=*(volatile u8*)(actor+0x420);
        if(randomizer_gallery_cut(actor+0x3C0)) {
            if(!*(volatile u8*)(actor+0x420))*(volatile u8*)(actor+0x420)=flag;
        } else randomizer_gallery_private_cut(actor);
    }
}
