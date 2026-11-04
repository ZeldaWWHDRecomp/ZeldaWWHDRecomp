/* CC0 actions inlined by GHS into move0213B3AC. */
#pragma once
static s32 pgm_wall_check(u32 actor){
 const u32 player=R_PGM(u32,pgm_game()+0x5B2C);unsigned int line[27];pgm_line_init(line);
 f32 offset[3]={0.0f,0.0f,pgm_hio_float(0x30)+100.0f},transformed[3];u32 temporary[3],end[3];s32 hit=0;
 for(u32 i=0;i<8;i++){
  pgm_y_rotation(pgm_matrix(),(s16)(i*0x2000));pgm_transform((u32)offset,(u32)transformed);
  pgm_vector_add(player+0x37C,(u32)temporary,(u32)transformed);pgm_copy_vector((u32)end,(u32)temporary);
  pgm_line_set((u32)line,player+0x37C,(u32)end,actor);
  if(pgm_line_cross(pgm_game()+0x12A0,(u32)line)){hit=1;break;}
 }
 pgm_line_finish(line);return hit;
}
static void pgm_emerge(u32 actor){
 (void)pgm_game();R_PGM(s16,actor+0x5E2)=3;if(R_PGM(s16,actor+0x5BC)!=0)return;
 u8 choice=pgm_hio_byte(2);
 if(choice==0){if(R_PGM(u8,actor+0x3D8)==0)choice=pgm_random(1.0)<0.5?1:2;
  else if(pgm_random(1.0)<(f64)(f32)0.4f&&!pgm_wall_check(actor))choice=4;
  else choice=pgm_random(1.0)<0.5?3:1;}
 switch(choice){
 case 1:R_PGM(s16,actor+0x5BA)=5;R_PGM(s16,actor+0x5BC)=-10;R_PGM(s16,actor+0x5DC)=pgm_hio_short(6);break;
 case 2:R_PGM(s16,actor+0x5BA)=7;R_PGM(s16,actor+0x5BC)=0;break;
 case 3:R_PGM(s16,actor+0x5BA)=9;R_PGM(s16,actor+0x5BC)=-10;R_PGM(s16,actor+0x5DC)=pgm_hio_short(6);break;
 case 4:R_PGM(s16,actor+0x5BA)=10;R_PGM(s16,actor+0x5BC)=0;break;
 }
}
static void pgm_vanish(u32 actor){
 (void)pgm_game();R_PGM(s16,actor+0x5E2)=3;
 switch(R_PGM(s16,actor+0x5BC)){
 case 0:
  if(R_PGM(s8,actor+0x63C)==1||R_PGM(s8,actor+0x63C)==2)R_PGM(s8,actor+0x63C)=35;
  pgm_disappear(actor,0);
  if(R_PGM(u8,actor+0x8C3)){R_PGM(s16,actor+0x5BC)=1;R_PGM(s16,actor+0x5D8)=30;}
  else {R_PGM(s16,actor+0x5BA)=0;R_PGM(s16,actor+0x5BC)=0;}break;
 case 1:if(R_PGM(s16,actor+0x5D8)==0)pgm_delete(actor);break;
 }
}
static void pgm_fail(u32 actor){
 (void)pgm_game();R_PGM(s16,actor+0x5E2)=3;
 switch(R_PGM(s16,actor+0x5BC)){
 case 0:pgm_disappear(actor,1);R_PGM(s16,actor+0x5BC)=1;pgm_animation(actor,7,2.0f,0,1.0f,-1);break;
 case 1:{
  if(!pgm_stopped(actor))break;R_PGM(s16,actor+0x5BC)=2;R_PGM(s16,actor+0x5D8)=30;
  unsigned int line[27];pgm_line_init(line);pgm_y_rotation(pgm_matrix(),R_PGM(s16,actor+0x2FA));
  f32 offset[3]={0.0f,0.0f,10000.0f},transformed[3];pgm_transform((u32)offset,(u32)transformed);
  offset[0]=R_PGM(f32,actor+0x2EC);offset[1]=R_PGM(f32,actor+0x2F0)+100.0f;offset[2]=R_PGM(f32,actor+0x2F4);
  pgm_vector_add_inplace((u32)transformed,(u32)offset,(u32)transformed);
  pgm_line_set((u32)line,(u32)offset,(u32)transformed,actor);
  const u32 model=R_PGM(u32,R_PGM(u32,actor+0x3DC)+0x90);
  pgm_matrix_copy(pgm_joint_matrix(model,24),pgm_matrix());
  offset[0]=0.0f;offset[1]=0.0f;offset[2]=0.0f;pgm_transform((u32)offset,(u32)transformed);
  if(pgm_line_cross(pgm_game()+0x12A0,(u32)line))pgm_copy_vector((u32)offset,(u32)line+0x30);
  const s16 angle=(s16)pgm_atan(transformed[0]-offset[0],transformed[2]-offset[2]);
  R_PGM(s16,actor+0x8D4)=(s16)(R_PGM(s16,actor+0x32A)-angle+0x7058+WWHD_GAME_DATA(0x1047B698,s16));
  R_PGM(u32,actor+0x8DC)=100;R_PGM(s16,actor+0x8D8)=(s16)(pgm_reg_short(0,5)+0x80);
  pgm_line_finish(line);break;}
 case 2:
  if(R_PGM(s16,actor+0x5D8)==0){
   const s16 step=(s16)((s16)pgm_integer(R_PGM(f32,actor+0x8D0))+R_PGM(s16,actor+0x8D8));
   pgm_angle_calculate(actor+0x8D6,R_PGM(s16,actor+0x8D4),1,step);
   R_PGM(f32,actor+0x8C8)=R_PGM(f32,actor+0x8C8)+R_PGM(f32,actor+0x8CC);
   R_PGM(f32,actor+0x8CC)=R_PGM(f32,actor+0x8CC)+R_PGM(f32,actor+0x8D0);
   R_PGM(f32,actor+0x8D0)=R_PGM(f32,actor+0x8D0)+(pgm_reg_float(12,10)+3.0f);
   const f32 limit=pgm_reg_float(12,3)+1000.0f;if(R_PGM(f32,actor+0x8D0)>limit)R_PGM(f32,actor+0x8D0)=limit;
   if(!(R_PGM(f32,actor+0x8C8)<15884.0f)){
    R_PGM(f32,actor+0x8C8)=15884.0f;R_PGM(s16,actor+0x8D8)=(s16)(pgm_reg_short(0,6)+0x800);
    if(R_PGM(f32,actor+0x8CC)>pgm_reg_float(12,1)+700.0f){
     R_PGM(f32,actor+0x8CC)=-(R_PGM(f32,actor+0x8CC)*(pgm_reg_float(12,2)+0.4f));
     pgm_effect_sound(actor,0x6998,R_PGM(u32,actor+0x8DC));
     if(R_PGM(u32,actor+0x8DC)>=30)R_PGM(u32,actor+0x8DC)-=20;
     if(R_PGM(s16,actor+0x8DA)==0)R_PGM(s16,actor+0x8DA)=(s16)(pgm_reg_short(0,1)+30);
    }else {R_PGM(f32,actor+0x8CC)=0.0f;R_PGM(s16,actor+0x5D8)=0;R_PGM(s16,actor+0x5BC)=3;}
   }
  }break;
 case 3:
  if(R_PGM(s16,actor+0x5D8)==0){pgm_disappear(actor,2);R_PGM(s16,actor+0x5D8)=10;R_PGM(s16,actor+0x5BC)=4;
   R_PGM(u32,actor+0x8E4)=pgm_actor_create(0x1CF,5,actor+0x314,R_PGM(s8,actor+0x326),0,0,-1,0);}
  break;
 case 4:{
  const unsigned int key=R_PGM(u32,actor+0x8E4);const u32 weapon=key==0xFFFFFFFFu?0:pgm_actor_judge(0x025E1234,(u32)&key);if(!weapon)break;
  if(!(R_PGM(u32,weapon+0x2E0)&0x2000))pgm_carry(weapon,0);
  const u32 model=R_PGM(u32,actor+0x3E4),destination=R_PGM(u32,weapon+0x3B4);const u32 source=model?model+0xC8:0;
  if(destination){unsigned int matrix[12];for(u32 i=0;i<12;i++)matrix[i]=R_PGM(u32,source+4*i);for(u32 i=0;i<12;i++)R_PGM(u32,destination+0xC8+4*i)=matrix[i];}
  if(R_PGM(s16,actor+0x5D8)==0){pgm_delete(actor);R_PGM(u8,weapon+0x434)=1;pgm_carry_cancel(weapon);}break;}
 }
}
static void pgm_fly(u32 actor,s32 second){
 const u32 player=R_PGM(u32,pgm_game()+0x5B2C);unsigned int line[27];pgm_line_init(line);
 if(((R_PGM(s16,actor+0x5B8)&15)==0&&pgm_random(1.0)<0.5)||R_PGM(s16,actor+0x5BC)==-10)
  R_PGM(s16,actor+0x5CC)=R_PGM(s16,player+0x32A);
 switch(R_PGM(s16,actor+0x5BC)){
 case -10:
  pgm_appear(actor);pgm_copy_vector(actor+0x314,actor+0x2EC);
  R_PGM(f32,actor+0x318)=R_PGM(f32,player+0x318)+pgm_hio_float(second?0x14:0xC);
  R_PGM(s16,actor+0x32A)=(s16)pgm_search_angle(actor,R_PGM(u32,pgm_game()+0x5B2C));
  R_PGM(s16,actor+0x5BC)=1;pgm_animation(actor,0x16,20.0f,2,1.0f,-1);R_PGM(s16,actor+0x5DA)=second?40:60;break;
 case 0:
  pgm_animation(actor,0x16,20.0f,2,1.0f,-1);R_PGM(s16,actor+0x5BC)++;R_PGM(f32,actor+0x5D4)=0.0f;
  R_PGM(s16,actor+0x5DA)=(s16)pgm_integer((f32)(pgm_random(50.0)+50.0));/* fall through */
 case 1:{
  if(R_PGM(s8,actor+0x8BE)==1)pgm_animation(actor,0x16,10.0f,2,1.0f,-1);
  pgm_y_rotation(pgm_matrix(),R_PGM(s16,actor+0x5CC));
  f32 offset[3],transformed[3];unsigned int temporary[3];offset[0]=0.0f;
  offset[2]=pgm_hio_float(second?0x10:8);offset[1]=pgm_hio_float(second?0x14:0xC);
  pgm_transform((u32)offset,(u32)transformed);pgm_vector_add(player+0x314,(u32)temporary,(u32)transformed);
  pgm_copy_vector(actor+0x5C0,(u32)temporary);pgm_vector_subtract(actor+0x5C0,(u32)temporary,actor+0x314);
  pgm_copy_vector((u32)offset,(u32)temporary);const f64 length=pgm_sqrt(pgm_square_magnitude((u32)offset));
  if(length>(f64)(f32)(pgm_reg_float(0,12)+300.0f))pgm_calculate(actor+0x370,pgm_reg_float(0,15)+30.0f,1.0f,pgm_reg_float(0,13)+2.0f);
  else pgm_decelerate(actor+0x370,1.0f,pgm_reg_float(0,14)+1.0f);
  R_PGM(f32,actor+0x5D0)=pgm_reg_float(0,5)+1500.0f;
  const s16 yaw=(s16)pgm_search_angle(actor,R_PGM(u32,pgm_game()+0x5B2C));pgm_angle_calculate(actor+0x32A,yaw,10,0x400);
  pgm_calculate(actor+0x5F0,pgm_reg_float(0,11)+50.0f,1.0f,2.0f);break;}
 }
 pgm_position_move(actor,0);pgm_flight_sound(actor);
 if(R_PGM(s16,actor+0x5DA)==0){R_PGM(s16,actor+0x5BA)=second?11:6;R_PGM(s16,actor+0x5BC)=0;}
 if(R_PGM(s16,actor+0x5DC)==0){R_PGM(s16,actor+0x5BA)=2;R_PGM(s16,actor+0x5BC)=0;}
 pgm_line_finish(line);
}
static void pgm_shot(u32 actor){
 (void)pgm_game();const s16 yaw=(s16)pgm_search_angle(actor,R_PGM(u32,pgm_game()+0x5B2C));pgm_angle_calculate(actor+0x32A,yaw,10,0x400);
 switch(R_PGM(s16,actor+0x5BC)){
 case 0:
  if(R_PGM(s8,actor+0x63C)!=0)break;pgm_animation(actor,0x10,10.0f,2,1.0f,-1);R_PGM(s16,actor+0x5BC)++;
  R_PGM(s16,actor+0x5D8)=40;pgm_voice(actor,0x493A);/* fall through */
 case 1:
  if(R_PGM(s16,actor+0x5D8)==30)R_PGM(s8,actor+0x63C)=1;
  if(R_PGM(s16,actor+0x5D8)<30)pgm_effect_sound(actor,0x5152,0);
  if(R_PGM(s16,actor+0x5D8)==0){pgm_animation(actor,0xD,3.0f,0,1.0f,-1);R_PGM(s16,actor+0x5BC)++;pgm_voice(actor,0x493C);pgm_effect_sound(actor,0x5954,0);}break;
 case 2:
  if(pgm_integer(R_PGM(f32,R_PGM(u32,actor+0x3DC)+0x9C))==(s32)pgm_reg_short(0,0)+14){R_PGM(s8,actor+0x8BC)=0;R_PGM(s8,actor+0x8BB)=0;R_PGM(s8,actor+0x8BD)=0;R_PGM(s8,actor+0x63D)=1;}
  if(pgm_stopped(actor)){pgm_animation(actor,0x16,3.0f,2,1.0f,-1);R_PGM(s16,actor+0x5BC)++;}/* fall through */
 case 3:
  if(R_PGM(s8,actor+0x63C)==5){
   unsigned int temporary[3];f32 offset[3];pgm_vector_subtract(actor+0x614,(u32)temporary,actor+0x37C);pgm_copy_vector((u32)offset,(u32)temporary);
   const f64 length=pgm_sqrt(pgm_square_magnitude((u32)offset));
   const f32 limit=pgm_fma(R_PGM(f32,actor+0x638),pgm_reg_float(0,0)+10.0f,400.0f)+pgm_reg_float(0,1);
   if(!(length<(f64)limit))break;
   if(R_PGM(s8,actor+0x8BD)==0){
    const s32 animation=pgm_random(1.0)<0.5?0x13:0x14;pgm_animation(actor,animation,0.0f,0,1.0f,-1);R_PGM(s16,actor+0x5BC)++;
    pgm_voice(actor,0x493E);pgm_effect_sound(actor,0x5950,0);R_PGM(s8,actor+0x8BB)++;
    if(R_PGM(s8,actor+0x8BC)>=7||(R_PGM(s8,actor+0x8BB)>=4&&pgm_random(1.0)<(f64)(f32)0.3f))R_PGM(s8,actor+0x8BD)=1;
   }else if(R_PGM(u8,actor+0x3D8)==2){R_PGM(s16,actor+0x5BA)=2;R_PGM(s16,actor+0x5BC)=0;return;}
  }break;
 case 4:{
  const u32 morf=R_PGM(u32,actor+0x3DC);const s32 frame=pgm_integer(R_PGM(f32,morf+0x9C));if(frame>=5&&frame<=15)R_PGM(s8,actor+0x8BA)=1;
  if(pgm_stopped(actor)||(R_PGM(u8,actor+0x3D8)!=0&&!(R_PGM(f32,morf+0x9C)<20.0f))){pgm_animation(actor,0x16,10.0f,2,1.0f,-1);R_PGM(s16,actor+0x5BC)=3;}break;}
 }
 pgm_decelerate(actor+0x370,1.0f,pgm_reg_float(0,14)+1.0f);pgm_position_move(actor,0);pgm_flight_sound(actor);
 if(R_PGM(s16,actor+0x5BC)>=2&&R_PGM(s8,actor+0x63C)==0){R_PGM(s16,actor+0x5BA)=5;R_PGM(s16,actor+0x5BC)=0;}
 else if(R_PGM(s16,actor+0x5BC)==3||R_PGM(s16,actor+0x5BC)==4)pgm_ball_sound(0x6237,actor+0x614,100,pgm_reverb(R_PGM(s8,actor+0x326)));
}
static void pgm_spin(u32 actor){
 const u32 player=R_PGM(u32,pgm_game()+0x5B2C);unsigned int line[27];pgm_line_init(line);
 const s32 frame=pgm_integer(R_PGM(f32,R_PGM(u32,actor+0x3DC)+0x9C));
 switch(R_PGM(s16,actor+0x5BC)){
 case 0:R_PGM(s16,actor+0x5BC)=1;R_PGM(s16,actor+0x32A)=R_PGM(s16,player+0x32A);/* fall through */
 case 1:{
  pgm_y_rotation(pgm_matrix(),R_PGM(s16,actor+0x32A));f32 offset[3]={0.0f,pgm_hio_float(0x28)+100.0f,pgm_hio_float(0x2C)},transformed[3];unsigned int temporary[3];
  pgm_transform((u32)offset,(u32)transformed);pgm_vector_add(player+0x314,(u32)temporary,(u32)transformed);pgm_copy_vector(actor+0x314,(u32)temporary);
  pgm_line_set((u32)line,player+0x37C,actor+0x314,actor);
  if(pgm_line_cross(pgm_game()+0x12A0,(u32)line)){R_PGM(s16,actor+0x32A)=(s16)pgm_integer(pgm_random_signed(32768.0));break;}
  R_PGM(f32,actor+0x318)=R_PGM(f32,actor+0x318)-100.0f;pgm_animation(actor,0x16,1.0f,2,1.0f,-1);R_PGM(s16,actor+0x5BC)=2;R_PGM(s16,actor+0x5D8)=30;pgm_appear(actor);/* fall through */
 }
 case 2:
  if(R_PGM(s16,actor+0x5D8)==0){pgm_animation(actor,9,2.0f,0,1.0f,-1);R_PGM(f32,actor+0x370)=0.0f;R_PGM(s16,actor+0x5BC)=3;pgm_voice(actor,0x493C);}break;
 case 3:
  if(frame==14)pgm_effect_sound(actor,0x5951,0);R_PGM(s8,actor+0x8B9)=1;R_PGM(u8,actor+0xC70)=8;
  if(frame==pgm_hio_short(0x3A))R_PGM(s8,actor+0x8E0)=(s8)(pgm_hio_short(0x3C)-pgm_hio_short(0x3A));
  pgm_calculate(actor+0x370,(u32)(frame-13)<12?20.0f:0.0f,1.0f,4.0f);R_PGM(s16,actor+0x322)=R_PGM(s16,actor+0x32A);R_PGM(s16,actor+0x320)=0;
  pgm_position_move(actor,1);if(pgm_stopped(actor)){R_PGM(s16,actor+0x5BA)=2;R_PGM(s16,actor+0x5BC)=0;}break;
 }
 pgm_line_finish(line);
}
