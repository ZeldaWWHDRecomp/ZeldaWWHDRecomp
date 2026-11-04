/* Complete CC0 a874e1a9 checkFallCode023FEA6C. MIT classic_features removes
 * only the dead-player exit branch; its life read and all other gates remain.
 * Staged until the complete classic family and coherent decoder are ready. */
#pragma once
#ifndef WWHD_RANDOMIZER_CLASSIC_MODE
#error "Missing validated classic-mode policy"
#elif WWHD_RANDOMIZER_CLASSIC_MODE != 0 && WWHD_RANDOMIZER_CLASSIC_MODE != 1
#error "Invalid classic-mode policy"
#elif WWHD_RANDOMIZER_CLASSIC_MODE
#define CFL8(a,o) (*(volatile u8*)((a)+(o)))
#define CFLS8(a,o) (*(volatile s8*)((a)+(o)))
#define CFL16(a,o) (*(volatile u16*)((a)+(o)))
#define CFL32(a,o) (*(volatile unsigned int*)((a)+(o)))
#define CFLF(a,o) (*(volatile f32*)((a)+(o)))
#ifndef CFLPTR
#define CFLPTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
#define CFLDATA(a) ((u32)&WWHD_GAME_DATA(a,u8))
WWHD_GAME_FUNC(0x023FD4E4,s32,cfl_restart,(u32 self,s32 mode,s32 wipe,f32 speed,s32 arg));
WWHD_GAME_FUNC(0x024EECC8,s32,cfl_exit,(u32 bg,u32 ground));
WWHD_GAME_FUNC(0x024EF130,s32,cfl_room,(u32 bg,u32 ground));
WWHD_GAME_FUNC(0x02540310,s32,cfl_compulsory,(u32 event,u32 self,u32 zero,u32 flags));
WWHD_GAME_FUNC(0x025C38B8,s32,cfl_scene,(u32 ground,f32 speed,s32 type,s32 room));
WWHD_GAME_FUNC(0x025B8B94,s32,cfl_event,(u32 save,u32 flag));
WWHD_GAME_FUNC(0x025B8B68,void,cfl_event_set,(u32 save,u32 flag));
WWHD_GAME_FUNC(0x023D4688,void,cfl_change_player,(u32 self,u32 actor));
WWHD_GAME_FUNC(0x025E1988,void,cfl_sound,(u32 id));
WWHD_GAME_FUNC(0x0201ADE0,void,cfl_sub,(u32 source,u32 out,u32 other));
WWHD_GAME_FUNC(0x028E8DD0,f64,cfl_square,(u32 vector));
WWHD_GAME_FUNC(0x028F4384,f64,cfl_sqrt,(f64 value));
WWHD_GAME_FUNC(0x020195B0,s16,cfl_atan,(f64 x,f64 z));
#ifndef CFL_STAGE_EQUALS
static void cfl_assure(u32 *s){((void(*)(u32))CFLPTR(s[1],0x14))((u32)s);}
static s32 cfl_stage_equals(void){
    u32 a[2]={CFLDATA(0x10035964),CFLDATA(0x10034B24)};
    const u32 play=wwhd_dComIfG_getGameInfo();
    u32 b[2]={play+0x5134,CFLDATA(0x10034B24)};
    cfl_assure(a);cfl_assure(a);const u32 first=a[0];cfl_assure(b);
    if(first==b[0])return 1;
    u32 x=a[0],y=b[0];
    for(u32 n=0;n<0x40001;n++,x++,y++){
        const u8 ax=CFL8(x,0),by=CFL8(y,0);
        if(ax!=by)return 0;if(!ax)return 1;
    }return 0;
}
#define CFL_STAGE_EQUALS() cfl_stage_equals()
#endif
WWHD_REPLACE(0x023FEA6C,void,randomizer_classic_fall_exit,(u32 self)){
#pragma clang fp contract(off)
    const unsigned int code=CFL32(self,0x69D0);
    const f32 ground=CFLF(self,0x8A0);
    if(code==4||ground==-1000000000.0f){
        const unsigned int mode=CFL32(self,0x6A70);
        s32 restart;
        if(CFLF(self,0x6A28)>ground)restart=(mode&0x40000)!=0;
        else restart=(mode&2)&&!(mode&0x200)&&CFLF(self,0x6A4C)-CFLF(self,0x318)>2000.0f;
        if(restart||((CFL32(self,0x834)&0x20)&&!(CFL32(self,0x3B8)&0xA0000000u))){
            (void)cfl_restart(self,5,0xC9,-1.0f,0);CFL32(self,0x834)&=~0x20u;
        }return;
    }
    if((CFL32(self,0x3B8)&0x10)&&(CFL32(self,0x6A70)&0x40000)){
        (void)cfl_restart(self,5,0xC9,-1.0f,0);return;
    }
    (void)CFL16(wwhd_dComIfG_getGameInfo(),0x5BAC);
    const s32 exit=cfl_exit(wwhd_dComIfG_getGameInfo()+0x12A0,self+0x8F4);
    const s32 room=cfl_room(wwhd_dComIfG_getGameInfo()+0x12A0,self+0x8F4);
    const u32 ship=CFLPTR(wwhd_dComIfG_getGameInfo(),0x5B3C);
    if(exit==0x3F)return;
    s32 go=(CFL32(self,0x6A70)&0x40000)||(CFL32(self,0x834)&0x20)||CFL32(self,0x65F0)==0x93;
    if(!go&&(CFL32(wwhd_dComIfG_getGameInfo(),0x5CD8)&0x10000)&&ship&&!CFL32(ship,0x704)&&!CFL32(ship,0x70C))go=1;
    if(!go&&!(CFL32(self,0x69D0)==5&&CFLF(self,0x6A4C)-CFLF(self,0x318)>500.0f))return;
    CFL8(wwhd_dComIfG_getGameInfo(),0x5BE0)=1;
    const f32 speed=CFLF(self,0x370);
    const f32 delta=17.0f-speed;
    const f32 bounded=speed<1.5f?1.5f:delta>=0.0f?speed:17.0f;
    s32 type;
    if(CFL32(wwhd_dComIfG_getGameInfo(),0x5CD8)&0x10000){
        type=1;
        if(CFLPTR(wwhd_dComIfG_getGameInfo(),0x5B3C)){
            const u32 current_ship=CFLPTR(wwhd_dComIfG_getGameInfo(),0x5B3C);CFL32(current_ship,0x644)|=0x20000000u;
        }
    }else if(CFL32(self,0x6A70)&0x1000000)type=CFLF(self,0x5898)<0?3:2;
    else type=0;
    const u16 command=CFL16(self,0xF8);
    if(CFL32(self,0x69D0)==5)CFL32(self,0x834)&=~0x20u;
    if(command!=3&&!cfl_compulsory(wwhd_dComIfG_getGameInfo()+0x51D0,self,0,0xFFFF))return;
    if(!cfl_scene(self+0x8F4,bounded,type,(s8)room))return;
    CFL32(self,0x3BC)|=0x100000u;
    if(cfl_event(WWHD_GAME_DATA(0x101F84DC,u32)+0x644,0x3E10))cfl_event_set(WWHD_GAME_DATA(0x101F84DC,u32)+0x644,0x3E01);
    cfl_change_player(self,self);
    if(CFL_STAGE_EQUALS()&&CFLS8(self,0x326)==1)cfl_sound(0x288B);
    if(CFL16(self,0xF8)==3)return;
    CFL16(self,0x420)=3;
    if(CFL32(self,0x69D0)==5)CFL32(self,0x430)=0x11;
    else if((CFL32(self,0x6A70)&0x40000)||CFL8(self,0x68DF)==1||CFL8(self,0x68DF)==4){CFLF(self,0x6A14)*=0.75f;CFL32(self,0x430)=0xE;}
    else CFL32(self,0x430)=2;
    f32 vector[3];cfl_sub(self+0x314,(u32)vector,self+0x300);
    const f64 length=cfl_sqrt(cfl_square((u32)vector));
    if(length>(f64)0.1f)CFL16(self,0x422)=cfl_atan(vector[0],vector[2]);
    else CFL16(self,0x422)=CFL16(self,0x322);
}
#endif
