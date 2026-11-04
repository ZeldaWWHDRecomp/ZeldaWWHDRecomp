"""Complete warp child factory and native SafeString callback/slot lifetimes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class FFWarpChild(unittest.TestCase):
    def test_consumer(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef int32_t s32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned char arena[0x10000],data[0x500000];static const u32 self=0x30000000,description=0x30001000,children=0x30002000,newchildren=0x30003000,child=0x30004000;
static void* mem(u32 a){if(a>=self&&a<self+sizeof arena)return arena+a-self;if(a>=0x10000000&&a<0x10500000)return data+a-0x10000000;if(a>0xFFFFFFFFu)return(void*)a;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_FFW(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) W(t,a)
static void assure(u32 object);
#define RFF_ASSURE(o) assure(o)
static int local(u32 o){return o>0xFFFFFFFFu&&!(o>=(u32)data&&o<(u32)data+sizeof data)&&!(o>=(u32)arena&&o<(u32)arena+sizeof arena);}
#define RFF_STRING_TEXT(o) (local(o)?((const u32*)(o))[0]:(u32)W(u32,o))
#include "ff_warp_child.h"
static unsigned assures,allocations,constructs,setups,bases,mutation,allocation_fail,constructor_fail,expected_index,expected_resource;static u32 last_size,stored_slot;
static const char* names[11]={"L_Map_00","L_WarpArea_00","L_WarpArea_01","L_WarpArea_02","L_WarpArea_04","L_WarpArea_05","L_WarpArea_07","L_WarpArea_08","L_WarpArea_03","L_WarpArea_06","L_WarpArea_09"};
static void assure(u32 object){assures++;if(local(object)){assert(((const u32*)object)[1]==(u32)&WWHD_GAME_DATA(0x1010394C,u8));assert(strcmp((const char*)((const u32*)object)[0],"L_WarpArea_09")==0);}if(mutation==1&&assures==1)W(u32,0x10400010)=999;if(mutation==2&&object==(u32)&WWHD_GAME_DATA(0x1049D634,u8))W(u32,description+0x14)=0x10403000;}
u32 rff_allocate(u32 size,u32 heap,u32 align){assert(size==(expected_resource==0?0x164:0x50)&&heap==555&&align==4);allocations++;last_size=size;stored_slot=children+(expected_index<W(u32,self+0x48)?expected_index*4:0);if(mutation==3){W(u32,self+0x4C)=newchildren;W(u32,newchildren+expected_index*4)=child+4;}return allocation_fail?0:child;}
static u32 construct(u32 a){assert(a==child);constructs++;if(mutation==4){W(u32,self+0x48)=0;W(u32,children)=child+8;}return constructor_fail?0:child+12;}
u32 rff_chart_construct(u32 a){assert(expected_resource==0);return construct(a);}
u32 rff_area_construct(u32 a){assert(expected_resource==1);return construct(a);}
static u32 setup(u32 a,u32 b,u32 c,u32 d){assert(!b&&!c&&d==description);assert(a==(mutation==3?child+4:mutation==4?child+8:child+12));setups++;return 66;}
u32 rff_chart_create(u32 a,u32 b,u32 c,u32 d){assert(expected_resource==0);return setup(a,b,c,d);}
u32 rff_area_setup(u32 a,u32 b,u32 c,u32 d){assert(expected_resource==1);return setup(a,b,c,d);}
u32 rff_base_child(u32 object,u32 index,u32 d){assert(object==self&&index==expected_index&&d==description);bases++;return 77;}
static void put(u32 object,u32 text,const char* value){W(u32,object)=text;W(u32,object+4)=0x1010394C;strcpy(mem(text),value);}
static void reset(unsigned index,unsigned resource){memset(arena,0,sizeof arena);memset(data,0,sizeof data);assures=allocations=constructs=setups=bases=mutation=allocation_fail=constructor_fail=0;expected_index=index;expected_resource=resource;W(u32,0x1018C404)=0x10400000;W(u32,0x10400010)=555;W(u32,self+0x48)=11;W(u32,self+0x4C)=children;for(unsigned i=0;i<10;i++)put(0x1049D634+8*i,0x10401000+0x20*i,names[i]);put(0x10495F0C,0x10402000,"Map_00");put(0x1049D590,0x10402020,"WarpArea_00");put(description+0x14,0x10403000,index<11?names[index]:"unknown");put(description+8,0x10403100,resource==0?"Map_00":resource==1?"WarpArea_00":"Other");}
int main(void){unsigned cases=0;
 for(unsigned i=0;i<12;i++)for(unsigned resource=0;resource<3;resource++)for(unsigned count=0;count<13;count++)for(unsigned fail=0;fail<2;fail++)for(unsigned ctorfail=0;ctorfail<2;ctorfail++){
  reset(i,resource);W(u32,self+0x48)=count;allocation_fail=fail;constructor_fail=ctorfail;const u32 result=randomizer_ff_warp_child(self,description);const unsigned alloc=i<11&&resource<2;assert(allocations==alloc&&constructs==(alloc&&!fail)&&setups==(alloc&&!fail&&!ctorfail)&&bases==(i<11&&resource==2));assert(result==(i>=11?1:resource==2?77:fail||ctorfail?0:66));if(alloc)assert(W(u32,children+(i<count?4*i:0))==(fail||ctorfail?0:child+12));cases++;
 }
 for(unsigned resource=0;resource<2;resource++)for(unsigned mut=1;mut<=4;mut++){
  reset(mut==4?1:0,resource);mutation=mut;if(mut==2){W(u32,description+0x14)=W(u32,0x1049D634);strcpy(mem(0x10403000),"changed");}assert(randomizer_ff_warp_child(self,description)==66&&setups==1&&allocations==1);if(mut==3)assert(W(u32,children)==child+12&&W(u32,newchildren)==child+4);if(mut==4)assert(W(u32,children)==child+8&&W(u32,children+4)==child+12);cases++;
 }
 /* Exactly 0x40001 nonzero matching bytes never count as a terminated match. */
 reset(0,2);const u32 a=0x10100000,b=0x10150000;memset(mem(a),'A',0x40002);memset(mem(b),'A',0x40002);W(u32,description+0x14)=a;W(u32,0x1049D634)=b;assert(!rff_same_name(description+0x14,0x1049D634));cases++;
 for(unsigned at=0;at<=0x40000;at+=0x10000){W(u8,a+at)=0;W(u8,b+at)=0;assert(rff_same_name(description+0x14,0x1049D634));W(u8,a+at)='A';W(u8,b+at)='A';cases++;}
 /* A changed name is reloaded for byte comparison after expected's callback. */
 reset(0,0);mutation=2;W(u32,description+0x14)=0x10403200;strcpy(mem(0x10403200),"changed");assert(randomizer_ff_warp_child(self,description)==66);cases++;
 printf("%u complete FF warp child-factory cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);(path/'test.c').write_text(source)
            subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(path/'test.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

if __name__=='__main__':unittest.main()
