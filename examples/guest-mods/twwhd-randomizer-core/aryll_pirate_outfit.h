/* Complete CC0 a874e1a9 d_a_npc_ls1.cpp heap/wait consumers.
 * MIT misc_rando_features.asm0227E5A4/022830E0 always selects pirate clothes
 * and wait state4. Native queries still execute where upstream leaves them;
 * no global second-quest or story flag is changed. */
#pragma once
#include "aryll_outfit_helpers.h"
WWHD_REPLACE(0x0227E3BC,s32,randomizer_aryll_body_heap,(u32 actor)) {
    const u32 archive=actor+0x7F8;
    const u32 data=rao_resource(archive,0xD);
    if(!data)rao_assert(0x1001D1B8,0xC3C,0x1001D1C8);
    for(unsigned int i=0;i<2;i++){
        const u32 material=rao_eye_material(data,i);
        u32 animation=rao_allocate(0x80);
        if(animation)animation=rao_material_constructor(animation);
        RAOPTR(material,0x24)=animation;
    }
    const u32 created=rao_morf_constructor(0,data,0,0,0,-1,1.0f,0,-1,1,0,0x80000,0x11020222);
    RAOPTR(actor,0x44C)=created;
    if(!created)return 0;
    if(!RAOPTR(created,0x90)){
        const u32 vtable=RAOPTR(created,0);
        ((void(*)(u32,s32))RAOPTR(vtable,0xC))(created,3);
        RAOPTR(actor,0x44C)=0;
        return 0;
    }
    /* The NOP removes the branch only, not the original type/save-byte loads. */
    if(RAOS8(actor,0xA25)!=4){const u32 save=WWHD_GAME_DATA(0x101F84DC,u32);(void)RAO8(save,0x1C0);}
    const u32 source=rao_resource(archive,0x1D);
    const u32 texture=RAOPTR(data,0x30);
    if(!texture)rao_assert(0x1001D1B8,0xC68,0x1001D1DC);
    const u32 names=RAOPTR(data,0x34);
    if(!names)rao_assert(0x1001D1B8,0xC6A,0x1001D1F0);
    for(u16 i=0;i<RAOU16(texture,0);i=(u16)(i+1)){
        const u32 name=rao_texture_name(names,i);
        if(!name)rao_assert(0x1001D1B8,0xC6E,0x1001D208);
        u32 a=name,b=(u32)&WWHD_GAME_DATA(0x1001D218,u8);
        u8 first,second;
        do{first=RAO8(a,0);a++;second=RAO8(b,0);b++;}while(first==second&&first);
        if(first!=second)continue;
        const unsigned int offset=(unsigned int)i*0x24;
        u32 entry=RAOPTR(texture,4)+offset;
        /* Forward interleaved word copy, including the ninth HD word. */
        for(unsigned int k=0;k<9;k++)RAO32(entry,4*k)=RAO32(source,4*k);
        entry=RAOPTR(texture,4)+offset;
        RAO32(entry,0x1C)=RAO32(entry,0x1C)+(unsigned int)source-(unsigned int)entry;
        entry=RAOPTR(texture,4)+offset;
        RAO32(entry,0xC)=RAO32(entry,0xC)+(unsigned int)source-(unsigned int)entry;
        entry=RAOPTR(texture,4)+offset;
        RAO32(entry,0x20)=RAO32(source,0x20);
        const unsigned int high=RAO32(texture,0x18),low=RAO32(texture,0x1C);
        if(i<0x40){
            const unsigned int n=i;
            const unsigned int previous_low=RAO32(texture,0xC);
            const unsigned int next_high=rao_shift_left(low,n+32)|rao_shift_left(high,n)|rao_shift_right(low,32-n);
            const unsigned int previous_high=RAO32(texture,8);
            RAO32(texture,8)=previous_high|next_high;
            RAO32(texture,0xC)=previous_low|rao_shift_left(low,n);
        }else{
            const unsigned int n=(unsigned int)i-64;
            const unsigned int previous_high=RAO32(texture,0x10);
            const unsigned int previous_low=RAO32(texture,0x14);
            const unsigned int next_high=rao_shift_left(low,n+32)|rao_shift_left(high,n)|rao_shift_right(low,32-n);
            RAO32(texture,0x14)=previous_low|rao_shift_left(low,n);
            RAO32(texture,0x10)=previous_high|next_high;
        }
    }
    if(!rao_texture_animation(actor,0,0)){RAOPTR(actor,0x44C)=0;return 0;}
    static const unsigned int strings[4]={0x1001D1B0,0x1001D238,0x1001D1A0,0x1001D1A8};
    static const unsigned int messages[4]={0x1001D224,0x1001D244,0x1001D25C,0x1001D274};
    static const s32 lines[4]={0xC7D,0xC7F,0xC81,0xC83};
    for(unsigned int i=0;i<4;i++){
        const s8 joint=(s8)rao_name_index(rao_joint_names(data),strings[i]);
        RAOS8(actor,0x7E4+i)=joint;
        if(joint<0)rao_assert(0x1001D1B8,lines[i],messages[i]);
    }
    rao_set_joint(actor,0x7E4,0x0227D954);
    rao_set_joint(actor,0x7E5,0x0227DAC0);
    rao_set_joint(actor,0x7E6,0x0227DC18);
    rao_set_joint(actor,0x7E7,0x0227DD70);
    RAOPTR(RAOPTR(RAOPTR(actor,0x44C),0x90),0xB8)=actor;
    return 1;
}
WWHD_REPLACE(0x02283038,s32,randomizer_aryll_wait_action,(u32 actor,u32 argument)) {
    (void)argument;
    const s32 state=RAOS8(actor,0xA26);
    if(!state){
        _Alignas(4) unsigned int position[3];
        rao_event_position(actor,(u32)position,0);
        const unsigned int y=position[1],z=position[2],x=position[0];
        RAO32(actor,0x9A0)=y;RAO32(actor,0x9A4)=z;RAO32(actor,0x99C)=x;
        rao_event_position(actor,(u32)position,1);
        const u8 next=(u8)(RAO8(actor,0xA26)+1);
        const unsigned int z2=position[2],y2=position[1],x2=position[0];
        RAO32(actor,0x9B0)=z2;RAO32(actor,0x9AC)=y2;RAO8(actor,0xA26)=next;RAO32(actor,0x9A8)=x2;
        const u32 save=WWHD_GAME_DATA(0x101F84DC,u32);
        (void)rao_event_query(save+0x644,0x2A80);
        rao_set_state(actor,4);
    }else if((unsigned int)state<=3){
        const u8 attention=rao_attention(actor);
        const s32 mode=RAOS8(actor,0xA21);
        RAO8(actor,0xA0E)=attention;
        switch(mode){
        case 1:RAO32(actor,0x9DC)=(unsigned int)rao_wait1(actor);break;
        case 2:RAO32(actor,0x9DC)=(unsigned int)rao_wait2(actor);break;
        case 3:RAO32(actor,0x9DC)=(unsigned int)rao_wait3(actor);break;
        case 4:RAO32(actor,0x9DC)=(unsigned int)rao_wait4(actor);break;
        case 5:RAO32(actor,0x9DC)=(unsigned int)rao_talk1(actor);break;
        }
    }
    return 1;
}
