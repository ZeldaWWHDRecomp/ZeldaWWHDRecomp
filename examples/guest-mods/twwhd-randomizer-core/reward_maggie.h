/* Event action follows public CC0 d_a_npc_kp1.cpp (0227C268).
 * The two gifts are private seed data; failed actor creation keeps the cut pending. */
#pragma once
WWHD_REPLACE(0x0227C268,u8,randomizer_maggie_gift,(u32 actor)) {
    const u8 action=*(volatile u8*)(actor+0x942);
    if(action>1) return 1;
    const u32 id=wwhd_fopAcM_createItemForPresentDemo(actor+0x314,
        init_reward_maggie[action],0,0xFFFFFFFFu,0xFFFFFFFFu,0,0);
    /* Upstream marks the first source after the factory call even on failure;
     * preserve the pending-cut return below independently of that quest flag. */
    if(action==0) wwhd_save_event_set(0x6A01,1);
    if(id==0xFFFFFFFFu) return 0;
    *(volatile u32*)(wwhd_dComIfG_getGameInfo()+0x52A0)=id;
    return 1;
}
