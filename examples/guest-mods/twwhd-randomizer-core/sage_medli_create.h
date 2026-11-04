/* Complete CC0 daNpc_Md_c::create, including its inline constructor.
 * The initial collection predicate is called genuinely and its branch skipped. */
#pragma once
static u32 rs_medli_create_body(u32 actor){
 RS8(actor,0x4259)=0;rs_copy_string(actor+0x43A0,WWHD_GAME_DATA(0x101C0278,u32));u32 heap_index=0;
 if(!(RS32(actor,0x2E4)&8)){if(actor)rs_medli_construct(actor);RS32(actor,0x2E4)|=8;}
 RS8(actor,0x4254)=(u8)(RS32(actor,0xB0)>>8);
 (void)rs_collect(rs_save()+0xD4,0,2);
 if(rs_stage(0x1001DC74,0x1001D6A0)){
  if(rs_event(0x2E04)||!rs_event(0x1820)||!rs_stage_boss(3))return 5;
 }else if(rs_stage(0x1001DC54,0x1001D6A0)){
  if(rs_event(0x2E04)||rs_letter())return 5;
 }else if(rs_stage(0x1001DC5C,0x1001D6A0)){
  if(rs_event(0x2E04)||!rs_letter())return 5;
 }else if(rs_stage(0x1001DC64,0x1001D6A0)){
  if(rs_event(0x2E04)||rs_event(0x1101))return 5;
 }else if(rs_stage(0x1001DC6C,0x1001D6A0)){
  if(!rs_event(0x2E04)||rs_event(0x2920))return 5;RS8(actor,0x4254)=4;
 }else if(rs_stage(0x1001DC78,0x1001D6A0)){
  if(!rs_event(0x2E04)||!rs_event(0x2920))return 5;RS8(actor,0x4254)=5;
 }else if(rs_stage(0x1001DC80,0x1001D6A0))return 5;
 if(!rs_event(0x2E04)&&rs_event(0x1608)){RS8(actor,0x4254)=7;rs_copy_string(actor+0x43A0,WWHD_GAME_DATA(0x101C027C,u32));heap_index=1;}
 const u32 phase=rs_load(actor+0x608,actor+0x43A0);RS8(actor,0x4259)=1;
 if(phase==4){
  if(RSP(wwhd_dComIfG_getGameInfo(),0x5B38))return 5;
  const u32 heap_size=RS32((u32)&WWHD_GAME_DATA(0x101C07FC,u8),4*heap_index);
  if(!rs_heap(actor,0x02285380,heap_size)){RSP(actor,0x61C)=0;RSP(actor,0x618)=0;RSP(actor,0x620)=0;return 5;}
  if(RS8(actor,0x4254)==5){rs_reset_priest(rs_save(),2,randomizer_sage_medli_spawns);rs_check_restart(actor,2);}
  const u32 model=RSP(RSP(actor,0x618),0x90);RSP(actor,0x348)=model?model+0xC8:0;
  if(!rs_medli_init(actor))return 5;rs_medli_matrix(actor);rs_layer(actor);WWHD_GAME_DATA(0x101CEF74,u32)=actor;
 }
 return phase;
}

/* Preserve legacy nesting isolation for the separate Fishmen adapter. This
 * scope supplies no sage predicate override; all sage queries remain genuine. */
WWHD_REPLACE(0x02286084,u32,randomizer_medli_create,(u32 actor)){
 const struct randomizer_spawn_queries previous=randomizer_spawn_query;
 randomizer_spawn_query=(struct randomizer_spawn_queries){0};
 const u32 result=rs_medli_create_body(actor);
 randomizer_spawn_query=previous;return result;
}
