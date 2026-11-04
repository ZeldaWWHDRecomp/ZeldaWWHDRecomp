"""Full sea-menu static initialization, all354 names and exact FF coordinates."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class FFWarpStatics(unittest.TestCase):
    def test_consumer(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t u32;typedef uint16_t u16;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned char data[0x500000];static unsigned step;static uint32_t minimum,maximum,extent,limit;
static void* mem(u32 a){if(a>=0x10000000&&a<0x10500000)return data+a-0x10000000;if(a>0xFFFFFFFFu)return(void*)a;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_FFW(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) W(t,a)
#include "ff_warp_statics.h"
static u32 address(u32 a){return(u32)mem(a);}
void rfws_register(u32 record){
 if(step==0){assert(record==address(0x101F61DC));for(unsigned i=0;i<4;i++)assert(W(u32,0x10495400+4*i)==0);W(u32,0x10495404)=0x12345678;W(u32,0x100F7ED4)=minimum;W(u32,0x100F7ED8)=maximum;}
 else if(step==2){assert(record==address(0x101F61E8)&&W(u8,0x104953FC)==0x11);W(u32,0x100F7EDC)=0x01020304;}
 else{assert(step==4&&record==address(0x101F61F4)&&W(u8,0x104953FD)==0x22);W(u32,0x100F7EDC)=extent;W(u32,0x100F7EE0)=limit;}
 step++;
}
void rfws_first(u32 object){assert(step==1&&object==address(0x104953FC)&&W(u32,0x104953E4)==minimum&&W(u32,0x104953E8)==maximum);W(u8,0x104953FC)=0x11;W(u32,0x100F7ED4)=0;W(u32,0x100F7ED8)=0;step++;}
void rfws_second(u32 object){assert(step==3&&object==address(0x104953FD)&&W(u32,0x100F7EDC)==0x01020304);W(u8,0x104953FD)=0x22;step++;}
static uint64_t hash_word(uint64_t hash,unsigned value){hash=(hash^(value>>8))*1099511628211u;return(hash^(value&255))*1099511628211u;}
int main(void){unsigned cases=0;
 const uint32_t patterns[]={0,0x80000000u,0x3F800000u,0x7F800000u,0xFF800000u,0x7FC12345u,0x7F800001u,0xFFFFFFFFu};
 const struct{unsigned base,count;} groups[]={{0x104953C4,4},{0x10495410,8},{0x10495450,3},{0x10495468,3},{0x10495494,168},{0x104959D4,167},{0x10495F0C,1}};
 const u8 coordinates[20]={254,254,0,254,2,254,254,0,1,0,2,2,254,3,255,255,0,2,253,253};
 for(unsigned a=0;a<8;a++)for(unsigned b=0;b<8;b++){
  memset(data,0xA5,sizeof data);step=0;minimum=patterns[a];maximum=patterns[b];extent=patterns[(a+b)&7];limit=patterns[(a+2*b)&7];randomizer_ff_warp_statics();
  assert(step==5&&W(u32,0x104953E4)==minimum&&W(u32,0x104953E8)==maximum&&W(u32,0x104953EC)==extent&&W(u32,0x104953F0)==extent&&W(u32,0x104953F4)==limit&&W(u32,0x104953F8)==limit&&W(u32,0x10495404)==0x12345678);
  uint64_t hash=1469598103934665603u;unsigned count=0;const uint32_t base=(uint32_t)address(0x100F7EE4),vtable=(uint32_t)address(0x100F7B4C);
  for(unsigned g=0;g<7;g++)for(unsigned i=0;i<groups[g].count;i++){
   const unsigned object=groups[g].base+8*i;assert(W(u32,object+4)==vtable);const unsigned offset=(uint32_t)(W(u32,object)-base);assert(offset<=0xC34);hash=hash_word(hash,object-0x104953C4);hash=hash_word(hash,offset);count++;
  }
  assert(count==354&&hash==UINT64_C(0x03BD06C78ADF61AC));for(unsigned i=0;i<20;i++)assert(W(u8,0x10495480+i)==coordinates[i]);assert(W(u8,0x10495494)==(u8)(W(u32,0x10495494)&255));cases++;
 }
 printf("%u complete FF warp static-initializer cases passed (354 names each)\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);(path/'test.c').write_text(source)
            subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(path/'test.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

if __name__=='__main__':unittest.main()
