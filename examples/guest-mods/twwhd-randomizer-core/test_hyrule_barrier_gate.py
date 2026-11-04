"""Barrier cutscene gate keeps full native geometry, callbacks and signed shard check."""
from pathlib import Path
import subprocess,tempfile,unittest
class HyruleBarrierGate(unittest.TestCase):
 def test_full_consumer(self):
  code=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef uint16_t u16;typedef int16_t s16;typedef int32_t s32;typedef float f32;typedef double f64;
static unsigned char arena[0x20000];static const u32 actor=0x30000000,save0=0x30002000,save1=0x30004000,game=0x30008000,player=0x30010000,other=0x30011000;static u32 saveptr;
static void* mem(u32 a){assert(a>=actor&&a<actor+sizeof(arena));return arena+a-actor;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_BARRIER(t,a) W(t,a)
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_GAME_DATA(a,t) ((a)==0x10026228?8800.0f:0)
static u32 randomizer_daily_save(void){return saveptr;}
static u32 wwhd_dComIfG_getGameInfo(void);
static void randomizer_hyrule_subtract(u32,f32*,u32);
static f64 randomizer_hyrule_square_magnitude(const f32*);
static f64 randomizer_hyrule_sqrt(f64);
static s32 wwhd_dSv_player_collect_c_getTriforceNum(u32);
#include "hyrule_barrier_gate.h"
static s32 event_result,shards;static f64 distance;static unsigned calls,geometry,queries,setters,indices,mutate,game_calls;
static u32 wwhd_dComIfG_getGameInfo(void){game_calls++;if(mutate==3&&game_calls==2)W(u32,game+0x5B2C)=other;return game;}
s32 randomizer_barrier_event(u32 e,u32 flag){assert(e==save0+0x644&&flag==0x3980&&calls++==0);saveptr=save1;return event_result;}
static void randomizer_hyrule_subtract(u32 p,f32* output,u32 a){assert(p==player+0x314&&a==actor+0x314&&calls++==1);output[0]=3;output[1]=999;output[2]=4;geometry++;}
static f64 randomizer_hyrule_square_magnitude(const f32* p){assert(p[0]==3&&p[1]==0&&p[2]==4&&calls++==2);return 25.0000000001;}
static f64 randomizer_hyrule_sqrt(f64 value){assert(value==25.0000000001&&calls++==3);if(mutate==1)W(u8,save1+0x2E)=0;return distance;}
static s32 wwhd_dSv_player_collect_c_getTriforceNum(u32 collect){assert(collect==save1+0xD4&&calls++==4);queries++;if(mutate==2)W(u8,player+0x3AC)=10;return shards;}
void randomizer_barrier_event_on(u32 e,u32 flag){assert(e==save1+0x644&&flag==0x2C02&&calls++==5);setters++;saveptr=save0;W(s32,actor+0xDFC)=999;}
s32 randomizer_barrier_event_index(u32 mgr,u32 name,u32 map){assert(mgr==game+0x52C4&&name==0x1002622C&&map==255&&calls++==6);indices++;W(s16,actor+0xDF8)=123;return 0x12348001;}
static void reset(void){memset(arena,0,sizeof arena);saveptr=save0;calls=geometry=queries=setters=indices=game_calls=0;W(u32,game+0x5B2C)=player;W(u16,game+0x5BAC)=1;W(u8,save1+0x2E)=0x3E;W(u8,player+0x3AC)=5;W(s32,actor+0xDFC)=777;}
int main(void){unsigned cases=0;event_result=1;distance=8800;mutate=0;
for(unsigned sword=0;sword<256;sword++)for(unsigned cut=0;cut<256;cut++)for(unsigned s=0;s<5;s++)for(unsigned active=0;active<2;active++){
reset();const s32 counts[]={-1,0,7,8,0x7FFFFFFF};shards=counts[s];W(u8,save1+0x2E)=sword;W(u8,player+0x3AC)=cut;W(u16,game+0x5BAC)=active;randomizer_hyrule_barrier_wait(actor);unsigned allow=sword==0x3E&&shards>=8&&active&&cut>=1&&cut<=10;assert(setters==allow&&indices==allow);assert(queries==(sword==0x3E));assert(W(s32,actor+0xDFC)==(allow?1:777));if(allow)assert(W(s16,actor+0xDF8)==(s16)0x8001);cases++;}
const f64 distances[]={8799.999999999998,8800,8800.000000000002,INFINITY,-INFINITY,NAN};
for(unsigned d=0;d<6;d++)for(unsigned e=0;e<4;e++)for(mutate=0;mutate<4;mutate++){
 reset();distance=distances[d];event_result=e==0?0:e==1?1:e==2?-1:2;shards=8;randomizer_hyrule_barrier_wait(actor);unsigned allow=e==1&&!(distance<8800)&&mutate!=1;assert(setters==allow);assert(geometry==(e==1));if(e!=1)assert(!queries&&!game_calls);if(mutate==3&&allow)assert(W(u32,game+0x5B2C)==other);cases++;}
printf("Hyrule barrier gate: %u cases passed\n",cases);}
'''
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'test.c').write_text(code)
   subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(root/'test.c'),'-o',str(root/'test')],check=True)
   subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
