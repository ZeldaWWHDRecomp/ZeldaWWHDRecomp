"""Complete Moblin creator with asynchronous lantern and callback mutations."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class MoblinCreate(unittest.TestCase):
    def test_creation(self):
        code=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint16_t u16;typedef int8_t s8;typedef uint8_t u8;typedef float f32;
static unsigned char arena[0x20000],data[0x500000];
static u32 actor=0x30000000,save=0x30004000,game=0x30008000,morf=0x30010000,model=0x30011000,append=0x30012000,lantern=0x30013000;
static void* mem(u32 a){if(a>=0x30000000&&a<0x30020000)return arena+a-0x30000000;if(a>=0x10000000&&a<0x10500000)return data+a-0x10000000;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_MO2(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) W(t,a)
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u32 randomizer_daily_save(void){return save;}
static unsigned phase1,phase2,heap_ok,path_ok,event_on,switch_on,lantern_found,loads,constructs,executes,spawns,judges,deletes,hio,animations,cylinders,spheres,stage_calls,mutation;
static void string_call(u32 fn,u32 string){assert(fn==0x77777777);u32* s=(u32*)string;assert(s[1]==0x10014D94);}
#define RANDOMIZER_MO2_STRING_CALL(fn,s) string_call(fn,s)
#include "moblin_create.h"
u32 randomizer_mo2_construct(u32 a){assert(a==actor);constructs++;W(u32,a+0x2E4)=0x100;return actor;}
s32 randomizer_mo2_res_load(u32 p,u32 arc){assert(loads<2);loads++;if(loads==1){assert(p==actor+0x3D0&&arc==0x1001518C);return phase1;}assert(p==actor+0x3C8&&arc==0x10015184);return phase2;}
u32 randomizer_mo2_append(void){return append;}
u32 randomizer_mo2_layer(void){assert(W(u32,append+4)==0x7F812345&&W(u32,append)==0xFFFF23&&W(s8,append+0x21)==-2);return 7;}
u32 randomizer_mo2_spawn_request(u32 l,s32 n,u32 a,u32 b,u32 p){assert(l==7&&n==0xC1&&!a&&!b&&p==append);spawns++;return 123;}
u32 randomizer_mo2_judge(u32 cb,u32 key){assert(cb==0x025E1234&&*(u32*)key==123);judges++;return lantern_found?lantern:0;}
s32 randomizer_mo2_delete(u32 a){assert(a==actor);deletes++;return 0;}
s32 randomizer_mo2_event(u32 e,u32 bit){assert(e==save+0x644&&bit==0x1101);return event_on;}
s32 randomizer_mo2_switch(u32 s,s32 i,s32 r){assert(s==save&&i==W(u8,actor+0x3DC)&&r==-2);return switch_on;}
u32 randomizer_mo2_game(void){stage_calls++;return game;}
s32 randomizer_mo2_character2(u32 t,u32 n,s32 m){assert(t==game+0x50A0&&n==0x10015188&&m==1);return 77;}
s32 randomizer_mo2_character(u32 t,u32 n,s32 m){assert(t==game+0x50AC&&n==0x10015188&&!m);return 88;}
s32 randomizer_mo2_heap(u32 a,u32 cb,u32 size){assert(a==actor&&cb==0x021CC524&&!size);W(u32,actor+0x3E0)=morf;W(u32,morf+0x90)=model;if(mutation)W(u8,actor+0x3D8)=5;return heap_ok;}
s32 randomizer_mo2_hio_child(u32 name,u32 h){assert(name==0x100151AC&&h==0x104654E0);hio++;return 0x180;}
void randomizer_mo2_min(u32 a,f32 x,f32 y,f32 z){assert(a==actor&&x==-200&&y==-50&&z==-100);}
void randomizer_mo2_max(u32 a,f32 x,f32 y,f32 z){assert(a==actor&&x==125&&y==250&&z==250);}
void randomizer_mo2_animation(u32 a,s32 i,f32 m,u32 l,f32 speed,s32 snd){assert(a==actor&&i==0x4C&&m==1&&l==2&&speed==1&&snd==0x15);assert(W(u8,0x101BA692)==0&&W(u32,actor+0x39C)==4);animations++;}
u32 randomizer_mo2_path(s32 i,s32 r){assert(i==W(u8,actor+0x3DA)&&r==-2);return path_ok?0x12345678:0;}
void randomizer_mo2_acch(u32 a,u32 p,u32 old,u32 self,s32 e,u32 c,u32 speed,u32 r,u32 s){assert(a==actor+0xE0C&&p==actor+0x314&&old==actor+0x300&&self==actor&&e==1&&c==actor+0xDCC&&speed==actor+0x33C&&!r&&!s);}
void randomizer_mo2_wall(u32 c,f32 r,f32 h){assert(c==actor+0xDCC&&r==50&&h==50);}
void randomizer_mo2_status(u32 s,u32 w,u32 d,u32 a){assert(s==actor+0x1014&&w==200&&d==255&&a==actor);assert(W(f32,actor+0xECC)==107&&W(u8,actor+0x3A0)==10&&W(u8,actor+0x3A1)==10);}
void randomizer_mo2_cylinder(u32 c,u32 s){assert(c==actor+(cylinders?0x25C4:0x2494)&&s==(cylinders?0x101BAB8C:0x101BAB48));cylinders++;}
void randomizer_mo2_sphere(u32 c,u32 s){u32 offs[]={0x26F4,0x294C,0x2A78,0x2820},src[]={0x101BAA14,0x101BAA54,0x101BAA94,0x101BAAD4};assert(spheres<4&&c==actor+offs[spheres]&&s==src[spheres]);spheres++;}
s32 randomizer_mo2_execute(u32 a){assert(a==actor&&W(u8,actor+0x3A9)==5);executes++;if(executes==1)W(u32,actor+0x368)=0x87654321;else assert(W(u32,actor+0x368)==0x87654321);return 0;}
static void reset(void){memset(arena,0,sizeof arena);loads=constructs=executes=spawns=judges=deletes=hio=animations=cylinders=spheres=stage_calls=0;W(u32,actor+4)=0xCAFE;W(u32,actor+0xB0)=0xFFFF0000;W(u32,actor+0x314)=0x7F812345;W(s8,actor+0x326)=-2;W(u32,actor+0x460)=123;W(u8,0x101BA692)=1;W(u8,0x101BA690)=0;W(u8,0x101BA691)=1;strcpy((char*)mem(game+0x5134),"sea");}
int main(void){W(u32,0x10014DA8)=0x77777777;W(f32,0x10015168)=125;W(f32,0x1047B62C)=7;W(f32,0x1047B628)=6;strcpy(mem(0x1001517C),"ITest63");strcpy(mem(0x10015194),"GanonJ");strcpy(mem(0x1001519C),"kazeMB");strcpy(mem(0x100151A4),"Hyroom");for(unsigned i=0;i<10;i++){W(s8,0x101BAB3C+i)=i;W(f32,0x101BAB14+4*i)=i+.5f;}
unsigned cases=0;phase1=phase2=4;heap_ok=path_ok=1;
for(unsigned mode=0;mode<256;mode++)for(unsigned death=0;death<256;death++)for(event_on=0;event_on<2;event_on++)for(switch_on=0;switch_on<2;switch_on++)for(mutation=0;mutation<2;mutation++){
reset();W(u32,actor+0xB0)|=mode;W(u8,actor+0x2E0C)=100;W(s16,actor+0x324)=death;const s32 result=randomizer_moblin_create(actor);const unsigned ds=death==255?0:death;const unsigned blocked=event_on&&switch_on&&ds;assert(result==(blocked?5:4));assert(constructs==1&&W(u32,actor+0x2E4)==0x108);assert(W(u8,actor+0x3DC)==ds);assert(executes==(blocked?0:2));if(!blocked){assert(W(u8,0x101BA692)==0&&W(u32,actor+0x39C)==4);assert(animations==(mutation||mode==5));assert(hio==1&&W(s8,0x104654E0)==-128);assert(W(u32,actor+0x29A4)==0x021C9AA0&&W(u32,actor+0x2AD0)==0x021C9AA0);assert(cylinders==2&&spheres==4);for(unsigned i=0;i<10;i++){assert(W(s8,actor+0x321C+i)==(s8)i&&W(f32,actor+0x3228+4*i)==i+.5f);}}cases++;}
for(phase1=0;phase1<6;phase1++)for(phase2=0;phase2<6;phase2++){reset();event_on=switch_on=mutation=0;assert(randomizer_moblin_create(actor)==(phase1!=4?(s32)phase1:phase2!=4?(s32)phase2:4));assert(loads==(phase1==4?2:1));cases++;}
phase1=phase2=4;
for(unsigned state=0;state<256;state++)for(lantern_found=0;lantern_found<2;lantern_found++){reset();W(u32,actor+0xB0)=1;W(u8,actor+0x2E0C)=state;const s32 result=randomizer_moblin_create(actor);assert(result==((state==100||(state&&lantern_found))?4:0));assert(spawns==(state==0));assert(judges==(state!=0&&state!=100));if(state&&state!=100&&!lantern_found)assert(deletes==((s8)(state+1)>20));if(state&&state!=100&&lantern_found)assert(W(u32,lantern+0x3C0)==0xCAFE);cases++;}
const char* stages[]={"sea","ITest63","GanonJ","kazeMB","Hyroom"};
for(unsigned st=0;st<5;st++)for(heap_ok=0;heap_ok<2;heap_ok++)for(path_ok=0;path_ok<2;path_ok++)for(unsigned hi=0;hi<2;hi++)for(unsigned dungeon=0;dungeon<4;dungeon++){
reset();event_on=switch_on=mutation=0;W(u32,actor+0xB0)=0xFF07000F;W(u32,actor+0x2E4)=0x8888;W(u8,0x101BA690)=hi;strcpy(mem(game+0x5134),stages[st]);const u8 flags[]={0,0x39,0x3E,0x3A};W(u8,save+0x2E)=flags[dungeon];assert(randomizer_moblin_create(actor)==(heap_ok&&path_ok?4:5));assert(!constructs);assert(W(s32,actor+0x3A4)==(st==4?77:88));assert(W(s8,0x104654A4)==(st>=1&&st<=3));if(heap_ok){assert(hio==!hi);if(path_ok){assert(W(s16,actor+0x8C2)==23&&W(u8,actor+0x3DD)==0);assert(W(s16,actor+0x848)==(dungeon==0?5:0));assert(W(u32,actor+0x2D74)==0x12345678&&W(s8,actor+0x2D70)==8);}}cases++;}
printf("Moblin create: %u cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'test.c').write_text(code)
            subprocess.run([os.environ.get("CC", "cc"),'-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__':unittest.main()
