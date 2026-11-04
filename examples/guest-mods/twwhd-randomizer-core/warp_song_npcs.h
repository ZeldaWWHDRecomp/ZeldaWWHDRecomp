/* Complete public CC0 a874e1a9 Tott demoInitPatten and Makar init.
 * MIT randomizer remove_cutscenes: default dance count1, boss-rescue action
 * always wait. Explicit event counts and all unrelated NPC paths remain native. */
#pragma once
#ifndef R_WARP_NPC
#define R_WARP_NPC(t,a) (*(volatile t*)(a))
#endif
#ifndef WARP_NPC_DATA
#define WARP_NPC_DATA(a) ((u32)&WWHD_GAME_DATA(a,u8))
#endif
WWHD_GAME_FUNC(0x022EF074,void,warp_npc_dance,(u32 self,s32 force));
WWHD_GAME_FUNC(0x0254487C,u32,warp_npc_substance,(u32 manager,s32 staff,u32 name,s32 type));
WWHD_GAME_FUNC(0x02543F10,s32,warp_npc_event_index,(u32 manager,u32 name,s32 map));
WWHD_GAME_FUNC(0x0221F2C4,void,warp_npc_makar_action,(u32 self,u32 pmf,u32 arg));
WWHD_GAME_FUNC(0x0221F314,s32,warp_npc_makar_ship,(u32 self));
WWHD_GAME_FUNC(0x025B8B94,s32,warp_npc_event,(u32 events,u32 flag));
WWHD_GAME_FUNC(0x02515F14,void,warp_npc_status,(u32 self,u32 weight,u32 type,u32 actor));
WWHD_GAME_FUNC(0x02516518,void,warp_npc_cylinder,(u32 self,u32 source));
WWHD_REPLACE(0x022EFAD8,void,randomizer_tott_pattern_init,(u32 self)){
    const u8 flags=R_WARP_NPC(u8,self+0x1111);R_WARP_NPC(s16,self+0x1112)=0;R_WARP_NPC(u8,self+0x1111)=flags|4;
    warp_npc_dance(self,0);
    const s32 staff=R_WARP_NPC(s32,self+0x111C);
    const u32 value=warp_npc_substance(wwhd_dComIfG_getGameInfo()+0x52C4,staff,WARP_NPC_DATA(0x10022F94),3);
    R_WARP_NPC(u8,self+0x1116)=value?(u8)R_WARP_NPC(s32,value):1;
}
static void warp_npc_makar_set(u32 self,u32 data){
    const u32 pmf[2]={R_WARP_NPC(u32,data),R_WARP_NPC(u32,data+4)};
    warp_npc_makar_action(self,(u32)pmf,0);
}
WWHD_REPLACE(0x0221F3C0,s32,randomizer_makar_init,(u32 self)){
    R_WARP_NPC(u32,self+0x374)=WWHD_GAME_DATA(0x10466C08,u32); /* HIO gravity, bit copy */
    WWHD_GAME_DATA(0x101BDC2C,u8)=0;WWHD_GAME_DATA(0x101BDC2D,u8)=0;
    WWHD_GAME_DATA(0x101BDC28,s16)=0;WWHD_GAME_DATA(0x101BDC2A,u16)=0;
    R_WARP_NPC(u8,self+0x389)=0xAB;R_WARP_NPC(u8,self+0x38B)=0xAB;R_WARP_NPC(u8,self+0x38C)=8;
    for(u32 i=0;i<5;i++){
        const u32 name=R_WARP_NPC(u32,WARP_NPC_DATA(0x101BD9E0)+i*4);
        const s32 event=warp_npc_event_index(wwhd_dComIfG_getGameInfo()+0x52C4,name,255);
        R_WARP_NPC(s16,self+0xAEC+i*2)=(s16)event;
    }
    R_WARP_NPC(s8,self+0xAEB)=-1;R_WARP_NPC(s8,self+0xAE5)=-1;
    R_WARP_NPC(u32,self+0x104)=0x0221EC9C;R_WARP_NPC(u32,self+0x100)=0x0221ED00;
    R_WARP_NPC(u32,self+0x3BC)|=0x40;
    const s32 parameter=R_WARP_NPC(s32,self+0xB0);
    if(parameter==5||parameter==1||parameter==2){
        warp_npc_makar_set(self,WARP_NPC_DATA(0x10018C48));
        R_WARP_NPC(s8,self+0x2FE)=-1;R_WARP_NPC(f32,self+0x374)=-0.1f;R_WARP_NPC(s8,self+0x326)=-1;
    }else if(parameter==0){
        /* Upstream replaces the boss query itself with constant true. */
        const u32 pmf[2]={0x0000FFFFu,0x02223048u};
        warp_npc_makar_action(self,(u32)pmf,0);
    }else if(parameter!=3&&parameter!=4&&warp_npc_makar_ship(self)){
        R_WARP_NPC(u8,self+0x38B)=0xAF;R_WARP_NPC(u8,self+0x389)=0xAF;
    }else{
        const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        if(warp_npc_event(save+0x644,0x1610))R_WARP_NPC(u32,self+0x3BC)&=~0x40u;
        if(R_WARP_NPC(u32,self+0xB0)==3)R_WARP_NPC(u32,self+0x3BC)|=2;
        warp_npc_makar_set(self,WARP_NPC_DATA(0x10018BE0));
        WWHD_GAME_DATA(0x101BDC2A,u16)&=(u16)~0x10;
    }
    warp_npc_status(self+0x7C0,0xFE,0xFF,self);
    warp_npc_cylinder(self+0x7FC,WARP_NPC_DATA(0x101BD958));R_WARP_NPC(u32,self+0x840)=self+0x7C0;
    warp_npc_cylinder(self+0x92C,WARP_NPC_DATA(0x101BD99C));R_WARP_NPC(u32,self+0x970)=self+0x7C0;
    return 1;
}
