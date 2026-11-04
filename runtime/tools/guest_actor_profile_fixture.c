// SPDX-License-Identifier: MPL-2.0
// Opt-in live fixture; compile as a trusted guest module in a private test run.
// It creates one invisible actor after Link begins executing, without assets.
#include "wwhd_guest.h"
WWHD_GAME_FUNC(0x025D4ED0,u32,actor_ctor,(u32 self));
WWHD_GAME_FUNC(0x025D58B4,u32,actor_create,(const char* name,u32 parameters,const f32* pos,s32 room,const void* angle,const void* scale,u32 create));
WWHD_GAME_FUNC(0x025E10E0,u32,profile_get,(u32 id));
WWHD_GAME_FUNC(0x025C109C,u32,name_get,(const char* name));
WWHD_GAME_FUNC(0x025C10E8,const char*,name_reverse,(u32 id,s32 subtype));
static s32 create(void* self){
    u32* flags=(u32*)((u8*)self+0x2E4);
    if(!(*flags&8)){actor_ctor((u32)self);*flags|=8;}
    wwhd_log("actor-profile fixture: create callback");return 4;
}
static s32 remove_actor(void* self){(void)self;wwhd_log("actor-profile fixture: delete callback");return 1;}
static s32 execute(void* self){static u32 seen;(void)self;if(!seen++)wwhd_log("actor-profile fixture: execute callback");return 1;}
static s32 is_delete(void* self){(void)self;return 1;}
static s32 draw(void* self){static u32 seen;(void)self;if(!seen++)wwhd_log("actor-profile fixture: draw callback");return 1;}
static const void* methods[]={create,remove_actor,execute,is_delete,draw};
// Exact 0x30 native profile layout, including the 16-bit process ID.
typedef struct {s32 layer;u16 list; s16 list_priority; s16 id;u16 pad;u32 base;u32 size,extra,parameters;u32 actor_base;s16 priority;u16 pad2;const void* methods;u32 status;u8 group,cull,pad3[2];} Profile;
static const Profile profile={-3,7,-3,0x1E4,0,0,0x3AC,0,0,0,0x100,0,methods,0,0,0,{0,0}};
static const Profile second={-3,7,-3,0x1E5,0,0,0x3AC,0,0,0,0x101,0,methods,0,0,0,{0,0}};
static const wwhd_native_actor_profile_v1 registration={sizeof(wwhd_native_actor_profile_v1),1,&profile,"SdkBean",-1,0};
static const wwhd_native_actor_profile_v1 second_registration={sizeof(wwhd_native_actor_profile_v1),1,&second,"SdkOther",-1,0};
WWHD_HOOK(0x028EA120,register_profiles,(void)){
    wwhd_log_int("actor-profile fixture: register first",wwhd_native_actor_profile_register(&registration));
    wwhd_log_int("actor-profile fixture: register second",wwhd_native_actor_profile_register(&second_registration));
    wwhd_log_hex("actor-profile fixture: lookup",profile_get(0x1E4));
    wwhd_log_hex("actor-profile fixture: name lookup",name_get("SdkBean"));
    wwhd_log(name_reverse(0x1E4,-1));
}
WWHD_HOOK(0x0240EBB0,spawn_profile_fixture,(void* link)){
    static u32 once;(void)link;if(once++)return;
    static const f32 position[3]={0,0,0};
    wwhd_log_int("actor-profile fixture: late registration",wwhd_native_actor_profile_register(&registration));
    wwhd_log_hex("actor-profile fixture: create request",actor_create("SdkBean",0,position,-1,0,0,0));
}
