/* Complete CC0 daNpc_Cb1_c::create. Fold the existing randomizer's skipped
 * first sword branch locally; keep its real query and all remaining checks. */
#pragma once
static u32 rs_makar_create_body(u32 actor){
 if(!(RS32(actor,0x2E4)&8)){if(actor)rs_makar_construct(actor);RS32(actor,0x2E4)|=8;}
 if(RS32(actor,0xB0)!=0){
  (void)rs_item(0x3E);
  if(rs_event(0x2910)){if(RS32(actor,0xB0)!=4)return 5;}
  else if(rs_event(0x2E02)){if(RS32(actor,0xB0)!=3)return 5;}
  else if(rs_event(0x1610)){s32 error=1;if(rs_stage(0x10018F54,0x10018CBC)&&rs_event(0x1604))error=0;if(error)return 5;}
  else if(rs_item(0x3A)){if(RS32(actor,0xB0)!=2)return 5;}
  else if(rs_event(0x1820))return 5;
  else if(rs_item(0x6B)){if(RS32(actor,0xB0)!=1)return 5;}
  else return 5;
 }
 const u32 phase=rs_load(actor+0x608,0x10018F58);
 if(phase==4){
  if(RS32(actor,0xB0)==4){rs_reset_priest(rs_save(),1,randomizer_sage_makar_spawns);rs_check_restart(actor,1);}
  if(RSP(wwhd_dComIfG_getGameInfo(),0x5B38))return 5;
  if((unsigned int)(RS32(actor,0xB0)-1u)<=4)RS16((u32)&WWHD_GAME_DATA(0x101BDC2A,u8),0)|=0x10;
  if(!rs_heap(actor,0x0221EC98,0x9400))return 5;
  rs_makar_matrix(actor);const u32 model=RSP(RSP(actor,0x610),0x90);RSP(actor,0x348)=model?model+0xC8:0;
  if(!rs_makar_init(actor))return 5;rs_layer(actor);WWHD_GAME_DATA(0x101CEF74,u32)=actor;
 }
 return phase;
}

/* Preserve legacy nesting isolation for the separate Fishmen adapter. This
 * scope supplies no sage predicate override; all sage queries remain genuine. */
WWHD_REPLACE(0x0221F700,u32,randomizer_makar_create,(u32 actor)){
 const struct randomizer_spawn_queries previous=randomizer_spawn_query;
 randomizer_spawn_query=(struct randomizer_spawn_queries){0};
 const u32 result=rs_makar_create_body(actor);
 randomizer_spawn_query=previous;return result;
}
