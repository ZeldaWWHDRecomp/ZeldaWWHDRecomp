/* Boss-source propagation from TWWHD Randomizer flexible_item_locations.asm.
 * Copyright (c) 2024 SuperDude88. MIT: see LICENSE-TWWHD-Randomizer.txt.
 * Native flow/layout verified against public CC0 f_op_actor_mng_3/4.cpp,
 * d_a_disappear.cpp, d_a_boss_item.cpp and d_item.cpp. */
#pragma once

/* Scope only the native producer calls; unrelated/unused callers keep vanilla behavior.
 * A second state separates cloud producers from the eventual item factory call. */
static u32 randomizer_boss_source=0xFFFFFFFFu;
static u32 randomizer_boss_factory_active,randomizer_boss_factory_item;
#define RANDO_BOSS_CLOUD_SCOPE(address,name,index,result) \
 WWHD_GAME_ORIGINAL(address,result,name##_native,(u32 actor)); \
 WWHD_REPLACE(address,result,name,(u32 actor)) { \
  const u32 previous=randomizer_boss_source;randomizer_boss_source=index; \
  result value=name##_native(actor);randomizer_boss_source=previous;return value; \
 }
/* Kalle's Execute returns BOOL; the other three producer functions return void. */
RANDO_BOSS_CLOUD_SCOPE(0x020AEBCC,randomizer_boss_kalle,1,u32)
#undef RANDO_BOSS_CLOUD_SCOPE
#define RANDO_BOSS_CLOUD_SCOPE(address,name,index) \
 WWHD_GAME_ORIGINAL(address,void,name##_native,(u32 actor)); \
 WWHD_REPLACE(address,void,name,(u32 actor)) { \
  const u32 previous=randomizer_boss_source;randomizer_boss_source=index; \
  name##_native(actor);randomizer_boss_source=previous; \
 }
RANDO_BOSS_CLOUD_SCOPE(0x020F52B8,randomizer_boss_gohma,0)
RANDO_BOSS_CLOUD_SCOPE(0x0206BAA8,randomizer_boss_helmaroc,3)
RANDO_BOSS_CLOUD_SCOPE(0x020D8564,randomizer_boss_jalhalla,4)
#undef RANDO_BOSS_CLOUD_SCOPE

WWHD_GAME_ORIGINAL(0x025D99E8,u32,randomizer_native_boss_cloud,
 (u32 actor,u32 pos,u32 scale,u32 drop,u32 bit));
WWHD_REPLACE(0x025D99E8,u32,randomizer_create_boss_cloud,
 (u32 actor,u32 pos,u32 scale,u32 drop,u32 bit)) {
 /* Upstream stores item in the unused parameter byte24..31, retaining bitFF.
  * Scale/drop remain native; other clouds in these same Execute calls stay native. */
 if(randomizer_boss_source<6 && drop==2 && bit==0xFF)
  bit=((u32)init_reward_boss[randomizer_boss_source]<<8)|0xFF;
 return randomizer_native_boss_cloud(actor,pos,scale,drop,bit);
}

#define RANDO_BOSS_FACTORY_SCOPE(address,name,index) \
 WWHD_GAME_ORIGINAL(address,void,name##_native,(u32 actor)); \
 WWHD_REPLACE(address,void,name,(u32 actor)) { \
  const u32 active=randomizer_boss_factory_active,item=randomizer_boss_factory_item; \
  randomizer_boss_factory_active=1;randomizer_boss_factory_item=init_reward_boss[index]; \
  name##_native(actor);randomizer_boss_factory_active=active;randomizer_boss_factory_item=item; \
 }
RANDO_BOSS_FACTORY_SCOPE(0x020EE200,randomizer_boss_gohdan,2)
/* Molgera02101F30 composes this scope in its complete molgera_camera.h body. */
#undef RANDO_BOSS_FACTORY_SCOPE

WWHD_GAME_ORIGINAL(0x0212451C,u32,randomizer_native_boss_cloud_execute,(u32 actor));
WWHD_REPLACE(0x0212451C,u32,randomizer_boss_cloud_execute,(u32 actor)) {
 const u32 active=randomizer_boss_factory_active,item=randomizer_boss_factory_item;
 /* Native timer/delete, ordinary drops and iball flows run unchanged. Only the
  * heart-container discriminator invokes createItemForBoss. All four native
  * sources for this discriminator are scoped above. */
 if(*(volatile u8*)(actor+0xB1)==0xFF && *(volatile u8*)(actor+0x3A1)==2 && *(volatile s16*)(actor+0x3B8)==1) {
  randomizer_boss_factory_active=1;randomizer_boss_factory_item=*(volatile u8*)(actor+0xB0);
 }
 const u32 result=randomizer_native_boss_cloud_execute(actor);
 randomizer_boss_factory_active=active;randomizer_boss_factory_item=item;return result;
}
WWHD_GAME_ORIGINAL(0x020D0A70,u32,randomizer_native_boss_respawn,(u32 actor));
WWHD_REPLACE(0x020D0A70,u32,randomizer_boss_respawn,(u32 actor)) {
 const u32 active=randomizer_boss_factory_active,item=randomizer_boss_factory_item;
 randomizer_boss_factory_active=1;randomizer_boss_factory_item=*(volatile u8*)(actor+0xB2);
 const u32 result=randomizer_native_boss_respawn(actor);
 randomizer_boss_factory_active=active;randomizer_boss_factory_item=item;return result;
}
WWHD_GAME_ORIGINAL(0x025D8A5C,u32,randomizer_native_boss_factory,
 (u32 pos,u32 unused,u32 room,u32 angle,u32 scale,u32 mode));
WWHD_GAME_FUNC(0x025D8870,u32,randomizer_boss_create_item,
 (u32 pos,u32 item,u32 bit,u32 room,u32 type,u32 angle,u32 action,u32 scale));
WWHD_REPLACE(0x025D8A5C,u32,randomizer_boss_factory,
 (u32 pos,u32 unused,u32 room,u32 angle,u32 scale,u32 mode)) {
 if(!randomizer_boss_factory_active)return randomizer_native_boss_factory(pos,unused,room,angle,scale,mode);
 /* Native callers retain their original r4 and its copies into other arguments.
  * Only this source-scoped item and pickup bit change; actionC is mode==1 only. */
 return randomizer_boss_create_item(pos,randomizer_boss_factory_item,0x15,room,3,angle,mode==1?0xC:5,scale);
}
WWHD_REPLACE(0x0254DC1C,void,randomizer_heart_container_grant,(void)) {
 const u32 info=wwhd_dComIfG_getGameInfo();
 volatile s16* pending_max=(volatile s16*)(info+0x5B5E);
 *pending_max=(s16)(*pending_max+4);
 const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
 volatile f32* pending_life=(volatile f32*)(info+0x5B44);
 *pending_life=*pending_life+(f32)*(volatile u16*)(save+0x20);
 /* Ownership belongs to source pickup15, never the current stage's boss flag. */
}
