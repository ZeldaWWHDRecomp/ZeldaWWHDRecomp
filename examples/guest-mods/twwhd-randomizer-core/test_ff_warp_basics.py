"""Complete FF warp lookup, opening, child animation and confirmation owners."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class FFWarpBasics(unittest.TestCase):
    def test_consumers(self):
        source=r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef uint16_t u16;typedef int32_t s32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned char arena[0x50000],data[0x500000];static const u32 self=0x30000000,grid=0x30010000,othergrid=0x30011000,cell=0x30020000,chart=0x30030000,otherchart=0x30031000,owner=0x30040000,children=0x30041000;
static void* mem(u32 a){if(a>=self&&a<self+sizeof arena)return arena+a-self;if(a>=0x10000000&&a<0x10500000)return data+a-0x10000000;if(a>0xFFFFFFFFu)return(void*)a;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_FFW(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) W(t,a)
#include "ff_warp_basics.h"
static unsigned finds,match,selected,selection_calls,resets,updates,refreshes,hides,restarts,prepares,messages,mutation;static u32 last_selection,expected_label,expected_pane,hidden[10];static unsigned custom;
u32 rfw_resource(u32 index){return randomizer_ff_warp_resource(index);}
u32 rfw_grid_find(u32 p,u32 coordinate){assert(p==(mutation==1&&finds?othergrid:grid));const u32 index=(coordinate-(u32)&WWHD_GAME_DATA(0x10495480,u8))/2;assert(index==finds);finds++;if(mutation==1)W(u32,self+0x14C)=othergrid;return index==match?cell:cell+4;}
void rfw_selection(u32 p,u32 value){assert(p==77);last_selection=value;selection_calls++;if(mutation==2)W(u32,self+0x14C)=othergrid;}
void rfw_reset_details(u32 p){assert(p==grid);resets++;if(mutation==3)W(u32,self+0x54)=otherchart;}
void rfw_update_map(u32 p){assert(p==(mutation==3&&resets?otherchart:chart));updates++;if(mutation==4)W(u32,self+0x54)=otherchart;}
void rfw_refresh_chart(u32 p){assert(p==self);refreshes++;}
void rfw_hide_area(u32 p){hidden[hides++]=p;if(mutation==5)W(u32,self+0x48)=2;}
void rfw_restart_text(u32 p,u32 index,u32 mode){assert(p==chart&&index==111&&mode==335);restarts++;if(mutation==6){W(u32,self+0x90)=otherchart;W(u32,self+0xA8)=2;}}
void rfw_prepare_message(u32 pane,u32 message,u32 flag){assert(pane==(mutation==6?12:expected_pane)&&message==(mutation==6?otherchart:chart)&&flag==1);prepares++;if(mutation==7)W(u32,self+0x90)=otherchart;}
void rfw_reset_message(u32 pane,u32 archive,u32 label){const u32* a=(const u32*)archive;assert(pane==expected_pane&&a[0]==99&&a[1]==(u32)&WWHD_GAME_DATA(0x1010394C,u8));if(custom){const u32* l=(const u32*)label;assert(strcmp((const char*)l[0],"00076")==0&&l[1]==(u32)&WWHD_GAME_DATA(0x1010394C,u8));}else assert(label==(u32)&WWHD_GAME_DATA(0x1049B0F0,u8)+(unsigned int)(expected_label*8));messages++;}
static void reset(void){memset(arena,0,sizeof arena);memset(data,0,sizeof data);finds=selected=selection_calls=resets=updates=refreshes=hides=restarts=prepares=messages=mutation=custom=0;match=99;W(u32,self+0x14C)=grid;W(u32,grid+0x118)=cell;W(u32,0x101F8344)=owner;W(u32,owner+0x1BC)=77;W(u32,self+0x54)=chart;W(u32,chart+0x14C)=grid;W(u32,self+0x48)=11;W(u32,self+0x4C)=children;for(unsigned i=0;i<11;i++)W(u32,children+4*i)=i+100;W(u32,self+0x90)=chart;W(u32,self+0xC4)=111;W(u32,chart+0x64C)=222;W(u32,otherchart+0x64C)=333;W(u32,self+0x64)=10;W(u32,self+0x68)=11;W(u32,self+0x6C)=12;W(u32,self+0x8C)=cell;W(u32,self+0x44)=grid;W(u32,grid+4)=othergrid;W(u32,othergrid+0x1C)=99;for(unsigned i=0;i<9;i++)W(u32,0x100F7BAC+4*i)=i+1;}
int main(void){unsigned cases=0;
 for(u32 value=0;value<65536;value++){reset();const s32 result=randomizer_ff_warp_find(value);assert(result==(value==0?9:value<=9?(s32)value-1:-1));cases++;}
 for(unsigned index=0;index<20;index++){reset();if(index!=9)W(u32,0x100F7BAC+4*index)=index+100;assert(randomizer_ff_warp_resource(index)==(index==9?0:index+100));cases++;}
 for(unsigned i=0;i<11;i++)for(unsigned available=0;available<2;available++)for(unsigned hasowner=0;hasowner<2;hasowner++){
  reset();match=i;W(u8,cell+0x97)=available;W(u32,owner+0x1BC)=hasowner?77:0;randomizer_ff_warp_open(self);const unsigned found=i<10&&hasowner;assert(selection_calls==found&&finds==(found?i+1:10));if(found)assert(last_selection==(available?i+1:0)&&W(u32,self+0x15C)==i&&W(u8,self+0x160)==1&&W(u8,grid+0x151)==1);cases++;
 }
 for(unsigned mut=1;mut<=2;mut++){reset();match=9;mutation=mut;randomizer_ff_warp_open(self);assert(selection_calls==1&&W(u8,othergrid+0x151)==1);cases++;}
 for(unsigned chart_present=0;chart_present<2;chart_present++)for(unsigned details=0;details<2;details++)for(unsigned mut=0;mut<6;mut++){
  reset();mutation=mut;W(u32,self+0x54)=chart_present?chart:0;W(u32,chart+0x14C)=details?grid:0;randomizer_ff_warp_initialize(self);assert(W(u32,self+0x58)==0xFFFFFFFFu&&resets==(chart_present&&details)&&updates==chart_present&&refreshes==1&&hides==10);for(unsigned i=0;i<10;i++)assert(hidden[i]==(mut==5&&i>=1?100:i+101));cases++;
 }
 const unsigned kinds[]={0,1,2,3,0xFFFFFFFF};const unsigned modes[]={0,1,2,3,0xFFFFFFFF};const unsigned labels[]={0,8,18,50,51,52,0xFFFFFFFF};const unsigned indices[]={0,9,10,11,0xFFFFFFFF,0xFFFFFFF7,0xFFFFFFFA};
 for(unsigned k=0;k<5;k++)for(unsigned m=0;m<5;m++)for(unsigned l=0;l<7;l++)for(unsigned n=0;n<7;n++){
  reset();W(u32,self+0xA8)=kinds[k];W(u32,self+0x98)=modes[m];W(u32,cell+0xC)=labels[l];W(u32,self+0xC0)=indices[n];expected_pane=kinds[k]<3?10+kinds[k]:10;expected_label=labels[l];if(modes[m]==2&&labels[l]==8){expected_label=(unsigned int)(indices[n]+8);if(expected_label==18)expected_label=51;}custom=expected_label==51;randomizer_ff_warp_confirmation(self);assert(messages==1&&!restarts&&!prepares);cases++;
 }
 for(unsigned k=0;k<5;k++)for(unsigned mut=0;mut<8;mut++){
  reset();W(u8,self+0xCF)=1;W(u32,self+0xA8)=kinds[k];expected_pane=kinds[k]<3?10+kinds[k]:10;mutation=mut;randomizer_ff_warp_confirmation(self);assert(restarts==1&&prepares==1&&!messages&&W(u32,self+0xC4)==(mut==6||mut==7?333:222));cases++;
 }
 printf("%u FF warp basic-consumer cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);(path/'test.c').write_text(source)
            subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(path/'test.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

if __name__=='__main__':unittest.main()
