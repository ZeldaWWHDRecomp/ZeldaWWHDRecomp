/* Complete CC0 a874e1a9 d_a_pw.cpp::Big_pow_down_check0244A67C.
 * MIT fix_vanilla_bugs.asm recognizes light-arrow deaths and cancels their
 * timer when Jalhalla's last-four-frame reform path resurrects the Poe. */
#pragma once
#define RPA8(a,o) (*(volatile u8*)((a)+(o)))
#define RPA16(a,o) (*(volatile s16*)((a)+(o)))
#define RPA32(a,o) (*(volatile unsigned int*)((a)+(o)))
WWHD_GAME_FUNC(0x025D54C4,s32,rpa_search_id,(u32 id,u32 result));
WWHD_REPLACE(0x0244A67C,s32,randomizer_poe_down_check,(u32 actor)) {
    /* The original health load survives upstream's replaced extension/branch. */
    (void)RPA8(actor,0x3A1);
    if(RPA8(actor,0x461))return 0;
    const unsigned int id=RPA32(actor,0x488);
    if(id==0xFFFFFFFFu)return 0;
    if((s8)RPA8(actor,0x3A1)>0&&!RPA8(actor,0x9A6))return 0;
    volatile u32 found;
    if(!rpa_search_id(id,(u32)&found))return 0;
    const u32 boss=found;
    if(!boss||RPA16(boss,8)!=0xD3)return 0;
    if(RPA16(boss,0x562)==0x6F&&RPA16(boss,0x56A)>3) {
        RPA8(boss,0x3A1)=(u8)(RPA8(boss,0x3A1)-1u);
        if((s8)RPA8(found,0x3A1)<=0)RPA8(actor,0x460)=1;
        RPA8(actor,0x461)=1;
        return 0;
    }
    RPA8(actor,0x3A1)=4;
    RPA8(actor,0x9A6)=0;
    return 1;
}
