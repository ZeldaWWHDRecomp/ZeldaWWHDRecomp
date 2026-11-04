/* Complete CC0 a874e1a9 People::checkPig022C3E78; MIT
 * fix_vanilla_bugs.asm retries pending speech before counting another pig. */
#pragma once
#define RDP8(a,o) (*(volatile u8*)((a)+(o)))
#define RDP32(a,o) (*(volatile unsigned int*)((a)+(o)))
#define RDPF(a,o) (*(volatile f32*)((a)+(o)))
WWHD_GAME_FUNC(0x025B8B94,s32,rdp_event,(u32 event,u32 flag));
WWHD_GAME_FUNC(0x025D9F38,u32,rdp_search,(u32 name,u32 mask,u32 parameter));
WWHD_GAME_FUNC(0x0259D624,void,rdp_distance,(u32 a,u32 b,u32 distance,u32 angle));
WWHD_GAME_FUNC(0x025B8AF4,void,rdp_register_set,(u32 event,u32 reg,u8 value));
WWHD_GAME_FUNC(0x025B8BB0,u8,rdp_register_get,(u32 event,u32 reg));
WWHD_GAME_FUNC(0x025D76A8,s32,rdp_order_speak,(u32 actor));
WWHD_REPLACE(0x022C3E78,s32,randomizer_dampa_check_pig,(u32 actor)) {
    if(!rdp_event(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x1178,0x0280))return 0;
    if(RDP32(actor,0x8CC)&0x80) {
        (void)rdp_order_speak(actor);
        return 0;
    }
    if(!RDP8(actor,0x8FD)&&!RDP8(actor,0x8FE))return 0;
    for(unsigned int i=0;i<3;i++) {
        const u32 pig=rdp_search(0x100207E0,0xF00,RDP32((u32)&WWHD_GAME_DATA(0x1002014C,u8),i*4));
        if(!pig||RDP8(pig,0x750))continue;
        unsigned int a[3],b[3];
        a[0]=RDP32(actor,0x314);a[2]=RDP32(actor,0x31C);a[1]=RDP32(actor,0x318);
        b[0]=RDP32(pig,0x314);b[1]=RDP32(pig,0x318);b[2]=RDP32(pig,0x31C);
        f32 distance;
        rdp_distance((u32)a,(u32)b,(u32)&distance,0);
        if(!(distance<400.0f))continue;
        union {unsigned int word;f32 value;} y,height;
        y.word=RDP32(actor,0x318);
        height.value=y.value-RDPF(pig,0x318);
        height.word&=0x7FFFFFFFu;
        if(height.value>10.0f)continue; /* Native unordered comparison admits NaN. */
        const unsigned int x=RDP32(actor,0x314),z=RDP32(actor,0x31C);
        RDP8(pig,0x750)=1;
        RDP32(pig,0x798)=x;RDP32(pig,0x7A0)=z;RDP32(pig,0x79C)=y.word;
        const unsigned int flags=RDP32(actor,0x8CC);
        RDP8(actor,0x901)=2;
        RDP32(actor,0x8CC)=flags|0x80;
        rdp_register_set(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x1178,0xFC03,(u8)i);
        const u8 reg=rdp_register_get(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x1178,0xFD07);
        rdp_register_set(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x1178,0xFD07,(u8)(reg|(1u<<i)));
        return 1;
    }
    return 0;
}
