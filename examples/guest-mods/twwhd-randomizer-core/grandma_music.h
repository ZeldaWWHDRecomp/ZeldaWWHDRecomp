/* Complete public CC0 JAIZelBasic::setScene (0202677C), from JAIZelBasic_7.cpp.
 * TWWHD Randomizer 1.2.0's MIT Grandma change removes the shield/layer-9
 * branch in scene 0xB. Outset skips the intro prerequisite and uses the
 * persistent stage-13 chest-5 state, independently of active stage memory.
 * HD offsets are the public header's WWHD_OFFSET values, not its GC field names. */
#pragma once
#ifndef R_GRANDMA
#define R_GRANDMA(type,address) (*(volatile type*)(address))
#endif
#ifndef GRANDMA_SCENE_TABLE
#define GRANDMA_SCENE_TABLE ((u32)&WWHD_GAME_DATA(0x1018E428,u8))
#endif
#ifndef GRANDMA_ISLAND_TABLE
#define GRANDMA_ISLAND_TABLE ((u32)&WWHD_GAME_DATA(0x1018E60C,u8))
#endif
WWHD_GAME_FUNC(0x0201E480,s32,grandma_check_event,(u32 self,u32 selector));
WWHD_GAME_FUNC(0x02023608,u32,grandma_expand_bgm,(u32 self,u32 bgm));
WWHD_GAME_FUNC(0x020264F8,void,grandma_change_scene,(u32 self,u32 bgm,u32 wave1,u32 wave2,s32 keep));
WWHD_GAME_FUNC(0x025B9100,s32,grandma_stage_item,(u32 memory,u32 flag));
WWHD_GAME_FUNC(0x025B8C74,s32,grandma_stage_chest,(u32 memory,u32 chest));
static s32 grandma_outset_bgm(void) {
    const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    return grandma_stage_chest(save+0x3A0u+13u*0x24u,5);
}
static s32 grandma_has_stage_item(u32 offset,u32 flag) {
    /* Reload the allocation for each native query: callbacks can change it. */
    const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    return grandma_stage_item(save+offset,flag);
}
static void grandma_change_from_entry(u32 self,u32 bgm,u32 entry,s32 keep) {
    const u8 wave2=R_GRANDMA(u8,entry+3);
    const u8 wave1=R_GRANDMA(u8,entry+2);
    grandma_change_scene(self,bgm,wave1,wave2,keep);
}
WWHD_REPLACE(0x0202677C,void,grandma_set_scene,(u32 self,s32 sc,s32 room,s32 keep,s32 layer)) {
    enum { GM_None,GM_Special,GM_Table,GM_ExpandedSpecial,GM_AlreadyChanged };
    if(sc>=0x79)return;
    if(R_GRANDMA(u8,self+0x29C)!=0)return;
    R_GRANDMA(u8,self+0xCD)=0;
    R_GRANDMA(u8,self+0xDD)=0;
    if(sc==0x12) {
        const u32 island=room>0?((u32)room&255u):0;
        R_GRANDMA(u8,self+0x2A8)=(u8)island;
        u32 bgm;
        if(keep==1)bgm=0;
        else {
            u32 selection=GM_Table,special=0;
            if(island==44) {
                if(layer==10)selection=GM_None;
                else {
                    s32 a;
                    if(grandma_check_event(self,1)==1) {
                        const s32 event=grandma_check_event(self,0x101);
                        if(event==0) {selection=GM_Special;special=0x8000000Eu;}
                        a=event==1;
                    } else a=grandma_check_event(self,0x101)==1;
                    if(selection==GM_Table) {
                        const s32 event=grandma_outset_bgm();
                        if(a&&event==0)selection=GM_None;
                        else if(event==1) {selection=GM_Special;special=0x80000055u;}
                    }
                }
            } else if(island==11) {
                if(grandma_check_event(self,0x2E01)==0)selection=GM_None;
            } else if(island==13) {
                if(layer==8||layer==10||layer==11)selection=GM_None;
            } else if(island==14) {
                if(layer==2||layer==3) {
                    R_GRANDMA(u8,self+0xCD)=1;
                    grandma_change_scene(self,0,0,0,keep);
                    R_GRANDMA(s32,self+0x290)=sc;
                    R_GRANDMA(u8,self+0x29D)=0;
                    R_GRANDMA(u8,self+0x29C)=1;
                    R_GRANDMA(u8,self+0x2A9)=(u8)layer;
                    return;
                }
            }
            if(selection==GM_None)bgm=0;
            else if(selection==GM_Special)bgm=special;
            else bgm=grandma_expand_bgm(self,R_GRANDMA(u16,GRANDMA_ISLAND_TABLE+island*4u));
        }
        grandma_change_scene(self,bgm,0,0,keep);
    } else {
        R_GRANDMA(u8,self+0x2A8)=0;
        const u32 entry=GRANDMA_SCENE_TABLE+(u32)sc*4u;
        u32 selection=GM_Table,special=0;
        s32 dungeon=0,set274=0;
        u32 offset=0,alternate=0;
        switch(sc) {
        case 0x10:
            if(grandma_check_event(self,0x280)==0) {selection=GM_Special;special=0x80000016u;}
            break;
        case 0x13:
            if(grandma_check_event(self,0x801)==0)selection=GM_None;
            break;
        case 2:
            dungeon=1;offset=0x40C;alternate=GRANDMA_SCENE_TABLE+4;set274=1;
            break;
        case 7:
            dungeon=1;offset=0x430;alternate=GRANDMA_SCENE_TABLE+12;
            break;
        case 0x25:
            dungeon=1;offset=0x454;alternate=GRANDMA_SCENE_TABLE+40;
            break;
        case 0x18:case 0x2D:
            if(R_GRANDMA(u8,self+0x31)==0)break;
            offset=sc==0x18?0x49C:0x478;
            if(grandma_has_stage_item(offset,5)!=0)R_GRANDMA(u8,self+0xDD)=1;
            if((u32)layer-8u<=3u)selection=GM_None;
            else if(grandma_has_stage_item(offset,3)!=0)selection=GM_None;
            break;
        case 0x59:
            if((u32)layer-8u<=3u) {selection=GM_None;break;}
            /* fall through */
        case 0x36:
            if(grandma_check_event(self,0x2D04)==0) {selection=GM_Special;special=0x80000034u;}
            break;
        case 0x35:case 0x27:case 0x29:case 0x16:case 0x2C:
            if((u32)layer-8u<=3u)selection=GM_None;
            break;
        case 0xB:
            (void)R_GRANDMA(u8,self+0x53); /* Upstream retains the load. */
            /* Upstream's unconditional branch skips both the shield test and
             * its layer-9 no-BGM path, then performs the real healing query. */
            if(grandma_check_event(self,0x2A20)==0) {selection=GM_Special;special=0x80000044u;}
            break;
        case 0x20:
            if(grandma_check_event(self,0x2110)==0&&grandma_check_event(self,0xA02)==1) {
                selection=GM_Special;special=0x8000003Du;
            }
            break;
        }
        if(dungeon&&R_GRANDMA(u8,self+0x31)!=0) {
            if(grandma_has_stage_item(offset,5)!=0)R_GRANDMA(u8,self+0xDD)=1;
            if(grandma_has_stage_item(offset,3)!=0) {
                const u32 bgm=grandma_expand_bgm(self,R_GRANDMA(u16,alternate));
                if(set274)R_GRANDMA(u8,self+0x274)=1;
                grandma_change_from_entry(self,bgm,entry,keep);
                selection=GM_AlreadyChanged;
            }
        }
        if(selection==GM_None)grandma_change_from_entry(self,0,entry,keep);
        else if(selection==GM_Special)grandma_change_from_entry(self,special,entry,keep);
        else if(selection==GM_Table) {
            const u32 bgm=grandma_expand_bgm(self,R_GRANDMA(u16,entry));
            grandma_change_from_entry(self,bgm,entry,keep);
        }
    }
    R_GRANDMA(u8,self+0x29D)=0;
    R_GRANDMA(u8,self+0x2A9)=(u8)layer;
    R_GRANDMA(u8,self+0x29C)=1;
    R_GRANDMA(s32,self+0x290)=sc;
}
