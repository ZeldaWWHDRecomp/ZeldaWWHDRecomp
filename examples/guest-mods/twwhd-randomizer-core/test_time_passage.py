"""Time boundaries, exceptions, callbacks, distinct save bases and defined casts."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class TimePassage(unittest.TestCase):
    def test_state_boundaries_and_callback_order(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef uint8_t u8;typedef int8_t s8;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static _Alignas(16) u8 save[2][0x1300],play[0x6000],environment[0x120C],before_env[0x120C],before_play[0x6000],before_save[2][0x1300],vtable[32];
static f32 original_time;static u16 original_date;
static u32 save_pointer;static u8 room,time_pass;static f32 evening=270,bias=2147483648.0f,day_length=360,day_stop=165,night_stop_time=345,half_day=180,zero;
static const char sea[]="sea",umikz[]="A_umikz",endumi[]="ENDumi";
static void* data(u32 a){switch(a){case 1:return &save_pointer;case 0x1004EF7C:return vtable;case 0x1004F22C:return(void*)sea;case 0x1004F224:return(void*)umikz;case 0x1004F230:return(void*)endumi;case 0x1047E6C8:return &room;case 0x101EBFD8:return &time_pass;case 0x1004F21C:return &evening;case 0x1004F220:return &bias;case 0x1004F108:return &day_length;case 0x1004F12C:return &day_stop;case 0x1004F118:return &night_stop_time;case 0x1004F10C:return &half_day;case 0x1004F0B0:return &zero;}assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static unsigned trace[64],count,night,daynight,days,mode,resolves;static u32 first_literal,second_literal;static u32* captured_literal;
static void note(unsigned n){assert(count<64);trace[count++]=n;}
static u32 wwhd_dComIfG_getGameInfo(void){return(u32)play;}
static void resolve(u32 object){u32*p=(u32*)object;if((mode==7||mode==8)&&!resolves)captured_literal=p;if((mode==7||mode==8)&&p[0]==(u32)play+0x5134){captured_literal[0]=(u32)endumi;if(mode==8)p[0]=(u32)sea;}if(mode==6){p[0]=p[0]==(u32)play+0x5134?first_literal:second_literal;}resolves++;note(p[0]==(u32)play+0x5134?2:1);if(mode==4&&p[0]==(u32)play+0x5134)p[0]=first_literal;else if(mode==5&&p[0]!=(u32)play+0x5134)p[0]=second_literal;}
#include "time_passage.h"
s32 randomizer_time_night_stop(void){note(3);return night;}
void randomizer_time_new_day(void){assert(!memcmp(save[0]+0x44,&original_time,4));assert(*(u16*)(save[0]+0x48)==original_date);note(10);days++;if(mode==1){*(f32*)(environment+0x1020)=42;*(u16*)(environment+0x108A)=123;}}
s32 randomizer_time_daynight(void){note(11);if(mode==2)*(f32*)(environment+0x1020)=100;return daynight;}
s32 randomizer_time_hour(void){assert(!memcmp(save[0]+0x44,&original_time,4));assert(*(u16*)(save[0]+0x48)==original_date);note(4);return -7;}
void randomizer_time_set_hour(s32 n){assert(n==-7);note(5);if(mode==3)*(f32*)(environment+0x1020)=99;}
s32 randomizer_time_minute(void){note(6);return 61;}
void randomizer_time_set_minute(s32 n){assert(n==61);note(7);if(mode==3)*(u16*)(environment+0x108A)=321;}
s32 randomizer_time_weekday(void){note(8);return 8;}
void randomizer_time_set_weekday(s32 n){assert(n==8);note(9);if(mode==3)save_pointer=(u32)save[1];}
static void setup(const char*stage,f32 time,f32 advance,u16 date){memset(save,0xA5,sizeof(save));memset(play,0,sizeof(play));memset(environment,0x3C,sizeof(environment));strcpy((char*)play+0x5134,stage);*(f32*)(save[0]+0x44)=time;*(u16*)(save[0]+0x48)=date;*(f32*)(environment+0x1028)=advance;original_time=time;original_date=date;save_pointer=(u32)save[0];*(u32*)(vtable+0x14)=(u32)resolve;room=0;time_pass=1;count=night=daynight=days=mode=resolves=0;}
static void verify(f32 expected,u16 date){if(!((isnan(expected)&&isnan(*(f32*)(environment+0x1020)))||*(f32*)(environment+0x1020)==expected))fprintf(stderr,"expected=%g actual=%g mode=%u days=%u\n",expected,*(f32*)(environment+0x1020),mode,days);assert((isnan(expected)&&isnan(*(f32*)(environment+0x1020)))||*(f32*)(environment+0x1020)==expected);assert(*(u16*)(environment+0x108A)==date);assert((isnan(expected)&&isnan(*(f32*)(save_pointer+0x44)))||*(f32*)(save_pointer+0x44)==expected);assert(*(u16*)(save_pointer+0x48)==date);assert(count>=10);const unsigned prefix[4]={1,1,2,3};assert(!memcmp(trace,prefix,sizeof(prefix)));const unsigned tail[6]={4,5,6,7,8,9};assert(!memcmp(trace+count-6,tail,sizeof(tail)));}
int main(void){
 const f32 times[]={-1,-0.5f,0,164.75f,165,179.75f,180,269.75f,270,344.75f,345,359.25f,359.75f,360};
 for(unsigned n=0;n<sizeof(times)/sizeof(times[0]);n++)for(unsigned stop=0;stop<2;stop++)for(unsigned event=0;event<2;event++){
  setup("sea",times[n],0.5f,65535);night=stop;play[0x5292]=event;memcpy(before_env,environment,sizeof(environment));memcpy(before_play,play,sizeof(play));memcpy(before_save,save,sizeof(save));
  f32 next=times[n];u16 date=65535;if(!stop&&!event){next=times[n]+0.5f;if(next>=360||next<=-1){next=0;date=0;}}
  randomizer_set_daytime((u32)environment);verify(next,date);
  *(f32*)(before_env+0x1020)=next;*(u16*)(before_env+0x108A)=date;*(f32*)(before_save[0]+0x44)=next;*(u16*)(before_save[0]+0x48)=date;
  assert(!memcmp(environment,before_env,sizeof(environment)));assert(!memcmp(play,before_play,sizeof(play)));assert(!memcmp(save,before_save,sizeof(save)));assert(resolves==(stop||event?9:6));assert(days==(date==0));
 }
 for(unsigned layer=0;layer<5;layer++){setup("sea",100,1,2);room=14;play[0x513F]=layer;randomizer_set_daytime((u32)environment);verify(layer==2||layer==3?100:101,2);}
 for(unsigned n=0;n<3;n++){setup("A_umikz",269.0f+n*0.5f,0.5f,2);night=1;randomizer_set_daytime((u32)environment);verify(n==0?269.5f:269.0f+n*0.5f,2);}
 setup("ENDumi",100,1,2);night=1;randomizer_set_daytime((u32)environment);verify(101,2);
 setup("ENDumi",100,1,2);randomizer_set_daytime((u32)environment);verify(102,2);
 for(daynight=0;daynight<2;){unsigned which=daynight;for(unsigned n=0;n<14;n++){setup("sea",times[n],0.5f,2);daynight=which;time_pass=0;f32 t=times[n];int run=which?t>=270&&t<345:t<165;randomizer_set_daytime((u32)environment);verify(t+(run?0.5f:0),2);}daynight=which+1;}
 setup("ENDumi",359.75f,0.5f,65535);mode=1;randomizer_set_daytime((u32)environment);verify(42.5f,123);assert(days==1);
 setup("sea",200,0.5f,2);mode=2;time_pass=0;randomizer_set_daytime((u32)environment);verify(100.5f,2);
 setup("sea",1,0.5f,2);mode=3;randomizer_set_daytime((u32)environment);verify(99,321);assert(*(f32*)(save[0]+0x44)==1&&*(u16*)(save[0]+0x48)==2);
 setup("sea",0,1,2);mode=4;first_literal=(u32)sea;assert(randomizer_time_stage((u32)sea));assert(resolves==3);
 static char a[0x40002],b[0x40002];memset(a,'X',sizeof(a));memset(b,'X',sizeof(b));setup("sea",0,1,2);mode=5;second_literal=(u32)a;first_literal=(u32)b; /* Separate bounded equal unterminated inputs. */
 /* Resolver can redirect the literal; the stage remains short and mismatches. */
 assert(!randomizer_time_stage((u32)sea));
 setup("sea",0,1,2);mode=6;second_literal=(u32)a;first_literal=(u32)b;assert(!randomizer_time_stage((u32)sea));assert(resolves==3);
 setup("sea",0,1,2);mode=7;assert(!randomizer_time_stage((u32)sea));assert(resolves==3);
 setup("sea",0,1,2);mode=8;assert(randomizer_time_stage((u32)sea));assert(resolves==3);
 const f32 unusual[]={NAN,INFINITY,-INFINITY,2147483648.0f,4294967296.0f};
 for(unsigned i=0;i<5;i++){setup("sea",unusual[i],0,2);randomizer_set_daytime((u32)environment);verify(i==0?NAN:0,i==0?2:3);}
 assert(randomizer_time_truncate(NAN)==0x80000000u);assert(randomizer_time_truncate(INFINITY)==0x7FFFFFFFu);assert(randomizer_time_truncate(-INFINITY)==0x80000000u);assert(randomizer_time_truncate(-0.5f)==0);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(root/'test.c'),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)


if __name__=='__main__':
    unittest.main()
