from pathlib import Path
import re,subprocess,tempfile,unittest

class TowerServantExecute(unittest.TestCase):
 def test_inactive_room_and_native_update_paths(self):
  header=Path(__file__).with_name('tower_servant_execute.h').read_text()
  # Host pointers are wider; native words remain 32 bits. Only these fields carry host pointers.
  header=header.replace('#define OS_E(t,a,o) (*(volatile t*)((u32)(a)+(o)))','#define OS_E(t,a,o) (*(volatile t*)address((u32)(a)+(o)))')
  header=header.replace('OS_E(u32,','OS_E(uint32_t,')
  for needle in ['actor,0x614','os_exec_info(),0x5B38','os_exec_info(),0x5B34','os_exec_info(),0x5B2C','os_exec_info(),0x5AB0']:
   header=header.replace('OS_E(uint32_t,'+needle+')','OS_E(u32,'+needle+')')
  manual={}
  def fn(name,body):manual[name]=body
  fn('os_exec_info','info_calls++;return (u32)info;')
  fn('os_exec_check_player','assert(actor==(u32)actor_bytes);assert((E32(0x8F4)&16)==0&&(E32(0x2E0)&32)==0);trace(1);if(mutate_room)actor_bytes[0x326]=mutate_room;')
  fn('os_exec_finish','assert(actor==(u32)actor_bytes);trace(2);finish_calls++;if(finish_calls==1)return finished;return finished_later;')
  fn('Os_initialRoomCheckHD','assert(actor==(u32)actor_bytes);trace(3);assert(actor_bytes[0x2FE]==expected_home);return initial_room;')
  fn('Os_groundRoomHD','assert(background==(u32)info+0x12A0&&polygon==(u32)actor_bytes+0x524);trace(4);return ground_room;')
  fn('os_exec_end_beam','assert(actor==(u32)actor_bytes);trace(5);end_calls++;')
  fn('os_exec_restart_number','assert(actor==(u32)actor_bytes);trace(6);return -7;')
  fn('Os_initialRestartHD','assert(actor==(u32)actor_bytes&&restart==-7&&flag==1);trace(7);')
  fn('os_exec_wakeup','assert(actor==(u32)actor_bytes);trace(8);return wakeup;')
  fn('os_exec_stage_layer','assert(actor==(u32)actor_bytes);trace(9);')
  fn('Os_setWallRadiusHD','assert(circle==(u32)actor_bytes+0x690+0x40*wall_calls);assert(radius==(carry?15:40));wall_calls++;trace(10);')
  fn('Os_searchPedestalHD','assert(name==0x12345678&&room==255&&flag==1);trace(11);return pedestal?(u32)pedestal_bytes:0;')
  fn('Os_isCreatingHD','assert(processId==0xAABBCCDD);trace(12);return creating;')
  fn('Os_moveStopCheckHD','assert(actor==(u32)actor_bytes);trace(13);return stopped;')
  fn('os_exec_set_npc','assert(actor==(u32)actor_bytes&&parameter==0);assert(*(u32*)pmf==0xABCDEF0012345678ULL);actor_bytes[0x913]=22;trace(14);')
  fn('os_exec_joint','assert(joint==(u32)actor_bytes+0x87C);assert(backx==106&&backy==110&&minbackx==114&&minbacky==118&&headx==104&&heady==108&&minheadx==112&&minheady==116&&step==120);trace(15);')
  fn('Os_positionMoveCheckHD','assert(actor==(u32)actor_bytes&&name==0x87654321);trace(16);return 0;')
  fn('os_exec_crr','assert(acch==(u32)actor_bytes+0x43C&&background==(u32)info+0x12A0);trace(17);crr_calls++;')
  fn('os_exec_event_proc','assert(actor==(u32)actor_bytes);trace(18);return 1;')
  fn('os_exec_collision','assert(actor==(u32)actor_bytes);trace(19);')
  fn('os_exec_event_order','assert(actor==(u32)actor_bytes);trace(20);')
  fn('os_exec_matrix','assert(actor==(u32)actor_bytes);trace(21);matrix_calls++;')
  source=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uintptr_t u32;typedef uint16_t u16;typedef int16_t s16;typedef uint8_t u8;typedef int8_t s8;typedef int32_t s32;typedef float f32;
static _Alignas(16) u8 globals[0x500000],actor_bytes[0xA00],info[0x6000],pedestal_bytes[0x400];
static void* address(u32 a){if(a>=0x10000000&&a<0x10500000)return globals+(a-0x10000000);return (void*)a;}
#define E32(o) (*(uint32_t*)(actor_bytes+(o)))
#define EF(o) (*(f32*)(actor_bytes+(o)))
static unsigned traces[128],n,info_calls,finish_calls,end_calls,wall_calls,crr_calls,matrix_calls;
static unsigned finished,finished_later,initial_room,stopped,pedestal,creating,wakeup,carry,mutate_room,expected_home;
static s32 ground_room;
static void trace(unsigned value){assert(n<128);traces[n++]=value;}
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_GAME_DATA(a,t) (*(t*)address(a))
void randomizer_servant_event_order(u32 actor);
#include "tower_servant_execute.h"
void randomizer_servant_event_order(u32 actor){(void)actor;assert(0);}
'''
  for match in re.finditer(r'WWHD_GAME_FUNC\([^,]+,([^,]+),([^,]+),\((.*?)\)\);',header):
   ret,name,args=match.groups()
   body=manual.get(name,'assert(!"unexpected '+name+'");'+('' if ret=='void' else 'return 0;'))
   source+=f'{ret} {name}({args}){{{body}}}\n'
  source+=r'''
