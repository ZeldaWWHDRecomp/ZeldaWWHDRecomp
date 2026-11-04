"""Check deferred actor readiness, one-shot momentum and reload placement gates."""
from pathlib import Path
import os, subprocess, tempfile, unittest
class TreeReward(unittest.TestCase):
 def test_lifecycle(self):
  source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;
typedef int16_t s16;typedef uint16_t u16;typedef float f32;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_HOOK(a,n,p) void n p
static _Alignas(16) u8 tree[0x900],item_actor[0x500],events[256];
static u32 init_reward_tree=0x3C,phase=4,exists=1,ready=0,pid=123,spawns,searches,reg=3,have=1,collected;
static f32 pos[3];static s16 angle[3];static u32 motion;
static s32 wwhd_fopAcM_SearchByID(u32 id,u32 out){assert(id==pid);++searches;*(u32*)out=ready?(u32)item_actor:0;return exists;}
static s32 wwhd_save_event_set(u32 bit,u32 value){assert(bit==0x2E20);collected=value;return 0;}
static s32 wwhd_save_event_test(u32 bit){return bit==0x0102?have:collected;}
static void* wwhd_save_flags_detail_event_object(void){return events;}
static void wwhd_dSv_event_c_setEventReg(u32 object,u32 bit,u32 value){assert(object==(u32)events && bit==0x9B07);reg=value;}
static u8 wwhd_dSv_event_c_getEventReg(u32 object,u32 bit){assert(object==(u32)events && bit==0x9B07);return reg;}
static u32 wwhd_fopAcM_createItem(u32 p,u32 id,u32 bit,u32 room,u32 type,u32 angles,u32 action,u32 scale){
 assert(id==init_reward_tree && bit==0xFFFFFFFF && (s32)room==-2 && type==3 && action==5);
 const f32* s=(const f32*)scale;assert(s[0]==1 && s[1]==1 && s[2]==1);
 memcpy(pos,(void*)p,sizeof(pos));memcpy(angle,(void*)angles,sizeof(angle));++spawns;return pid;
}
static void wwhd_mDoMtx_YrotM(u32 m,s32 a){(void)m;assert(a==9100);++motion;}
#include "reward_tree.h"
u32 randomizer_native_tree_create(u32 actor){assert(actor==(u32)tree);return phase;}
void randomizer_tree_translate(u32 matrix,f32 x,f32 y,f32 z){f32* m=(f32*)matrix;m[0]=x;m[1]=y;m[2]=z;}
void randomizer_tree_transform(u32 matrix,u32 source,u32 dest){const f32* m=(const f32*)matrix;const f32* s=(const f32*)source;f32* d=(f32*)dest;for(unsigned n=0;n<3;n++)d[n]=m[n]+s[n];}
int main(void){
 *(u32*)(tree+0x3A4)=99;tree[0x32E]=7;phase=1;
 assert(randomizer_tree_create((u32)tree)==1 && *(u32*)(tree+0x3A4)==99 && tree[0x32E]==7);
 phase=4;assert(randomizer_tree_create((u32)tree)==4 && *(u32*)(tree+0x3A4)==0xFFFFFFFF && !tree[0x32E]);
 randomizer_tree_pending((u32)tree);assert(!searches);
 *(u32*)(tree+4)=77;*(u32*)(tree+0x7BC)=3;tree[0x326]=(u8)-2;
 *(f32*)(tree+0x314)=10;*(f32*)(tree+0x318)=20;*(f32*)(tree+0x31C)=30;*(s16*)(tree+0x32A)=100;
 assert(randomizer_tree_launch((u32)tree)==1 && spawns==1 && pos[1]==880 && angle[1]==9100);
 assert(*(s16*)(tree+0x7C0)==1 && *(u32*)(tree+0x3A4)==pid && !tree[0x32E]);
 randomizer_tree_pending((u32)tree);assert(!tree[0x32E] && !collected);
 ready=1;*(u32*)(item_actor+0x2E0)=0x100;
 randomizer_tree_pending((u32)tree);assert(tree[0x32E]==1 && *(u32*)(item_actor+0x2E8)==77);
 assert(*(f32*)(item_actor+0x370)==1.75f && *(f32*)(item_actor+0x340)==30 && *(f32*)(item_actor+0x374)==-2.1f && *(u32*)(item_actor+0x2E0)==0x140);
 *(f32*)(item_actor+0x340)=2;randomizer_tree_pending((u32)tree);assert(*(f32*)(item_actor+0x340)==2);
 exists=0;randomizer_tree_pending((u32)tree);assert(collected==1 && *(u32*)(tree+0x3A4)==0xFFFFFFFF);
 collected=0;exists=1;*(u32*)(tree+0x7BC)=3;*(s16*)(tree+0x4CE)=-1;
 assert(randomizer_tree_place((u32)tree)==1 && tree[0x32E]==1 && motion==1 && pos[0]==10 && pos[1]==21 && pos[2]==340);
 randomizer_tree_pending((u32)tree);assert(*(f32*)(item_actor+0x340)==2);
 for(unsigned gate=0;gate<4;gate++){
  *(s16*)(tree+0x4CE)=gate==0?0:-1;have=gate!=1;collected=gate==2;reg=gate==3?4:3;
  const u32 before=spawns;assert(!randomizer_tree_place((u32)tree) && spawns==before);
 }
 have=1;collected=0;reg=3;pid=0xFFFFFFFF;assert(!randomizer_tree_launch((u32)tree));
 const u32 before=spawns;init_reward_tree=0xFF;assert(!randomizer_tree_launch((u32)tree) && spawns==before);
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'test.c').write_text(source)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
