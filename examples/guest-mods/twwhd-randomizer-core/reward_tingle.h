/* Gift control flow adapted from the public CC0 d_a_npc_tc_cut.cpp (022E9854).
 * Seed values are private configuration, not executable patches. */
#pragma once
WWHD_GAME_ORIGINAL(0x022E9854,void,randomizer_native_tingle_present,(u32 actor,s32 staff));
WWHD_GAME_FUNC(0x0254487C,u32,randomizer_event_substance,(u32 manager,s32 staff,const char* name,s32 type));
WWHD_GAME_FUNC(0x02543280,void,randomizer_event_cut_end,(u32 manager,s32 staff));

WWHD_REPLACE(0x022E9854,void,randomizer_tingle_present,(u32 actor,s32 staff)) {
    const u32 info=wwhd_dComIfG_getGameInfo();
    const u32 manager=info+0x52C4;
    const u32 parameter=randomizer_event_substance(manager,staff,"ItemType",3);
    const u32 type=parameter?*(volatile u32*)parameter:0;
    u32 item;
    if(type==0) item=init_reward_tingle_first;
    else if(type==1) item=init_reward_tingle_second;
    else if(type==2 && *(volatile u8*)(actor+0x970)==1 &&
            wwhd_daNpc_Tc_c__checkAllMonumentFee((void*)actor))
        item=init_reward_ankle_statues;
    else {
        /* Preserve the unshuffled monument fees and chart-deciphering rewards. */
        randomizer_native_tingle_present(actor,staff);
        return;
    }
    const u32 id=wwhd_fopAcM_createItemForPresentDemo(actor+0x314,item,0,
        0xFFFFFFFFu,(u32)*(volatile s8*)(actor+0x326),0,0);
    if(id!=0xFFFFFFFFu) {
        *(volatile u32*)(info+0x52A0)=id;
        randomizer_event_cut_end(manager,staff);
    }
}