static void reset(void){
 memset(actor_bytes,0,sizeof(actor_bytes));memset(info,0,sizeof(info));memset(pedestal_bytes,0,sizeof(pedestal_bytes));
 n=info_calls=finish_calls=end_calls=wall_calls=crr_calls=matrix_calls=0;
 finished=finished_later=initial_room=stopped=pedestal=creating=wakeup=carry=mutate_room=0;
 E32(0x8F4)=0x80000010;E32(0x2E0)=0x200020;actor_bytes[0x2FE]=44;actor_bytes[0x2DD]=1;ground_room=-8;
 *(uint32_t*)address(0x101C28F4)=0x12345678;*(uint32_t*)address(0x101C28E8)=0x87654321;
 *(u32*)address(0x1001F260)=0xABCDEF0012345678ULL;
 *(uint32_t*)(pedestal_bytes+4)=0xAABBCCDD;
 *(u32*)(info+0x5B38)=(u32)actor_bytes;
 for(unsigned i=0;i<0x1C8;i++)actor_bytes[0x110+i]=(u8)(i*13+7);
 for(unsigned off=0x34;off<=0x44;off+=2)*(s16*)address(0x10467E44+off)=(s16)(100+off-0x30);
 *(u32*)address(0x10467F00)=1;*(u32*)address(0x10467F04)=1;
}
int main(void){
 /* Every byte value matters: the injected comparison is unsigned, including 0xFF. */
 for(unsigned room=0;room<256;room++)for(unsigned stay=0;stay<256;stay++){
  reset();actor_bytes[0x326]=room;*(u8*)address(0x1047E6C8)=stay;expected_home=room==stay?44:255;
  assert(randomizer_servant_execute((u32)actor_bytes)==1);assert(n==5&&traces[0]==1&&traces[1]==2&&traces[2]==3&&traces[3]==4&&traces[4]==5);
  assert(actor_bytes[0x2FE]==expected_home&&actor_bytes[0x326]==248&&*(u32*)(info+0x5B38)==0&&end_calls==1&&wall_calls==0);
 }
 /* Room comparison follows checkPlayerRoom, not entry; finished servants skip it. */
 reset();actor_bytes[0x326]=3;mutate_room=19;*(u8*)address(0x1047E6C8)=19;expected_home=44;
 assert(randomizer_servant_execute((u32)actor_bytes)==1&&actor_bytes[0x2FE]==44);
 for(unsigned k=0;k<2;k++)for(unsigned p=0;p<2;p++)for(unsigned c=0;c<2;c++){
  reset();finished=finished_later=1;pedestal=p;creating=c;carry=k;expected_home=44;actor_bytes[0x326]=7;
  if(carry){*(s16*)(actor_bytes+0x8E8)=0;*(s16*)(actor_bytes+0x8EA)=-1;*(uint32_t*)(actor_bytes+0x8EC)=0x022AECD4;}
  assert(randomizer_servant_execute((u32)actor_bytes)==1&&actor_bytes[0x2FE]==44&&wall_calls==2&&end_calls==1&&finish_calls==(p&&!c?3u:2u));
  assert(*(u32*)(actor_bytes+0x614)==(p&&!c?(u32)pedestal_bytes:0));
 }
 /* Restart, wakeup flags, move-stop PMF setup, and early-exit order. */
 for(unsigned w=0;w<2;w++){
  reset();initial_room=1;stopped=1;wakeup=w;actor_bytes[0x326]=8;*(u8*)address(0x1047E6C8)=8;expected_home=44;E32(0x3BC)=0xABCDEF40;
  assert(randomizer_servant_execute((u32)actor_bytes)==1&&wall_calls==2&&actor_bytes[0x913]==0&&end_calls==1&&crr_calls==0);
  assert(E32(0x3BC)==(w?0xABCDEF00u:0xABCDEF40u));
 }
 /* Complete carry path retains joint parameters/correction/action/event/matrix and lighting copies. */
 reset();initial_room=1;expected_home=255;actor_bytes[0x326]=8;*(u8*)address(0x1047E6C8)=7;E32(0x2E0)|=0x2000;EF(0x4D0)=-1000000000.0f;
 assert(randomizer_servant_execute((u32)actor_bytes)==1&&crr_calls==1&&matrix_calls==1&&end_calls==0);
 assert((E32(0x8F4)&0x10)!=0&&actor_bytes[0x2FE]==255&&finish_calls==2);
 assert(!memcmp(address(0x10475460),actor_bytes+0x110,0x44));
 assert(!memcmp(address(0x10475460+0x84),actor_bytes+0x194,0x39));
 /* Both cached light records are assigned; only the inter-record padding stays untouched. */
 assert(!memcmp(address(0x10475460+0xC0),actor_bytes+0x1D0,0x44));
 assert(!memcmp(address(0x10475460+0x144),actor_bytes+0x254,0x44));
 assert(*(u8*)address(0x10475460+0x44)==0&&*(u8*)address(0x10475460+0x104)==0&&*(u8*)address(0x10475460+0x188)==0);
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'tower_servant_execute.h').write_text(header);(root/'test.c').write_text(source)
   subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter',str(root/'test.c'),'-o',str(root/'test')],check=True)
   subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
