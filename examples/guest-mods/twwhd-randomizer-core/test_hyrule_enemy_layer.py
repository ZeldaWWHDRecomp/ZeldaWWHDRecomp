"""Full layer-selection matrix, genuine query order and mutable SafeString contract."""
from pathlib import Path
import os, subprocess, tempfile, unittest
class Layer(unittest.TestCase):
 def test_consumer(self):
  code=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u8 play[0x5200],other[0x5200],save[0x800],save2[0x800];static u32 saveptr=(u32)save;
#define WWHD_GAME_DATA(a,t) saveptr
static const unsigned addresses[]={0x1004BC68,0x1004BC74,0x1004BC6C,0x1004BC7C,0x1004BC84,0x1004BC58,0x1004BC60,0x1004BC8C,0x1004BC94};
static const char* names[]={"sea","A_mori","Asoko","Hyrule","Hyroom","kenroom","M2tower","GanonK","GTower","other"};
static u32 literal(u32 a){for(unsigned i=0;i<9;i++)if(a==addresses[i])return(u32)names[i];assert(0);return 0;}
#define RHL_LITERAL(a) literal(a)
static void terminate(u32);static void terminate2(u32);
static u32 pointer(u32 a){assert(a==0x1004B8D4||a==0x400014);return(u32)(a==0x1004B8D4?terminate:terminate2);}
#define RHL_PTR(a,o) pointer((a)+(o))
static u32 wwhd_dComIfG_getGameInfo(void);
#include "hyrule_enemy_layer.h"
static unsigned flags,events[32],ne,expected[32],nx,infos,terms,hour_calls,night_calls,shards_calls;static s32 hour;static u32 night,shards;static unsigned mutate_info,mutation;static rhl_string* first;
static const unsigned bits[]={0x520,0xE20,0x101,0x2D01,0x1820,0xF80,0x3280,0x2C01,0x3B40,0x3802,0x3B02,0x4002};
static u32 flag(unsigned f){for(unsigned i=0;i<12;i++)if(f==bits[i])return(flags>>i)&1;assert(0);return 0;}
static u32 wwhd_dComIfG_getGameInfo(void){++infos;if(mutate_info&&infos==2)return(u32)other;return(u32)play;}
s32 rhl_hour(void){++hour_calls;return hour;}
u32 rhl_night(void){++night_calls;return night;}
u32 rhl_event_native(u32 s,u32 f){assert(s==saveptr+0x644);assert(ne<32);events[ne++]=f;return flag(f);}
u32 rhl_triforce_native(u32 s){assert(s==saveptr+0xD4);++shards_calls;return shards;}
static void terminate2(u32 p){rhl_string*s=(rhl_string*)p;assert(s==first);++terms;s->text=(u32)"Hyrule";}
static void terminate(u32 p){rhl_string*s=(rhl_string*)p;++terms;if(mutation==1){if(terms==1){first=s;s->vtable=0x400000;}else if(terms==3){assert(s!=first);first->text=(u32)"wrong";s->text=(u32)"Hyrule";}}if(mutation==2&&terms==3)saveptr=(u32)save2;}
static unsigned query(unsigned f){expected[nx++]=f;return flag(f);}
static s32 oracle(unsigned stage,s32 room){s32 l=night?1:((unsigned)hour-6<12?0:1);switch(stage){
case 0:if(room==44){if(query(0x520))return l|4;if(query(0xE20))return l|2;if(query(0x101))return 9;}else if(room==11){if(query(0x2D01))return l|4;if(night)return l|2;}else if(room==1)return query(0x1820)?3:1;return l;
case 1:return query(0xF80)?l|2:l;
case 2:return query(0x520)?l|2:l;
case 3:return l;
case 4:if(shards==8&&!query(0x2C01))return l|4;if(query(0x3280))return l|2;return query(0x3B40)?l|6:l;
case 5:if(query(0x2C01))return l|6;if(query(0x3802)&&!query(0x3280))return l|6;if(shards==8)return l|4;return query(0x3802)?l|2:l;
case 6:return query(0x2D01)?l|2:l;
case 7:return query(0x3B02)?l:8;
case 8:return query(0x4002)?l:8;
default:return l;}}
static void reset(unsigned stage){memset(play,0,sizeof play);strcpy((char*)play+0x5134,names[stage]);play[0x513F]=255;ne=nx=infos=terms=hour_calls=night_calls=shards_calls=0;mutate_info=mutation=0;saveptr=(u32)save;}
int main(void){unsigned count=0;const s32 hours[]={5,6,17,18,INT32_MIN,INT32_MAX};const s32 rooms[]={44,11,1,0,-1,INT32_MIN,INT32_MAX};
for(flags=0;flags<4096;flags++)for(unsigned st=0;st<10;st++)for(unsigned h=0;h<6;h++)for(night=0;night<2;night++)for(unsigned sh=0;sh<2;sh++){
 reset(st);hour=hours[h];shards=sh?8:0;s32 room=rooms[(flags+h)%7];s32 wanted=oracle(st,room);assert(randomizer_hyrule_enemy_layer(room)==wanted);assert(ne==nx&&!memcmp(events,expected,ne*sizeof(*events)));assert(hour_calls==1&&night_calls==(st==0&&room==11&&!flag(0x2D01)?2:1));assert(terms==3*(st<9?st+1:9));if(st==3)assert(!ne&&!shards_calls);++count;
}
for(unsigned i=0;i<128;i++){reset(3);play[0x513F]=i;other[0x513F]=127-i;mutate_info=1;assert(randomizer_hyrule_enemy_layer(0)==(s8)(127-i)&&infos==2&&!terms&&!hour_calls);++count;}
reset(3);mutation=1;assert(rhl_stage_is((u32)"wrong"));assert(terms==3);++count; /* captured literal text survives third callback, second vtable reload observed. */
reset(2);mutation=2;flags=1;hour=12;night=0;assert(randomizer_hyrule_enemy_layer(0)==2&&saveptr==(u32)save2);++count;
static char a[0x40002],b[0x40002];memset(a,'x',sizeof a);memset(b,'x',sizeof b);assert(!rhl_equal((u32)a,(u32)b));a[0x40000]=b[0x40000]=0;assert(rhl_equal((u32)a,(u32)b));a[0x40000]='x';assert(!rhl_equal((u32)a,(u32)b));assert(rhl_equal((u32)a,(u32)a));++count;
printf("layer: %u independent matrix/callback/bound cases passed\n",count);return 0;}
'''
  with tempfile.TemporaryDirectory() as d:
   src=Path(d)/'test.c';src.write_text(code);exe=Path(d)/'test'
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(src),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
if __name__=='__main__':unittest.main()
