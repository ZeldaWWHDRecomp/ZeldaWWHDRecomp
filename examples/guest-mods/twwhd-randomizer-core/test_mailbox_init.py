"""Mailbox initialization: independent mail gates and native setup callbacks."""
from pathlib import Path
import subprocess,tempfile,unittest
class Mailbox(unittest.TestCase):
 def test_init(self):
  code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u8 actor[0xB00],save[0x700],morph[0xA0],alternate[0xA0],model[0x200],cylinder[80],name[8];
static float constants[]={-50,70,0,200,10};static unsigned mask,wallet,seq,stockcount,stocks[5],bosses[2],bosscount;
static void* memory(u32 a){if(a==0x100314C0)return cylinder;if(a==0x1003140C)return name;assert(a>=0x100313F8&&a<=0x10031408);return(u8*)constants+a-0x100313F8;}
#define WWHD_GAME_DATA(a,t) (*(t*)memory(a))
static u32 randomizer_daily_save(void){return(u32)save;}
static u32 randomizer_daily_boss(u32 stage){assert(bosscount<2);bosses[bosscount++]=stage;return stage==6?(mask>>3)&1:(mask>>4)&1;}
static void randomizer_daily_stock(u32 letter){assert(stockcount<5);stocks[stockcount++]=letter;}
static u32 wwhd_dComIfGs_checkGetItem(u32 item){assert(item==0x31);return(mask>>1)&1;}
#include "mailbox_init.h"
static void step(unsigned n){assert(seq++==n);}
u32 randomizer_mail_symbol(u32 c,u32 symbol){assert(c==(u32)save+0xD4&&symbol==2);return mask&1;}
u32 randomizer_mail_delivered(u32 l){assert(l==0xAC03);return(mask>>2)&1;}
void randomizer_mail_animation(u32 a,u32 n,u32 f){step(0);assert(a==(u32)actor&&n==1&&!f);assert(*(unsigned*)(actor+0xA08)==1&&*(unsigned*)(actor+0x7C8)==0xFFFFFFFF);assert(actor[0x38B]==6&&actor[0x389]==5&&*(unsigned*)(actor+0x39C)==0x2000000A);}
void randomizer_mail_matrix(u32 a){step(1);assert(a==(u32)actor);*(u32*)(actor+0x7EC)=(u32)morph;}
void randomizer_mail_model_calc(u32 m){step(2);assert(m==(u32)morph);*(u32*)(actor+0x7EC)=(u32)alternate;*(u32*)(alternate+0x90)=(mask&32)?0:(u32)model;}
void randomizer_mail_cull(u32 a,f32 x,f32 y,f32 z,f32 X,f32 Y,f32 Z){step(3);assert(a==(u32)actor&&x==-50&&y==0&&z==-50&&X==70&&Y==200&&Z==70);assert(*(u32*)(actor+0x348)==((mask&32)?0:(u32)model+0xC8));}
void randomizer_mail_status(u32 s,u32 w,u32 f,u32 a){step(4);assert(s==(u32)actor+0x654&&w==255&&f==255&&a==(u32)actor);assert(*(float*)(actor+0x364)==10);}
void randomizer_mail_cylinder(u32 c,u32 s){step(5);assert(c==(u32)actor+0x690&&s==(u32)cylinder);}
u32 randomizer_mail_price(u32 a){step(6);assert(a==(u32)actor&&*(u32*)(actor+0x6D4)==a+0x654);return 0x1234;}
void randomizer_mail_mode(u32 a,u32 p,u32 m){step(7);assert(a==(u32)actor&&!p&&!m&&actor[0xA00]==0x34);}
void randomizer_mail_effect(u32 p){step(8);assert(p==(u32)actor+0x314);}
void randomizer_mail_event_actor(u32 c,u32 n,u32 a){step(9);assert(c==(u32)actor+0x3E0&&n==(u32)name&&a==(u32)actor);}
int main(void){for(mask=0;mask<64;mask++)for(wallet=0;wallet<256;wallet++){
 memset(actor,0,sizeof actor);seq=stockcount=bosscount=0;save[0x32]=wallet;randomizer_mailbox_init((u32)actor);assert(seq==10);
 unsigned n=0;if(mask&1)assert(stocks[n++]==0xB503);if(mask&2)assert(stocks[n++]==0x7D03);if((mask&4)&&(mask&8))assert(stocks[n++]==0x7C03);if(mask&16)assert(stocks[n++]==0x7B03);if(wallet==1||wallet==2)assert(stocks[n++]==0x7A03);assert(n==stockcount);
 assert(bosscount==1+!!(mask&4));if(mask&4)assert(bosses[0]==6);assert(bosses[bosscount-1]==4);
 }return 0;}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(code)
   subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
