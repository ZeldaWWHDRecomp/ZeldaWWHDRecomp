/* Full public CC0 consumers: d_a_door10.cpp, d_a_door12.cpp, d_a_knob00.cpp.
 * Entrance behavior from TWWHD Randomizer 1.2.0, MIT (see bundled license).
 * Only the conditional entrance group changes behavior; saved keys/Deed bits
 * are never forged. All native helper calls and event writes keep source order. */
#pragma once
#ifndef R_ENTRANCE
#define R_ENTRANCE(type,address) (*(volatile type*)(address))
#endif
WWHD_GAME_FUNC(0x0252AA2C,u32,entrance_door_type,(u32 self));
WWHD_GAME_FUNC(0x0252AA58,u32,entrance_door_arg,(u32 self));
WWHD_GAME_FUNC(0x021264C4,s32,entrance_door10_stop_back,(u32 self));
WWHD_GAME_FUNC(0x021263D0,s32,entrance_door10_stop_front,(u32 self));
WWHD_GAME_FUNC(0x02127038,f32,entrance_door10_size,(u32 self));
WWHD_GAME_FUNC(0x02128638,s32,entrance_door12_stop_back,(u32 self));
WWHD_GAME_FUNC(0x0212854C,s32,entrance_door12_stop_front,(u32 self));
WWHD_GAME_FUNC(0x02127E10,s32,entrance_door12_shape,(u32 self));
WWHD_GAME_FUNC(0x025B9100,s32,entrance_has_dungeon_item,(u32 memory,u32 item));
WWHD_GAME_FUNC(0x0252AE04,s32,entrance_check_area,(u32 self,f32 x,f32 y,f32 z));
WWHD_GAME_FUNC(0x025D5218,u32,entrance_search_actor,(u32 callback,s32* actor_id));
WWHD_GAME_FUNC(0x025E1234,u32,entrance_search_id,(u32 actor,s32* actor_id));
WWHD_GAME_FUNC(0x025B8B94,s32,entrance_event_bit,(u32 event,u32 selector));
WWHD_GAME_FUNC(0x021A4318,void,entrance_knob_set_event,(u32 self));
WWHD_GAME_FUNC(0x0252A2DC,void,entrance_init_open_demo,(u32 self,s32 value));
WWHD_GAME_FUNC(0x021A3FB8,s32,entrance_knob_demo,(u32 self));
#ifndef R_ENTRANCE_SEARCH_CALLBACK
#define R_ENTRANCE_SEARCH_CALLBACK ((u32)entrance_search_id)
#endif
#ifndef R_ENTRANCE_GRAB_ID
static s32 entrance_grab_id(u32 player) {
    const u32 table=R_ENTRANCE(u32,player+0xB4);
    return ((s32 (*)(u32))R_ENTRANCE(u32,table+0xBC))(player);
}
#define R_ENTRANCE_GRAB_ID(player) entrance_grab_id(player)
#endif
static void entrance_publish_event(u32 self) {
    const u8 action=R_ENTRANCE(u8,self+0x3E2);
    const u16 flags=R_ENTRANCE(u16,self+0xFA);
    R_ENTRANCE(s16,self+0xFC)=R_ENTRANCE(s16,self+0x3BE + action*2);
    const u8 tool=R_ENTRANCE(u8,self+0x3D6+action);
    R_ENTRANCE(u16,self+0xFA)=(u16)(flags|4);
    R_ENTRANCE(u8,self+0xFE)=tool;
}
WWHD_GAME_ORIGINAL(0x02127080,void,entrance_native_door10_event,(u32 self));
WWHD_REPLACE(0x02127080,void,entrance_door10_event,(u32 self)) {
    if(!init_entrances_shuffled) {entrance_native_door10_event(self);return;}
    if(R_ENTRANCE(u8,self+0x3BC)==0) {
        R_ENTRANCE(u8,self+0x3E2)=2;
        if(R_ENTRANCE(u8,self+0x4CE)==255)
            R_ENTRANCE(u8,self+0x4CE)=(u8)entrance_door10_stop_back(self);
    } else {
        R_ENTRANCE(u8,self+0x3E2)=3;
        if((u8)entrance_door_type(self)==1&&(u8)entrance_door_arg(self)!=16)return;
        if(R_ENTRANCE(u8,self+0x4CE)==255)
            R_ENTRANCE(u8,self+0x4CE)=(u8)entrance_door10_stop_front(self);
    }
    if(R_ENTRANCE(u8,self+0x4CC)!=0)return;
    if((u8)entrance_door_type(self)==1&&(u8)entrance_door_arg(self)!=16)
        R_ENTRANCE(u8,self+0x3E2)=6;
    else if(R_ENTRANCE(u8,self+0x4CE)==1)
        R_ENTRANCE(u8,self+0x3E2)=(u8)(R_ENTRANCE(u8,self+0x3E2)+2);
    if(R_ENTRANCE(u8,self+0x424)) {
        const u8 type=(u8)entrance_door_type(self);
        const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        if(type==1) {
            if(!entrance_has_dungeon_item(save+0x798,2))return;
        } else if(R_ENTRANCE(u8,self+0x3BC)==0&&R_ENTRANCE(u8,save+0x7B8)==0)return;
    }
    const f32 size=entrance_door10_size(self);
    if(entrance_check_area(self,size,12100.0f,62500.0f))entrance_publish_event(self);
}
WWHD_GAME_ORIGINAL(0x0212919C,void,entrance_native_door12_event,(u32 self));
WWHD_REPLACE(0x0212919C,void,entrance_door12_event,(u32 self)) {
    if(!init_entrances_shuffled) {entrance_native_door12_event(self);return;}
    if(R_ENTRANCE(u8,self+0x3BC)==0) {
        R_ENTRANCE(u8,self+0x3E2)=2;
        if(R_ENTRANCE(u8,self+0x4AA)==255)
            R_ENTRANCE(u8,self+0x4AA)=(u8)entrance_door12_stop_back(self);
    } else {
        R_ENTRANCE(u8,self+0x3E2)=3;
        if((u8)entrance_door_type(self)==3)return;
        if(R_ENTRANCE(u8,self+0x4AA)==255)
            R_ENTRANCE(u8,self+0x4AA)=(u8)entrance_door12_stop_front(self);
    }
    if(R_ENTRANCE(u8,self+0x4A8)!=0)return;
    if((u8)entrance_door_type(self)==3)R_ENTRANCE(u8,self+0x3E2)=6;
    else if(R_ENTRANCE(u8,self+0x4AA)==1)
        R_ENTRANCE(u8,self+0x3E2)=(u8)(R_ENTRANCE(u8,self+0x3E2)+2);
    if(entrance_door12_shape(self)==1||entrance_door12_shape(self)==2||
       entrance_door12_shape(self)==4||entrance_door12_shape(self)==5) {
        const u32 player=R_ENTRANCE(u32,wwhd_dComIfG_getGameInfo()+0x5B2C);
        const s32 id=R_ENTRANCE_GRAB_ID(player);
        if(id!=-1) {
            s32 key=id;
            if(entrance_search_actor(R_ENTRANCE_SEARCH_CALLBACK,&key))R_ENTRANCE(u8,self+0x3E2)=11;
        }
    }
    if(R_ENTRANCE(u8,self+0x400)) {
        const u8 type=(u8)entrance_door_type(self);
        const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        if(type==3) {
            if(!entrance_has_dungeon_item(save+0x798,2))return;
        } else if(R_ENTRANCE(u8,self+0x3BC)==0&&R_ENTRANCE(u8,save+0x7B8)==0)return;
    }
    if(!entrance_check_area(self,12100.0f,12100.0f,62500.0f))return;
    entrance_publish_event(self);
}
WWHD_GAME_ORIGINAL(0x021A44DC,s32,entrance_native_cabana,(u32 self));
WWHD_REPLACE(0x021A44DC,s32,entrance_cabana,(u32 self)) {
    if(!init_entrances_shuffled)return entrance_native_cabana(self);
    if(R_ENTRANCE(u16,self+0xF8)==3) {
        entrance_init_open_demo(self,0);
        R_ENTRANCE(u8,self+0x5A4)=7;
        entrance_knob_demo(self);
    } else if(entrance_event_bit(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x2D80))R_ENTRANCE(u8,self+0x5A4)=1;
    else if(R_ENTRANCE(u8,self+0x3BC)!=0) {
        R_ENTRANCE(u8,self+0x5A4)=1;
        entrance_knob_set_event(self);
        /* Upstream restores VILLA, not the entry state: actionInit also calls
         * this method before it has written the door's initial VILLA state. */
        R_ENTRANCE(u8,self+0x5A4)=8;
    } else entrance_knob_set_event(self);
    return 1;
}
