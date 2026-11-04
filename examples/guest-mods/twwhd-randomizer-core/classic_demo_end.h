/* Complete public CC0 a874e1a9 endDemoMode023F2048.
 * MIT classic_features.asm skips only the special-demo wait reset. The explicit
 * boss-warp proc201 branch, ground exits and all ship choices remain native.
 * Staged consumer: classic seeds remain rejected until the whole family lands. */
#pragma once
#ifndef WWHD_RANDOMIZER_CLASSIC_MODE
#error "Missing validated classic-mode policy"
#endif
#if WWHD_RANDOMIZER_CLASSIC_MODE != 0 && WWHD_RANDOMIZER_CLASSIC_MODE != 1
#error "Invalid classic-mode policy"
#endif
#if WWHD_RANDOMIZER_CLASSIC_MODE
#define RCD8(a,o) (*(volatile u8*)((a)+(o)))
#define RCD16(a,o) (*(volatile u16*)((a)+(o)))
#define RCD32(a,o) (*(volatile unsigned int*)((a)+(o)))
#ifndef RCDPTR
#define RCDPTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
WWHD_GAME_FUNC(0x023DDD48,void,rcd_texture,(u32 self));
WWHD_GAME_FUNC(0x023DC7AC,void,rcd_delete_item,(u32 self,s32 mode));
WWHD_GAME_FUNC(0x025E1E88,void,rcd_mute_off,(void));
WWHD_GAME_FUNC(0x023E2358,s32,rcd_scope,(u32 self,s32 item));
WWHD_GAME_FUNC(0x023E32AC,s32,rcd_change_wait,(u32 self));
WWHD_GAME_FUNC(0x023F14E0,s32,rcd_next,(u32 self,s32 mode));
WWHD_GAME_FUNC(0x023E2EFC,s32,rcd_control,(u32 self));
WWHD_GAME_FUNC(0x023F1AC8,s32,rcd_cannon,(u32 self));
WWHD_GAME_FUNC(0x023F1C68,s32,rcd_crane,(u32 self));
WWHD_GAME_FUNC(0x023F1D84,s32,rcd_steer,(u32 self));
WWHD_GAME_FUNC(0x023F1E60,s32,rcd_paddle,(u32 self));
WWHD_GAME_FUNC(0x023E2FF4,s32,rcd_wait,(u32 self));
WWHD_REPLACE(0x023F2048,void,randomizer_classic_demo_end,(u32 self)){
    const unsigned int mode=RCD32(self,0x430);
    if(mode==6||mode==0x25)RCD32(self,0x8260)=30;
    const unsigned int procedure=RCD32(self,0x65F0),flags=RCD32(self,0x3B8);
    if(procedure!=104)RCD16(self,0x322)=RCD16(self,0x32A);
    RCD32(self,0x3B8)=flags&~0x100000u;
    const u32 play=wwhd_dComIfG_getGameInfo();
    RCD32(play,0x5CD8)&=~0x10u;
    const unsigned int stick=WWHD_GAME_DATA(0x10035124,unsigned int);
    const u16 old_type=RCD16(self,0x420);(void)old_type;
    RCD32(self,0x430)=0;RCD32(self,0x42C)=0;RCD32(self,0x428)=0;
    RCD16(self,0x420)=0;RCD32(self,0x434)=stick;
    rcd_texture(self);
    const u16 item=RCD16(self,0x69B0);
    if(item==0x104||item==0x10A){rcd_delete_item(self,0);RCD32(self,0x3BC)&=~0x1000u;}
    rcd_mute_off();
    if((RCD32(self,0x834)&0x20)&&!(RCD32(self,0x6A70)&0x14452822u)){
        const unsigned int current=RCD32(self,0x65F0);
        if(current!=0x69&&current!=0xB2&&current!=0xB0){
            if(RCD32(wwhd_dComIfG_getGameInfo(),0x5CD8)&0x200000u)(void)rcd_scope(self,0x20);
            else (void)rcd_change_wait(self);
            return;
        }
    }
    if(RCD32(wwhd_dComIfG_getGameInfo(),0x5CD8)&0x10000u){
        const u32 ship=RCDPTR(wwhd_dComIfG_getGameInfo(),0x5B3C);
        if(!ship){(void)rcd_next(self,0);return;}
        if(RCDPTR(wwhd_dComIfG_getGameInfo(),0x5B2C)!=self){(void)rcd_control(self);return;}
        const u8 part=RCD8(ship,0x637);
        if(part==2)(void)rcd_cannon(self);
        else if(part==3)(void)rcd_crane(self);
        else if(part==1)(void)rcd_steer(self);
        else (void)rcd_paddle(self);
        return;
    }
    /* Native boss-warp branch precedes the patched special-demo test. */
    if(RCD32(self,0x65F0)==201)(void)rcd_wait(self);
}
#endif
