/* Complete CC0 a874e1a9 daItemBase_c::animEntry0218368C.
 * MIT misc_rando_features uses resource color7 for a ping-pong rainbow BRK. */
#pragma once
#ifndef RRAIN
#define RRAIN(t,a) (*(volatile t*)(a))
#endif
WWHD_GAME_FUNC(0x025E83FC,void,rrain_brk,(u32 animation,u32 model,f32 frame));
WWHD_GAME_FUNC(0x025E7FC4,void,rrain_btk,(u32 animation,u32 model,f32 frame));
WWHD_GAME_FUNC(0x025E86B8,void,rrain_bck,(u32 animation,u32 model,f32 frame));
WWHD_REPLACE(0x0218368C,void,randomizer_item_animation_entry,(u32 actor)) {
    const u32 first=RRAIN(u32,actor+0x3C8);
    if(first) {
        const u32 item=RRAIN(u8,actor+0x74E);
        const s8 color=RRAIN(s8,(u32)&WWHD_GAME_DATA(0x101E4674,u8)+item*36+0x14);
        const u32 model=RRAIN(u32,actor+0x3B4);
        const u32 data=RRAIN(u32,model+0xAC);
        f32 frame;
        if(color==-1)frame=RRAIN(f32,first+4);
        else if(color==7) {
            frame=RRAIN(f32,actor+0x3A0)+0.15f;
            if(!(frame<6.0f))frame=-6.0f;
            RRAIN(f32,actor+0x3A0)=frame;
            frame=__builtin_fabsf(frame);
        } else frame=(f32)color;
        rrain_brk(first,data,frame);
    }
    static const u16 offsets[]={0x3C0,0x3CC,0x3C4,0x3D0};
    for(u32 index=0;index<4;index++) {
        const u32 animation=RRAIN(u32,actor+offsets[index]);
        if(!animation)continue;
        const u32 model=RRAIN(u32,actor+0x3B4);
        const f32 frame=RRAIN(f32,animation+4);
        const u32 data=RRAIN(u32,model+0xAC);
        if(index==1)rrain_brk(animation,data,frame);
        else if(index==3)rrain_bck(animation,data,frame);
        else rrain_btk(animation,data,frame);
    }
}
