"""Pending pig text blocks counting without changing normal capture/flag behavior."""
from pathlib import Path
import os,subprocess,tempfile,unittest
class Dampa(unittest.TestCase):
 def test_consumer(self):
  code=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u8 actor[0x1000],pigs[3][0x1000],save1[0x1300],save2[0x1300];static u32 saveptr=(u32)save1;static unsigned int parameters[]={0x100,0x200,0x300};
static void* data(u32 a){if(a==1)return&saveptr;assert(a>=0x1002014C&&a<0x10020158);return parameters+(a-0x1002014C)/4;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#include "dampa_pending_pig.h"
static unsigned tmp,searches,orders,distance_calls,writes,getters,chosen,mask,mutation;static u8 reg;static f32 distances[3];static unsigned snapshot_x,snapshot_y,snapshot_z;
s32 rdp_event(u32 event,u32 flag){assert(event==(u32)save1+0x1178&&flag==0x280);if(mutation==1)RDP32((u32)actor,0x8CC)|=0x80;return tmp;}
u32 rdp_search(u32 name,u32 m,u32 parameter){assert(name==0x100207E0&&m==0xF00&&searches<3&&parameter==parameters[searches]);unsigned i=searches++;chosen=i;return mask&(1u<<i)?(u32)pigs[i]:0;}
void rdp_distance(u32 a,u32 b,u32 distance,u32 angle){assert(!angle&&RDP32(a,0)==snapshot_x&&RDP32(a,4)==snapshot_y&&RDP32(a,8)==snapshot_z&&RDP32(b,0)==RDP32((u32)pigs[chosen],0x314));++distance_calls;*(f32*)distance=distances[chosen];if(mutation==2){RDPF((u32)actor,0x318)=9;RDP32((u32)actor,0x314)=0x7FC45678;RDP32((u32)actor,0x31C)=0x80000000;saveptr=(u32)save2;}}
void rdp_register_set(u32 event,u32 r,u8 value){assert(event==saveptr+0x1178&&RDP8((u32)pigs[chosen],0x750)==1&&(RDP32((u32)actor,0x8CC)&0x80)&&RDP8((u32)actor,0x901)==2);if(writes++==0){assert(r==0xFC03&&value==chosen);if(mutation==3)saveptr=(u32)save2;}else{assert(r==0xFD07&&value==(u8)(reg|(1u<<chosen)));}}
u8 rdp_register_get(u32 event,u32 r){assert(event==saveptr+0x1178&&r==0xFD07&&writes==1);++getters;if(mutation==4)saveptr=(u32)save2;return reg;}
s32 rdp_order_speak(u32 self){assert(self==(u32)actor&&!searches&&!writes);++orders;RDP32(self,0x8CC)^=0x12340000;return -7;}
static void reset(void){memset(actor,0,sizeof actor);memset(pigs,0,sizeof pigs);saveptr=(u32)save1;searches=orders=distance_calls=writes=getters=chosen=mutation=0;mask=7;tmp=1;reg=0;RDP8((u32)actor,0x8FD)=1;RDP32((u32)actor,0x314)=snapshot_x=0x80000000;RDP32((u32)actor,0x318)=snapshot_y=0;RDP32((u32)actor,0x31C)=snapshot_z=0x3F800000;for(unsigned i=0;i<3;i++)distances[i]=1;}
int main(void){unsigned cases=0;
for(unsigned active=0;active<2;active++)for(unsigned pending=0;pending<2;pending++)for(unsigned a=0;a<256;a++)for(unsigned b=0;b<256;b++){
 reset();tmp=active;RDP32((u32)actor,0x8CC)=pending?0x80000080:0x80000000;RDP8((u32)actor,0x8FD)=a;RDP8((u32)actor,0x8FE)=b;
 unsigned eligible=active&&!pending&&(a||b);assert(randomizer_dampa_check_pig((u32)actor)==(s32)eligible);assert(orders==(active&&pending)&&searches==eligible&&writes==2*eligible);++cases;
}
const f32 ranges[]={0,399.99f,400,INFINITY,NAN};const f32 heights[]={0,10,-10,10.01f,-10.01f,NAN,INFINITY};
for(mask=0;mask<8;mask++)for(unsigned counted=0;counted<8;counted++)for(unsigned d=0;d<5;d++)for(unsigned h=0;h<7;h++){
 unsigned selected_mask=mask;reset();mask=selected_mask;for(unsigned i=0;i<3;i++){RDP8((u32)pigs[i],0x750)=(counted>>i)&1;RDPF((u32)pigs[i],0x318)=heights[h];distances[i]=ranges[d];}
 unsigned allowed=mask&~counted;unsigned success=allowed&&d<2&&h<3; if(h==5)success=allowed&&d<2;
 assert(randomizer_dampa_check_pig((u32)actor)==(s32)success);if(success){unsigned i=0;while(!(allowed&(1u<<i)))++i;assert(searches==i+1&&writes==2&&getters==1&&chosen==i);assert(RDP32((u32)pigs[i],0x798)==snapshot_x&&RDP32((u32)pigs[i],0x79C)==snapshot_y&&RDP32((u32)pigs[i],0x7A0)==snapshot_z);}++cases;
}
for(unsigned m=1;m<5;m++){reset();mutation=m;reg=0xA0;assert(randomizer_dampa_check_pig((u32)actor)==(m!=1));if(m==1)assert(orders==1&&!searches);if(m==2){assert(RDP32((u32)pigs[0],0x798)==0x7FC45678&&RDP32((u32)pigs[0],0x7A0)==0x80000000&&RDPF((u32)pigs[0],0x79C)==9);}++cases;}
printf("Dampa pending pig: %u cases passed\n",cases);return 0;}
'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
