"""Sage reentry tables, complete Create phases and callback capture contracts."""
from pathlib import Path
import os, subprocess, tempfile, unittest
class Sages(unittest.TestCase):
 def test_consumers(self):
  code=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef int16_t s16;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u8 actor[0x4400],save1[0x12C0],save2[0x12C0],play[0x6000],morf[0x100],model[0x200],music[2];static u32 save_ptr=(u32)save1,partner,arc,shiparc;static unsigned int heaps[2]={0x7660,0x61C0};
static void* data(u32 a){if(a==1)return&save_ptr;if(a==0x101CEF74)return&partner;if(a==0x101C0278)return&arc;if(a==0x101C027C)return&shiparc;if(a==0x101C07FC)return heaps;if(a==0x101BDC2A)return music;assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static struct{u32 key,value;} cells[256];static unsigned n_cells;
static u32* cell(u32 key){for(unsigned i=0;i<n_cells;i++)if(cells[i].key==key)return&cells[i].value;assert(n_cells<256);cells[n_cells].key=key;cells[n_cells].value=0;return&cells[n_cells++].value;}
#define RSP(a,o) (*cell((a)+(o)))
static u32 wwhd_dComIfG_getGameInfo(void);
#include "sage_dungeon_reentry.h"
static unsigned restart_calls,priest_calls,check_calls,heap_calls,item_calls,collect_calls,stage_calls,ctor_calls,layer_calls,matrix_calls,init_calls;
static u32 observed_restart,observed_position,observed_priest;static s16 observed_angle;static s8 observed_room;static s32 observed_mode;
static unsigned info_mutation,restart_mutation;static u32 phase_value,heap_ok,init_ok,item_owned,collect_owned,letter_value,boss_value;static unsigned flags;static unsigned trace[64],trace_n;
static void mark(unsigned x){assert(trace_n<64);trace[trace_n++]=x;}
static u32 wwhd_dComIfG_getGameInfo(void){++stage_calls;if(info_mutation){save_ptr=(u32)save2;RSS16((u32)save1,0x1D8)=1234;RSS8((u32)save1,0x1DA)=42;}return(u32)play;}
void rs_construct_npc(u32 actor){++ctor_calls;RS32(actor,0x2E4)=0xA0;}
void rs_frame_ctor(u32 frame,s32 end){(void)frame;(void)end;}
void rs_matrix_ctor(u32 matrix){(void)matrix;}
void rs_array_ctor(u32 array,u32 count,u32 size,u32 constructor){(void)array;(void)count;(void)size;(void)constructor;}
void rs_status_ctor(u32 status){(void)status;}
void rs_gstatus_ctor(u32 status){(void)status;}
void rs_object_ctor(u32 object){(void)object;}
void rs_cylinder_ctor(u32 cylinder){(void)cylinder;}
void rs_joint_ctor(u32 joint){(void)joint;}
void rs_btp_ctor(u32 animation){(void)animation;}
void rs_btk_ctor(u32 animation){(void)animation;}
void rs_line_ctor(u32 check){(void)check;}
void rs_capsule_ctor(u32 capsule){(void)capsule;}
void rs_mirror_ctor(u32 mirror){(void)mirror;}
void rs_ripple_ctor(u32 callback){(void)callback;}
u32 rs_item(u32 item){(void)item;assert(!randomizer_spawn_query.fish);++item_calls;return item_owned;}
u32 rs_collect(u32 save,u32 field,u32 bit){assert(save==save_ptr+0xD4&&field==0&&bit==2);assert(!randomizer_spawn_query.fish);++collect_calls;return collect_owned;}
s32 rs_event_native(u32 save,u32 bit){assert(save==save_ptr+0x644);return bit==0x2910?(flags&1)!=0:bit==0x2E04?(flags&2)!=0:bit==0x2920?(flags&4)!=0:0;}
s32 rs_stage_boss(u32 stage){assert(stage==3);return boss_value;}
u32 rs_load(u32 phase,u32 name){assert(phase==(u32)actor+0x608&&name);return phase_value;}
s32 rs_heap(u32 actor,u32 callback,u32 size){assert(actor&&callback&&size);++heap_calls;mark(4);RSP(actor,0x610)=RSP(actor,0x618)=(u32)morf;RSP((u32)morf,0x90)=(u32)model;return heap_ok;}
void rs_restart(u32 restart,s32 mode,u32 position,s16 angle,s8 room){++restart_calls;mark(1);observed_restart=restart;observed_mode=mode;observed_position=position;observed_angle=angle;observed_room=room;if(restart_mutation){RS32(position,0)=0xDEADBEEF;RS16(position,0xC)=900;RS8(position,0xE)=77;}}
void rs_priest(u32 priest,s32 mode,u32 position,s16 angle,s8 room){++priest_calls;mark(2);assert(mode==observed_mode&&position==observed_position&&angle==observed_angle&&room==observed_room);observed_priest=priest;memmove((void*)priest,(void*)position,12);RS16(priest,0xC)=(u16)angle;RS8(priest,0xE)=(u8)room;RS8(priest,0xF)=(u8)mode;}
void rs_check_restart(u32 actor,s32 mode){assert(actor&&mode);++check_calls;mark(3);}
void rs_layer(u32 actor){(void)actor;++layer_calls;mark(7);}
void rs_makar_matrix(u32 actor){(void)actor;++matrix_calls;mark(5);}
s32 rs_makar_init(u32 actor){(void)actor;++init_calls;mark(6);return init_ok;}
void rs_medli_matrix(u32 actor){(void)actor;++matrix_calls;mark(5);}
s32 rs_medli_init(u32 actor){(void)actor;++init_calls;mark(6);return init_ok;}
void rs_copy_string(u32 dest,u32 source){memcpy((void*)dest,(void*)source,strlen((char*)source)+1);}
s32 rs_letter(void){return letter_value;}
static const char* literal(u32 p){switch(p){case 0x10018F54:case 0x1001DC74:return"sea";case 0x1001DC54:return"Atorizk";case 0x1001DC5C:return"Adanmae";case 0x1001DC64:return"M_Dra09";case 0x1001DC6C:return"Edaichi";case 0x1001DC78:return"M_Dai";case 0x1001DC80:return"M_DaiB";default:return 0;}}
static void assure(rs_string* s){const char* value=literal(s->text);if(value)s->text=(u32)value;}
static void reset(void){memset(actor,0,sizeof actor);memset(save1,0,sizeof save1);memset(save2,0,sizeof save2);memset(play,0,sizeof play);memset(morf,0,sizeof morf);memset(model,0,sizeof model);memset(music,0,sizeof music);n_cells=0;save_ptr=(u32)save1;partner=0;arc=(u32)"Md";shiparc=(u32)"Md_ship";restart_calls=priest_calls=check_calls=heap_calls=item_calls=collect_calls=stage_calls=ctor_calls=layer_calls=matrix_calls=init_calls=trace_n=0;info_mutation=restart_mutation=0;phase_value=4;heap_ok=init_ok=1;item_owned=collect_owned=1;letter_value=boss_value=0;flags=0;RSP(0x10018CBC,0x14)=(u32)assure;RSP(0x1001D6A0,0x14)=(u32)assure;RS32((u32)actor,0x2E4)=8;RSS16((u32)play,0x513C)=-1;RSS16((u32)save1,0x1D8)=-12345;RSS8((u32)save1,0x1DA)=-91;for(unsigned i=0;i<3;i++)RS32((u32)save1,0x1CC+4*i)=0x7FC01234u+i;}
int main(void){unsigned cases=0;const u8 spawn_ids[2][7]={{15,22,23,24,69,20,70},{0,22,23,24,69,9,17}};
 /* Every signed start-spawn value, not just the seven successful rows. */
 for(unsigned who=0;who<2;who++)for(unsigned spawn=0;spawn<65536;spawn++){
  reset();RSS16((u32)play,0x513C)=(s16)(u16)spawn;RS8((u32)save1,0x1DB)=255;const unsigned int (*rows)[4]=who?randomizer_sage_medli_spawns:randomizer_sage_makar_spawns;
  int match=-1;for(unsigned i=0;i<7;i++)if((s32)(s16)(u16)spawn==(s32)spawn_ids[who][i])match=(int)i;
  rs_reset_priest((u32)save1,(s32)who+1,rows);assert(restart_calls==1&&priest_calls==1&&stage_calls==1);assert(observed_restart==(u32)save1+0x1148&&observed_position==(u32)save1+0x1CC&&observed_priest==(u32)save1+0x1CC&&observed_mode==(s32)who+1);
  if(match>=0){assert(observed_angle==(s16)(u16)rows[match][0]&&observed_room==(s8)(u8)(rows[match][0]>>16));for(unsigned j=0;j<3;j++)assert(RS32((u32)save1,0x1CC+4*j)==rows[match][1+j]);}
  else{assert(observed_angle==-12345&&observed_room==-91);for(unsigned j=0;j<3;j++)assert(RS32((u32)save1,0x1CC+4*j)==0x7FC01234u+j);}
  assert(RS8((u32)save1,0x1DB)==who+1);++cases;
 }
 /* Removed flag guards: every saved-priest flag still executes both setters. */
 for(unsigned who=0;who<2;who++)for(unsigned row=0;row<7;row++)for(unsigned flag=0;flag<256;flag++){
  reset();RS8((u32)save1,0x1DB)=flag;RSS16((u32)play,0x513C)=spawn_ids[who][row];rs_reset_priest((u32)save1,(s32)who+1,who?randomizer_sage_medli_spawns:randomizer_sage_makar_spawns);assert(restart_calls==1&&priest_calls==1&&RS8((u32)save1,0x1DB)==who+1);++cases;
 }
 assert(cases==134656);
 /* GameInfo can replace the singleton and mutate the captured record; a miss
  * preserves cached signed angle/room. Restart can mutate live position data. */
 reset();info_mutation=restart_mutation=1;rs_reset_priest((u32)save1,1,randomizer_sage_makar_spawns);assert(observed_restart==(u32)save1+0x1148&&observed_position==(u32)save1+0x1CC&&observed_priest==(u32)save2+0x1CC);assert(observed_angle==-12345&&observed_room==-91&&RS32((u32)save2,0x1CC)==0xDEADBEEFu);
 reset();RSS16((u32)play,0x513C)=15;info_mutation=restart_mutation=1;rs_reset_priest((u32)save1,1,randomizer_sage_makar_spawns);assert(observed_angle==(s16)0x94A0&&observed_room==15&&observed_priest==(u32)save2+0x1CC&&RS32((u32)save2,0x1CC)==0xDEADBEEFu);
 /* Complete Create phase boundaries and live availability checks. */
 reset();randomizer_spawn_query.fish=1;RS32((u32)actor,0xB0)=4;flags=1;phase_value=1;assert(randomizer_makar_create((u32)actor)==1);assert(randomizer_spawn_query.fish==1);randomizer_spawn_query.fish=0;assert(item_calls==1&&!restart_calls&&!heap_calls);
 reset();RS32((u32)actor,0xB0)=4;flags=1;RSS16((u32)play,0x513C)=22;RSP((u32)play,0x5B38)=0xABCD;assert(randomizer_makar_create((u32)actor)==5);assert(restart_calls==1&&priest_calls==1&&check_calls==1&&!heap_calls);
 reset();RS32((u32)actor,0xB0)=4;flags=1;RSS16((u32)play,0x513C)=15;assert(randomizer_makar_create((u32)actor)==4);assert(partner==(u32)actor&&RSP((u32)actor,0x348)==(u32)model+0xC8&&layer_calls==1&&matrix_calls==1&&init_calls==1);assert(trace[0]==1&&trace[1]==2&&trace[2]==3&&trace[3]==4&&trace[4]==5&&trace[5]==6&&trace[6]==7);
 reset();randomizer_spawn_query.fish=1;memcpy(play+0x5134,"M_Dai",6);flags=2|4;RSS16((u32)play,0x513C)=0;phase_value=2;assert(randomizer_medli_create((u32)actor)==2);assert(randomizer_spawn_query.fish==1);randomizer_spawn_query.fish=0;assert(collect_calls==1&&RS8((u32)actor,0x4254)==5&&RS8((u32)actor,0x4259)==1&&!restart_calls);
 reset();memcpy(play+0x5134,"M_Dai",6);flags=2|4;heap_ok=0;RSP((u32)actor,0x618)=RSP((u32)actor,0x61C)=RSP((u32)actor,0x620)=0x1234;assert(randomizer_medli_create((u32)actor)==5);assert(!restart_calls&&!RSP((u32)actor,0x618)&&!RSP((u32)actor,0x61C)&&!RSP((u32)actor,0x620));
 reset();memcpy(play+0x5134,"M_Dai",6);flags=2|4;RSS16((u32)play,0x513C)=0;assert(randomizer_medli_create((u32)actor)==4);assert(restart_calls==1&&priest_calls==1&&check_calls==1&&partner==(u32)actor);assert(trace[0]==4&&trace[1]==1&&trace[2]==2&&trace[3]==3&&trace[4]==6&&trace[5]==5&&trace[6]==7);
 /* Constructor subobjects retain public absolute offsets/vtable identities. */
 reset();RS32((u32)actor,0x2E4)=0;phase_value=1;assert(randomizer_makar_create((u32)actor)==1);assert(ctor_calls&&RS32((u32)actor,0x2E4)==0xA8&&RSP((u32)actor,0xB4)==0x100191F0&&RS16((u32)actor,0xB30)==0xFFFF&&RS8((u32)actor,0xAE3)==255&&RSP((u32)actor,0x928)==0x1004B150);
 reset();RS32((u32)actor,0x2E4)=0;phase_value=1;assert(randomizer_medli_create((u32)actor)==1);assert(ctor_calls&&RS32((u32)actor,0x2E4)==0xA8&&RSP((u32)actor,0xB4)==0x1001E088&&RS16((u32)actor,0x43CC)==0xFFFF&&RS8((u32)actor,0x803)==1&&RSP((u32)actor,0xD0C)==0x1004AF60);
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
