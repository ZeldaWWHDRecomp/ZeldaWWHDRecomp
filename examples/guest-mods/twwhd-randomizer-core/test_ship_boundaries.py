"""Sea story paths are skipped; ordinary polygon geometry and side effects remain."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class ShipBoundaries(unittest.TestCase):
    def test_sea_and_nonsea_polygons(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint16_t u16;typedef uint8_t u8;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) static r n p
#define WWHD_REPLACE(a,r,n,p) static r n p
#define WWHD_ADDR_save_info_pointer 1
static _Alignas(16) u8 info[0x6000],stag[32],table[0x170],save[0x1300],actor[0x800],paths[5][32],points[4][16];
static u32 zero;
#define WWHD_GAME_DATA(a,t) (*(t*)((a)==1?(void*)&save_ptr:(void*)&zero))
static u32 save_ptr;
static unsigned events,symbols,gameinfos,nexts,mathcalls;
static u32 wwhd_dComIfG_getGameInfo(void){gameinfos++;return(u32)info;}
static s32 wwhd_save_event_test(u32 bit){assert(bit==0x902);events++;return 0;}
static s32 randomizer_ship_symbol(u32 c,u32 s){assert(c==(u32)save+0xD4&&s==2);symbols++;return 0;}
static u32 getstag(u32 self){assert(self==(u32)info+0x5150);return(u32)stag;}
static u32 randomizer_ship_room_path(u32 p,s32 room){assert(p==1&&room==-1);return(u32)paths[0];}
static u32 randomizer_ship_next_path(u32 path,s32 room){assert(room==-1);nexts++;unsigned n=((u8*)path-(u8*)paths)/32;return n<4?(u32)paths[n+1]:0;}
static s16 wwhd_cM_atan2s_hd(f32 y,f32 x){mathcalls++;return(s16)(int32_t)(atan2f(y,x)*10430.37835047045f);}
static f64 randomizer_ship_sqrt(f64 x){return sqrt(x);}
#include "ship_boundaries.h"
int main(void){
 save_ptr=(u32)save;*(u32*)(info+0x5150)=(u32)table;*(u32*)(table+0x15C)=(u32)getstag;
 const float corners[4][2]={{0,0},{0,100},{100,100},{100,0}};
 for(unsigned i=0;i<4;i++){*(f32*)(points[i]+4)=corners[i][0];*(f32*)(points[i]+12)=corners[i][1];}
 for(unsigned i=0;i<5;i++){*(u16*)paths[i]=4;*(u32*)(paths[i]+8)=(u32)points;}
 for(unsigned sea=0;sea<2;sea++)for(unsigned minigame=0;minigame<2;minigame++)for(unsigned position=0;position<3;position++){
  memset(actor,0xA5,sizeof(actor));actor[0x634]=1;*(f32*)(actor+0x314)=position==0?50:position==1?-10:110;*(f32*)(actor+0x31C)=50;
  *(u32*)(stag+12)=(sea?7:1)<<16;info[0x5CEA]=minigame;events=symbols=nexts=gameinfos=mathcalls=0;
  const s32 result=randomizer_ship_out_of_range((u32)actor);
  assert(result==(position!=0));
  if(result){assert(*(u32*)(actor+0x648)==(minigame?0x60A:0x5DD));assert(*(u32*)(actor+0x644)&0x400000);}
  if(sea){assert(events==1&&symbols==1);assert(nexts>=3);}
  else assert(!events&&!symbols);
 }
 /* The fourth sea polygon has its own native message; later polygons retain609. */
 *(u16*)paths[3]=2;actor[0x634]=1;*(u32*)(stag+12)=7<<16;info[0x5CEA]=0;
 *(f32*)(actor+0x314)=-10;*(f32*)(actor+0x31C)=50;
 assert(randomizer_ship_out_of_range((u32)actor)==1);assert(*(u32*)(actor+0x648)==0x609);
 /* A disabled path never calls stage/path/math APIs or changes actor bytes. */
 u8 before[sizeof(actor)];actor[0x634]=255;memcpy(before,actor,sizeof(actor));unsigned calls=gameinfos;
 assert(!randomizer_ship_out_of_range((u32)actor));assert(gameinfos==calls);assert(!memcmp(actor,before,sizeof(actor)));
 /* Undersized records still increment path index without querying ownership. */
 actor[0x634]=1;*(u32*)(stag+12)=7<<16;for(unsigned i=0;i<5;i++)*(u16*)paths[i]=2;
 events=symbols=nexts=mathcalls=0;assert(!randomizer_ship_out_of_range((u32)actor));assert(nexts==5&&!events&&!symbols&&!mathcalls);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(root/'test.c'),'-lm','-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)


if __name__ == '__main__':
    unittest.main()
